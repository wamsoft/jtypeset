/**
 * microtex_handler.cpp — MicroTeX の Font / TextLayout / Graphics2D を typeset の表示リストへ
 *
 * MicroTeX は描画先を抽象クラスで受ける（graphic/graphic.h）。ここでは
 *   Font        → glyphware の Face（MicroTeX 付属フォントはファイルから、システムフォント名は FontSet のキーへ）
 *   TextLayout  → typeset のシェイパで幅・高さを測り、Graphics2D::drawText で描く
 *   Graphics2D  → 変形行列を持ち、drawChar は GlyphRun、矩形・線は PathItem に落とす
 * とする。座標は MicroTeX の px をそのまま pt とみなす（Formula::PIXELS_PER_POINT = 1）。
 * TeXRender の幅・高さは int で丸められるので、10 倍で組んで 1/10 に縮めて置く。
 */

#include "microtex_handler.hpp"

#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "typeset/dl/glyph_transform.hpp"
#include "typeset/inl/shaper.hpp"
#include "typeset/text/utf.hpp"

#include "graphic/graphic.h"
#include "latex.h"
#include "render.h"
#include "core/formula.h"

namespace typeset::handlers {

namespace {

constexpr float kOversample = 10.0f;

std::wstring toWide(const std::u16string& s) {
    if constexpr (sizeof(wchar_t) == 2) {
        return std::wstring(s.begin(), s.end());
    } else {
        std::wstring out;
        for (size_t i = 0; i < s.size(); ++i) {
            char32_t c = s[i];
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size()) {
                const char32_t lo = s[i + 1];
                if (lo >= 0xDC00 && lo < 0xE000) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    ++i;
                }
            }
            out.push_back(static_cast<wchar_t>(c));
        }
        return out;
    }
}

std::u16string fromWide(const std::wstring& s) {
    if constexpr (sizeof(wchar_t) == 2) {
        return std::u16string(s.begin(), s.end());
    } else {
        std::u16string out;
        for (wchar_t wc : s) {
            char32_t c = static_cast<char32_t>(wc);
            if (c >= 0x10000) {
                c -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
                out.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
            } else {
                out.push_back(static_cast<char16_t>(c));
            }
        }
        return out;
    }
}

/// ハンドラ全体で共有する状態（MicroTeX の Font::create は static なので、ここから引く）
struct Context {
    font::FontSet* fonts = nullptr;
    std::string textFamily, sansFamily;
    std::map<std::string, std::string> fileKeys;    ///< ファイルパス → FontSet のキー
    std::vector<std::shared_ptr<glyphware::Face>> usedFaces;
    bool initialized = false;
    std::string initError;
    std::mutex mutex;
};

Context& ctx() {
    static Context c;
    return c;
}

} // namespace

} // namespace typeset::handlers

//------------------------------------------------------------------------------
// MicroTeX の抽象の実装（tex 名前空間の static 関数を定義する）
//------------------------------------------------------------------------------

namespace tex {

using typeset::handlers::ctx;

class Font_ts : public Font {
public:
    std::shared_ptr<glyphware::Face> face;
    std::string key;        ///< FontSet のキー（シェイプ用）
    float size = 1.0f;
    int style = PLAIN;

    float getSize() const override { return size; }
    sptr<Font> deriveFont(int s) const override {
        auto f = std::make_shared<Font_ts>(*this);
        f->style = s;
        return f;
    }
    bool operator==(const Font& o) const override {
        const auto* f = dynamic_cast<const Font_ts*>(&o);
        return f && f->face == face && f->size == size && f->style == style;
    }
    bool operator!=(const Font& o) const override { return !(*this == o); }
};

Font* Font::create(const std::string& file, float size) {
    auto& c = ctx();
    auto* f = new Font_ts();
    f->size = size;
    auto it = c.fileKeys.find(file);
    if (it != c.fileKeys.end()) {
        f->key = it->second;
        f->face = c.fonts->find(f->key);
    } else {
        f->key = "microtex:" + file;
        f->face = c.fonts->loadFile(file, f->key);
        if (f->face) c.fileKeys[file] = f->key;
    }
    if (!f->face) {
        delete f;
        throw ex_invalid_state("cannot load font file " + file);
    }
    return f;
}

sptr<Font> Font::_create(const std::string& name, int style, float size) {
    auto& c = ctx();
    auto f = std::make_shared<Font_ts>();
    f->size = size;
    f->style = style;
    f->key = (name == "SansSerif") ? c.sansFamily : c.textFamily;
    f->face = c.fonts->find(f->key);
    if (!f->face) {
        f->key = c.textFamily;
        f->face = c.fonts->find(f->key);
    }
    if (!f->face) throw ex_invalid_state("no text font registered for MicroTeX: " + name);
    return f;
}

class TextLayout_ts : public TextLayout {
public:
    std::u16string text;
    sptr<Font_ts> font;

    void getBounds(Rect& r) override {
        typeset::TextStyle st;
        st.font.family = {font->key};
        st.size = font->size;
        const typeset::inl::ShapedText shaped =
            typeset::inl::shapeText(text, st, *ctx().fonts, typeset::WritingMode::HorizontalTb);
        typeset::Pt asc = 0, desc = 0;
        typeset::font::faceAscentDescent(*font->face, font->size, asc, desc);
        r.x = 0;
        r.y = -asc;
        r.w = shaped.advance;
        r.h = asc + desc;
    }
    void draw(Graphics2D& g2, float x, float y) override {
        const Font* prev = g2.getFont();
        g2.setFont(font.get());
        g2.drawText(std::wstring(typeset::handlers::toWide(text)), x, y);
        g2.setFont(prev);
    }
};

sptr<TextLayout> TextLayout::create(const std::wstring& src, const sptr<Font>& font) {
    auto tl = std::make_shared<TextLayout_ts>();
    tl->text = typeset::handlers::fromWide(src);
    tl->font = std::static_pointer_cast<Font_ts>(font);
    return tl;
}

/**
 * 表示リストへ描く Graphics2D
 */
class Graphics2D_ts : public Graphics2D {
public:
    Graphics2D_ts(std::vector<typeset::dl::Item>& out, const typeset::Matrix& base)
        : out_(out), base_(base), ctm_(base) {}

    ~Graphics2D_ts() { flush(); }

    void setColor(color c) override { color_ = c; }
    color getColor() const override { return color_; }
    void setStroke(const Stroke& s) override { stroke_ = s; }
    const Stroke& getStroke() const override { return stroke_; }
    void setStrokeWidth(float w) override { stroke_.lineWidth = w; }
    const Font* getFont() const override { return font_; }
    void setFont(const Font* font) override { font_ = font; }

    void translate(float dx, float dy) override {
        ctm_ = typeset::multiply(ctm_, typeset::Matrix::translation(dx, dy));
    }
    void scale(float sx, float sy) override {
        ctm_ = typeset::multiply(ctm_, typeset::Matrix::scaling(sx, sy));
        sx_ *= sx;
        sy_ *= sy;
    }
    void rotate(float angle) override {
        typeset::Matrix r;
        const float c = std::cos(angle), s = std::sin(angle);
        r.xx = c; r.xy = -s; r.yx = s; r.yy = c;
        ctm_ = typeset::multiply(ctm_, r);
    }
    void rotate(float angle, float px, float py) override {
        translate(px, py);
        rotate(angle);
        translate(-px, -py);
    }
    void reset() override {
        ctm_ = base_;
        sx_ = sy_ = 1.0f;
    }
    float sx() const override { return sx_; }
    float sy() const override { return sy_; }

    void drawChar(wchar_t c, float x, float y) override {
        const auto* f = dynamic_cast<const Font_ts*>(font_);
        if (!f || !f->face) return;
        const uint32_t gid = f->face->glyphIndex(static_cast<char32_t>(c));
        putGlyph(f->face, gid, f->size, typeset::Point{x, y}, f->style);
    }

    void drawText(const std::wstring& text, float x, float y) override {
        const auto* f = dynamic_cast<const Font_ts*>(font_);
        if (!f || !f->face) return;
        typeset::TextStyle st;
        st.font.family = {f->key};
        st.size = f->size;
        const typeset::inl::ShapedText shaped = typeset::inl::shapeText(
            typeset::handlers::fromWide(text), st, *ctx().fonts, typeset::WritingMode::HorizontalTb);
        for (const typeset::inl::PlacedGlyph& g : shaped.glyphs) {
            if (!g.face) continue;
            putGlyph(g.face, g.gid, f->size, typeset::Point{x + g.inline_, y + g.block}, f->style);
        }
    }

    void drawLine(float x1, float y1, float x2, float y2) override {
        typeset::Path p;
        p.addLine({x1, y1}, {x2, y2});
        emitPath(p, false, true);
    }
    void drawRect(float x, float y, float w, float h) override {
        typeset::Path p;
        p.addRect(typeset::Rect{x, y, w, h});
        emitPath(p, false, true);
    }
    void fillRect(float x, float y, float w, float h) override {
        typeset::Path p;
        p.addRect(typeset::Rect{x, y, w, h});
        emitPath(p, true, false);
    }
    void drawRoundRect(float x, float y, float w, float h, float rx, float ry) override {
        emitPath(roundRect(x, y, w, h, rx, ry), false, true);
    }
    void fillRoundRect(float x, float y, float w, float h, float rx, float ry) override {
        emitPath(roundRect(x, y, w, h, rx, ry), true, false);
    }

    void flush() {
        if (open_ && !run_.glyphs.empty()) out_.push_back(run_);
        run_ = typeset::dl::GlyphRun{};
        open_ = false;
    }

private:
    static typeset::Color toColor(color c) { return typeset::Color::argb(c); }

    /// ctm の一様スケール成分（グリフのサイズに使う）
    float uniformScale() const { return std::sqrt(std::fabs(ctm_.determinant())); }

    void putGlyph(const std::shared_ptr<glyphware::Face>& face, uint32_t gid, float fontSize,
                  typeset::Point local, int style) {
        const float s = uniformScale();
        if (s <= 0.0f) return;
        const float size = fontSize * s;
        typeset::Mat2 lin = ctm_.linear();
        lin.xx /= s; lin.xy /= s; lin.yx /= s; lin.yy /= s;
        if (style & ITALIC) lin.xy += typeset::dl::kFakeItalicSkew;
        const float embolden = (style & BOLD) ? typeset::dl::fakeBoldWidth(size) : 0.0f;
        const typeset::Color fill = toColor(color_);
        const bool same = open_ && run_.face == face && run_.size == size && run_.embolden == embolden &&
                          run_.fill == fill;
        if (!same) {
            flush();
            run_.face = face;
            run_.size = size;
            run_.fill = fill;
            run_.embolden = embolden;
            open_ = true;
            auto& used = ctx().usedFaces;
            bool have = false;
            for (const auto& u : used) if (u == face) { have = true; break; }
            if (!have) used.push_back(face);
        }
        typeset::dl::Glyph g;
        g.gid = gid;
        g.pos = ctm_.apply(local);
        g.xform = lin;
        run_.glyphs.push_back(g);
    }

    void emitPath(const typeset::Path& local, bool fill, bool stroke) {
        flush();
        typeset::dl::PathItem item;
        item.path = local.transformed(ctm_);
        if (fill) item.fill = toColor(color_);
        if (stroke) {
            typeset::Stroke s;
            s.color = toColor(color_);
            s.width = stroke_.lineWidth * uniformScale();
            s.cap = stroke_.cap == CAP_ROUND ? typeset::StrokeCap::Round
                  : stroke_.cap == CAP_SQUARE ? typeset::StrokeCap::Square : typeset::StrokeCap::Butt;
            s.join = stroke_.join == JOIN_ROUND ? typeset::StrokeJoin::Round
                   : stroke_.join == JOIN_BEVEL ? typeset::StrokeJoin::Bevel : typeset::StrokeJoin::Miter;
            item.stroke = s;
        }
        out_.push_back(std::move(item));
    }

    static typeset::Path roundRect(float x, float y, float w, float h, float rx, float ry) {
        typeset::Path p;
        rx = std::min(std::fabs(rx), w * 0.5f);
        ry = std::min(std::fabs(ry), h * 0.5f);
        if (rx <= 0.0f || ry <= 0.0f) { p.addRect(typeset::Rect{x, y, w, h}); return p; }
        const float k = 0.5522847f;
        p.moveTo(x + rx, y);
        p.lineTo(x + w - rx, y);
        p.cubicTo(x + w - rx + rx * k, y, x + w, y + ry - ry * k, x + w, y + ry);
        p.lineTo(x + w, y + h - ry);
        p.cubicTo(x + w, y + h - ry + ry * k, x + w - rx + rx * k, y + h, x + w - rx, y + h);
        p.lineTo(x + rx, y + h);
        p.cubicTo(x + rx - rx * k, y + h, x, y + h - ry + ry * k, x, y + h - ry);
        p.lineTo(x, y + ry);
        p.cubicTo(x, y + ry - ry * k, x + rx - rx * k, y, x + rx, y);
        p.close();
        return p;
    }

    std::vector<typeset::dl::Item>& out_;
    typeset::Matrix base_;
    typeset::Matrix ctm_;
    float sx_ = 1.0f, sy_ = 1.0f;
    color color_ = black;
    Stroke stroke_;
    const Font* font_ = nullptr;
    typeset::dl::GlyphRun run_;
    bool open_ = false;
};

} // namespace tex

//------------------------------------------------------------------------------
// ハンドラ
//------------------------------------------------------------------------------

namespace typeset::handlers {

namespace {

bool parseHexColor(const std::string& s, tex::color& out) {
    if (s.size() != 7 || s[0] != '#') return false;
    const unsigned long v = std::strtoul(s.c_str() + 1, nullptr, 16);
    out = 0xFF000000u | static_cast<tex::color>(v);
    return true;
}

} // namespace

obj::ObjectHandler makeMicroTexHandler(font::FontSet& fonts, MicroTexOptions options) {
    {
        Context& c = ctx();
        std::lock_guard<std::mutex> lock(c.mutex);
        c.fonts = &fonts;
        c.textFamily = options.textFamily;
        c.sansFamily = options.sansFamily;
        if (!c.initialized) {
            std::string res = options.resDir;
#ifdef TYPESET_MICROTEX_RES_DIR
            if (res.empty()) res = TYPESET_MICROTEX_RES_DIR;
#endif
            try {
                tex::LaTeX::init(res);
                c.initialized = true;
            } catch (const std::exception& e) {
                c.initError = std::string("MicroTeX init failed: ") + e.what();
            }
        }
    }

    return [](const obj::ObjectRequest& req) -> obj::ObjectResult {
        Context& c = ctx();
        std::lock_guard<std::mutex> lock(c.mutex);
        obj::ObjectResult res;
        if (!c.initialized) {
            res.error = c.initError.empty() ? "MicroTeX not initialized" : c.initError;
            return res;
        }

        bool display = !req.inlineContext;
        auto st = req.params.find("style");
        if (st != req.params.end()) display = (st->second != "text");
        tex::color fg = tex::black;
        auto col = req.params.find("color");
        if (col != req.params.end()) parseHexColor(col->second, fg);

        try {
            tex::Formula formula(toWide(req.source));
            tex::TeXRenderBuilder builder;
            builder.setStyle(display ? tex::TexStyle::display : tex::TexStyle::text)
                .setTextSize(req.fontSize * kOversample)
                .setForeground(fg);
            std::unique_ptr<tex::TeXRender> render(builder.build(formula));
            const float w = static_cast<float>(render->getWidth()) / kOversample;
            const float h = static_cast<float>(render->getHeight()) / kOversample;
            const float baseline = render->getBaseline() * h;

            c.usedFaces.clear();
            std::vector<dl::Item> items;
            {
                tex::Graphics2D_ts g2(items, Matrix::scaling(1.0f / kOversample, 1.0f / kOversample));
                render->draw(g2, 0, 0);
                g2.flush();
            }
            res = obj::makeResult(Size{w, h}, baseline, std::move(items));
            res.hasBaseline = true;
            res.fonts = c.usedFaces;
        } catch (const std::exception& e) {
            res.error = std::string("MicroTeX: ") + e.what();
        }
        return res;
    };
}

void releaseMicroTex() {
    Context& c = ctx();
    std::lock_guard<std::mutex> lock(c.mutex);
    if (c.initialized) {
        tex::LaTeX::release();
        c.initialized = false;
    }
}

} // namespace typeset::handlers
