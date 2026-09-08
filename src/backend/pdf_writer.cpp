/**
 * pdf_writer.cpp — 表示リスト → PDF
 *
 * グリフ ID を直接書くので、ビューア側で再シェイピングされることがない。
 * richtext PdfWriter の移植（入力を GlyphInfo 列から表示リストへ変更）。
 */

#include "typeset/backend/pdf_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

#include <glyphware/Face.h>
#include <hb.h>
#include <hb-subset.h>
#include <zlib.h>

#include "typeset/image/image.hpp"
#include "sfnt_info.hpp"

namespace typeset::backend {

namespace {

/// PDF の実数表記（指数表記は使えない）
std::string num(float v) {
    if (!std::isfinite(v)) v = 0.0f;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(v));
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s.empty() || s == "-0") s = "0";
    return s;
}

std::string hex4(uint32_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", v & 0xFFFF);
    return std::string(buf);
}

std::string escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '(' || c == ')' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

/// PDF のテキスト文字列（UTF-16BE、BOM 付き 16 進）
std::string utf16String(const std::u16string& s) {
    std::string out = "<FEFF";
    for (char16_t c : s) out += hex4(c);
    out += ">";
    return out;
}

/// PDF 名前に使える形へ（英数字と一部記号のみ）
std::string sanitizeName(const std::string& s) {
    std::string out;
    for (char c : s) {
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            c == '-' || c == '+' || c == '_') {
            out += c;
        }
    }
    if (out.empty()) out = "Font";
    return out;
}

std::string colorOp(Color c, const char* op) {
    return num(c.r / 255.0f) + " " + num(c.g / 255.0f) + " " + num(c.b / 255.0f) + " " + op + "\n";
}

int joinCode(StrokeJoin j) { return j == StrokeJoin::Miter ? 0 : j == StrokeJoin::Round ? 1 : 2; }
int capCode(StrokeCap c) { return c == StrokeCap::Butt ? 0 : c == StrokeCap::Round ? 1 : 2; }

std::string streamObject(const std::string& dictExtra, const std::string& data) {
    std::string out = "<< /Length " + std::to_string(data.size());
    if (!dictExtra.empty()) out += " " + dictExtra;
    out += " >>\nstream\n";
    out += data;
    out += "\nendstream";
    return out;
}

/// zlib（FlateDecode 用）
std::string deflateBytes(const std::string& raw) {
    uLongf destLen = compressBound(static_cast<uLong>(raw.size()));
    std::string out(destLen, char(0));
    if (compress2(reinterpret_cast<Bytef*>(out.data()), &destLen,
                  reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), 6) != Z_OK) {
        return std::string();
    }
    out.resize(destLen);
    return out;
}

/// JPEG の SOF マーカーから成分数を取る（0 = 不明）
int jpegComponentCount(const std::vector<uint8_t>& d) {
    size_t i = 2;
    while (i + 3 < d.size()) {
        if (d[i] != 0xFF) { ++i; continue; }
        const uint8_t marker = d[i + 1];
        if (marker == 0xFF) { ++i; continue; }
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) { i += 2; continue; }
        const size_t len = (static_cast<size_t>(d[i + 2]) << 8) | d[i + 3];
        const bool sof = (marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (sof) {
            if (i + 9 < d.size()) return d[i + 9];
            break;
        }
        if (marker == 0xDA) break;   // SOS 以降にはない
        i += 2 + len;
    }
    return 0;
}

/**
 * hb-subset で使用グリフだけを残す。グリフ ID は保持（RETAIN_GIDS）するので、
 * コンテンツストリームの CID はそのまま使える。失敗時は空文字列
 */
std::string subsetFont(const uint8_t* data, size_t size, int faceIndex,
                       const std::set<uint32_t>& gids) {
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(data),
                                     static_cast<unsigned int>(size),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, static_cast<unsigned int>(faceIndex));
    hb_blob_destroy(blob);

    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    if (!input) {
        hb_face_destroy(face);
        return std::string();
    }
    hb_set_t* glyphs = hb_subset_input_glyph_set(input);
    hb_set_add(glyphs, 0);   // .notdef
    for (uint32_t g : gids) hb_set_add(glyphs, g);
    hb_subset_input_set_flags(input, HB_SUBSET_FLAGS_RETAIN_GIDS | HB_SUBSET_FLAGS_NOTDEF_OUTLINE);
    // PDF は組版済みのグリフ ID を置くだけなので、レイアウト系のテーブルは要らない
    hb_set_t* drop = hb_subset_input_set(input, HB_SUBSET_SETS_DROP_TABLE_TAG);
    const hb_tag_t dropTags[] = {
        HB_TAG('G', 'S', 'U', 'B'), HB_TAG('G', 'P', 'O', 'S'), HB_TAG('G', 'D', 'E', 'F'),
        HB_TAG('B', 'A', 'S', 'E'), HB_TAG('J', 'S', 'T', 'F'), HB_TAG('M', 'A', 'T', 'H'),
        HB_TAG('D', 'S', 'I', 'G'), HB_TAG('v', 'h', 'e', 'a'), HB_TAG('v', 'm', 't', 'x'),
        HB_TAG('V', 'O', 'R', 'G'), HB_TAG('k', 'e', 'r', 'n'),
    };
    for (hb_tag_t t : dropTags) hb_set_add(drop, t);

    hb_face_t* out = hb_subset_or_fail(face, input);
    hb_subset_input_destroy(input);
    hb_face_destroy(face);
    if (!out) return std::string();

    hb_blob_t* ob = hb_face_reference_blob(out);
    unsigned int len = 0;
    const char* d = hb_blob_get_data(ob, &len);
    std::string result(d, d + len);
    hb_blob_destroy(ob);
    hb_face_destroy(out);
    return result;
}

/// サブセットフォント名の接頭辞（6 文字の大文字）。使用グリフ集合から決める
std::string subsetTag(const std::set<uint32_t>& gids) {
    uint32_t h = 2166136261u;
    for (uint32_t g : gids) {
        h ^= g;
        h *= 16777619u;
    }
    std::string tag(6, 'A');
    for (int i = 0; i < 6; ++i) {
        tag[i] = static_cast<char>('A' + (h % 26));
        h /= 26;
        if (h == 0) h = 7919u * static_cast<uint32_t>(i + 1);
    }
    return tag;
}

} // namespace

//------------------------------------------------------------------------------

struct PdfWriter::Impl {
    struct FontResource {
        std::shared_ptr<glyphware::Face> face;
        std::string resourceName;          ///< /F1 など
        std::string baseFont;
        SfntInfo sfnt;
        const uint8_t* data = nullptr;
        size_t dataSize = 0;
        std::set<uint32_t> usedGlyphs;
        std::map<uint32_t, uint32_t> toUnicode;   ///< GID → コードポイント
        bool embeddable = false;
    };

    struct Page {
        float width = 0;
        float height = 0;
        std::string content;
    };

    struct ImageResource {
        std::shared_ptr<const dl::Image> image;
        std::string name;              ///< /Im1 など
    };

    struct OutlineEntry {
        std::u16string title;
        int level = 1;
        size_t pageIndex = 0;
        float x = 0.0f, y = 0.0f;      ///< PDF 座標（y-up）
    };

    std::string title;
    std::string author;
    std::string creator = "typeset";
    bool embedToUnicode = true;
    bool subsetFonts = true;
    bool compressStreams = true;

    std::vector<Page> pages;
    std::vector<std::unique_ptr<FontResource>> fonts;
    std::unordered_map<const glyphware::Face*, FontResource*> fontMap;
    std::map<int, std::string> alphaStates;   ///< 透明度(0..255) → ExtGState 名
    std::vector<std::string> warnings;
    std::vector<ImageResource> images;
    std::unordered_map<const dl::Image*, size_t> imageMap;
    std::vector<OutlineEntry> outline;

    // --- 現在のページの描画状態 ---
    std::string curFontName;
    float curFontSize = -1.0f;
    int curTextRender = -1;
    std::string curFillColor;
    std::string curStrokeColor;
    float curLineWidth = -1.0f;
    std::string curAlphaState;

    Page& page() { return pages.back(); }

    void resetState() {
        curFontName.clear();
        curFontSize = -1.0f;
        curTextRender = -1;
        curFillColor.clear();
        curStrokeColor.clear();
        curLineWidth = -1.0f;
        curAlphaState.clear();
    }

    FontResource* acquireFont(const std::shared_ptr<glyphware::Face>& face);
    std::string alphaStateName(float alpha);

    void ensureFillColor(Color c);
    void ensureStrokeColor(Color c);
    void ensureLineWidth(float w);
    void ensureAlpha(float alpha);

    void drawItems(const std::vector<dl::Item>& items, const Matrix& ctm, float opacity);
    void drawGlyphRun(const dl::GlyphRun& run, const Matrix& ctm, float opacity);
    void drawPath(const Path& path, const Matrix& ctm, const std::optional<Color>& fill,
                  const std::optional<Stroke>& stroke, bool evenOdd, float opacity);
    void drawImage(const dl::ImageItem& item, const Matrix& ctm, float opacity);

    void emitGlyph(FontResource* font, uint32_t gid, float fontSize, const Mat2& m,
                   Point pen, int textRender, float strokeWidth, Color color, float opacity);
    std::string pathOps(const Path& path, const Matrix& ctm) const;
};

//------------------------------------------------------------------------------

PdfWriter::PdfWriter() : impl_(std::make_unique<Impl>()) {}
PdfWriter::~PdfWriter() = default;

void PdfWriter::setTitle(std::string title) { impl_->title = std::move(title); }
void PdfWriter::setAuthor(std::string author) { impl_->author = std::move(author); }
void PdfWriter::setCreator(std::string creator) { impl_->creator = std::move(creator); }
void PdfWriter::setEmbedToUnicode(bool embed) { impl_->embedToUnicode = embed; }
void PdfWriter::setSubsetFonts(bool subset) { impl_->subsetFonts = subset; }
void PdfWriter::setCompressStreams(bool compress) { impl_->compressStreams = compress; }
size_t PdfWriter::pageCount() const { return impl_->pages.size(); }
const std::vector<std::string>& PdfWriter::warnings() const { return impl_->warnings; }

//------------------------------------------------------------------------------
// フォント
//------------------------------------------------------------------------------

PdfWriter::Impl::FontResource* PdfWriter::Impl::acquireFont(
        const std::shared_ptr<glyphware::Face>& face) {
    auto it = fontMap.find(face.get());
    if (it != fontMap.end()) return it->second;

    auto res = std::make_unique<FontResource>();
    res->face = face;
    res->resourceName = "F" + std::to_string(fonts.size() + 1);
    res->data = face->data();
    res->dataSize = face->size();

    const glyphware::FontDescriptor& desc = face->descriptor();
    std::string base = desc.postScriptName;
    if (base.empty()) {
        base = desc.family;
        if (!desc.subfamily.empty() && desc.subfamily != "Regular") base += "-" + desc.subfamily;
    }
    if (base.empty()) base = desc.key;
    res->baseFont = sanitizeName(base);

    if (res->data && res->dataSize > 0 && parseSfnt(res->data, res->dataSize, res->sfnt)) {
        res->embeddable = true;
    } else {
        res->embeddable = false;
        warnings.push_back("font not embeddable: " + res->baseFont +
                           (res->sfnt.isCollection ? " (TTC is not supported)"
                                                   : " (no usable sfnt data)"));
    }

    FontResource* raw = res.get();
    fonts.push_back(std::move(res));
    fontMap.emplace(face.get(), raw);
    return raw;
}

std::string PdfWriter::Impl::alphaStateName(float alpha) {
    const int key = static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    auto it = alphaStates.find(key);
    if (it != alphaStates.end()) return it->second;
    const std::string name = "GS" + std::to_string(alphaStates.size() + 1);
    alphaStates.emplace(key, name);
    return name;
}

//------------------------------------------------------------------------------
// 描画状態
//------------------------------------------------------------------------------

void PdfWriter::Impl::ensureFillColor(Color c) {
    const std::string op = colorOp(c, "rg");
    if (op == curFillColor) return;
    page().content += op;
    curFillColor = op;
}

void PdfWriter::Impl::ensureStrokeColor(Color c) {
    const std::string op = colorOp(c, "RG");
    if (op == curStrokeColor) return;
    page().content += op;
    curStrokeColor = op;
}

void PdfWriter::Impl::ensureLineWidth(float w) {
    if (std::fabs(w - curLineWidth) < 1e-4f) return;
    page().content += num(w) + " w\n";
    curLineWidth = w;
}

void PdfWriter::Impl::ensureAlpha(float alpha) {
    const std::string name = alphaStateName(alpha >= 0.999f ? 1.0f : alpha);
    if (name == curAlphaState) return;
    page().content += "/" + name + " gs\n";
    curAlphaState = name;
}

//------------------------------------------------------------------------------
// グリフ
//------------------------------------------------------------------------------

void PdfWriter::Impl::emitGlyph(FontResource* font, uint32_t gid, float fontSize,
                                const Mat2& m, Point pen, int textRender,
                                float strokeWidth, Color color, float opacity) {
    if (!font->embeddable) return;
    font->usedGlyphs.insert(gid);

    ensureAlpha(color.a / 255.0f * opacity);
    if (textRender != 0) {
        ensureStrokeColor(color);
        ensureLineWidth(strokeWidth);
    }
    ensureFillColor(color);

    std::string& out = page().content;
    out += "BT\n";
    if (font->resourceName != curFontName || std::fabs(fontSize - curFontSize) > 1e-4f) {
        out += "/" + font->resourceName + " " + num(fontSize) + " Tf\n";
        curFontName = font->resourceName;
        curFontSize = fontSize;
    }
    if (textRender != curTextRender) {
        out += std::to_string(textRender) + " Tr\n";
        curTextRender = textRender;
    }

    // 画面（y-down）の 2x2 を PDF（y-up）へ移す。Y 反転で共役を取るので
    // シアー成分の符号が入れ替わる。フォントサイズは Tf が掛けるので
    // Tm には入れない（入れると二重に効く）。
    const float a = m.xx;
    const float b = -m.yx;
    const float c = -m.xy;
    const float d = m.yy;
    out += num(a) + " " + num(b) + " " + num(c) + " " + num(d) + " " +
           num(pen.x) + " " + num(page().height - pen.y) + " Tm\n";
    out += "<" + hex4(gid) + "> Tj\n";
    out += "ET\n";
}

void PdfWriter::Impl::drawGlyphRun(const dl::GlyphRun& run, const Matrix& ctm, float opacity) {
    if (!run.face || run.glyphs.empty()) return;
    FontResource* font = acquireFont(run.face);
    if (!font->embeddable) return;

    // ctm の一様スケール（フォントサイズと線幅に掛ける）。回転・シアーは Tm の 2x2 へ
    const float devScale = std::sqrt(std::fabs(ctm.determinant()));
    const float fontSize = run.size * devScale;
    Mat2 lin = ctm.linear();
    if (devScale > 0.0f) {
        lin.xx /= devScale; lin.xy /= devScale; lin.yx /= devScale; lin.yy /= devScale;
    }

    for (const dl::Glyph& g : run.glyphs) {
        const Point pen = ctm.apply(g.pos);
        const Mat2 m = multiply(lin, g.xform);

        if (embedToUnicode && run.text && g.charIndex < run.text->size()) {
            const char16_t c = (*run.text)[g.charIndex];
            uint32_t cp = c;
            if (c >= 0xD800 && c <= 0xDBFF && g.charIndex + 1 < run.text->size()) {
                const char16_t c2 = (*run.text)[g.charIndex + 1];
                if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
                    cp = 0x10000 + ((static_cast<uint32_t>(c - 0xD800)) << 10) + (c2 - 0xDC00);
                }
            }
            font->toUnicode.emplace(g.gid, cp);
        }

        if (run.fill && run.fill->a > 0) {
            // フェイクボールドは同色の縁取りを重ねて太らせる（2 Tr）
            const float embolden = run.embolden * devScale;
            emitGlyph(font, g.gid, fontSize, m, pen, embolden > 0.0f ? 2 : 0, embolden,
                      *run.fill, opacity);
        }
        if (run.stroke && run.stroke->color.a > 0 && run.stroke->width > 0.0f) {
            emitGlyph(font, g.gid, fontSize, m, pen, 1,
                      (run.stroke->width + run.embolden) * devScale, run.stroke->color, opacity);
        }
    }
}

//------------------------------------------------------------------------------
// パス
//------------------------------------------------------------------------------

std::string PdfWriter::Impl::pathOps(const Path& path, const Matrix& ctm) const {
    const float H = pages.back().height;
    auto pt = [&](Point p) {
        const Point q = ctm.apply(p);
        return num(q.x) + " " + num(H - q.y);
    };
    std::string out;
    size_t pi = 0;
    Point last{};
    for (Path::Cmd c : path.cmds) {
        switch (c) {
        case Path::Cmd::Move:
            last = path.pts[pi++];
            out += pt(last) + " m\n";
            break;
        case Path::Cmd::Line:
            last = path.pts[pi++];
            out += pt(last) + " l\n";
            break;
        case Path::Cmd::Quad: {
            // PDF に 2 次ベジェは無いので 3 次へ昇格
            const Point q = path.pts[pi++];
            const Point p = path.pts[pi++];
            const Point c1{last.x + 2.0f / 3.0f * (q.x - last.x), last.y + 2.0f / 3.0f * (q.y - last.y)};
            const Point c2{p.x + 2.0f / 3.0f * (q.x - p.x), p.y + 2.0f / 3.0f * (q.y - p.y)};
            out += pt(c1) + " " + pt(c2) + " " + pt(p) + " c\n";
            last = p;
            break;
        }
        case Path::Cmd::Cubic: {
            const Point c1 = path.pts[pi++];
            const Point c2 = path.pts[pi++];
            const Point p = path.pts[pi++];
            out += pt(c1) + " " + pt(c2) + " " + pt(p) + " c\n";
            last = p;
            break;
        }
        case Path::Cmd::Close:
            out += "h\n";
            break;
        }
    }
    return out;
}

void PdfWriter::Impl::drawPath(const Path& path, const Matrix& ctm,
                               const std::optional<Color>& fill,
                               const std::optional<Stroke>& stroke, bool evenOdd,
                               float opacity) {
    if (path.empty()) return;
    const bool doFill = fill && fill->a > 0;
    const bool doStroke = stroke && stroke->color.a > 0 && stroke->width > 0.0f;
    if (!doFill && !doStroke) return;

    std::string& out = page().content;
    out += "q\n";
    // 透明度は塗りと線で別々に出せないので、塗りの方を優先して 1 つの gs にする
    const float alpha = (doFill ? fill->a : stroke->color.a) / 255.0f * opacity;
    if (alpha < 0.999f) out += "/" + alphaStateName(alpha) + " gs\n";
    if (doFill) out += colorOp(*fill, "rg");
    if (doStroke) {
        const float devScale = std::sqrt(std::fabs(ctm.determinant()));
        out += colorOp(stroke->color, "RG");
        out += num(stroke->width * devScale) + " w\n";
        out += std::to_string(joinCode(stroke->join)) + " j\n";
        out += std::to_string(capCode(stroke->cap)) + " J\n";
        if (stroke->join == StrokeJoin::Miter) out += num(stroke->miterLimit) + " M\n";
    }
    out += pathOps(path, ctm);
    if (doFill && doStroke) out += evenOdd ? "B*\n" : "B\n";
    else if (doFill)        out += evenOdd ? "f*\n" : "f\n";
    else                    out += "S\n";
    out += "Q\n";
    resetState();
}

//------------------------------------------------------------------------------
// 画像
//------------------------------------------------------------------------------

void PdfWriter::Impl::drawImage(const dl::ImageItem& item, const Matrix& ctm, float opacity) {
    if (!item.image || item.image->width <= 0 || item.image->height <= 0) return;
    const dl::Image* key = item.image.get();
    auto it = imageMap.find(key);
    if (it == imageMap.end()) {
        ImageResource res;
        res.image = item.image;
        res.name = "Im" + std::to_string(images.size() + 1);
        images.push_back(std::move(res));
        it = imageMap.emplace(key, images.size() - 1).first;
    }
    const ImageResource& res = images[it->second];

    // 単位正方形（y-up）→ 画素（行は上から）→ ページ（y-down）→ PDF（y-up）
    Matrix unitToPixel;
    unitToPixel.xx = static_cast<float>(item.image->width);
    unitToPixel.yy = -static_cast<float>(item.image->height);
    unitToPixel.dy = static_cast<float>(item.image->height);
    Matrix flip;
    flip.yy = -1.0f;
    flip.dy = page().height;
    const Matrix m = multiply(flip, multiply(ctm, multiply(item.xform, unitToPixel)));

    std::string& out = page().content;
    out += "q\n";
    const float alpha = item.opacity * opacity;
    if (alpha < 0.999f) out += "/" + alphaStateName(alpha) + " gs\n";
    out += num(m.xx) + " " + num(m.yx) + " " + num(m.xy) + " " + num(m.yy) + " " +
           num(m.dx) + " " + num(m.dy) + " cm\n";
    out += "/" + res.name + " Do\n";
    out += "Q\n";
    resetState();
}

//------------------------------------------------------------------------------

void PdfWriter::Impl::drawItems(const std::vector<dl::Item>& items, const Matrix& ctm,
                                float opacity) {
    for (const dl::Item& item : items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            drawGlyphRun(*run, ctm, opacity);
        } else if (const auto* p = std::get_if<dl::PathItem>(&item)) {
            drawPath(p->path, ctm, p->fill, p->stroke, p->evenOdd, opacity);
        } else if (const auto* r = std::get_if<dl::RectItem>(&item)) {
            Path path;
            path.addRect(r->rect);
            drawPath(path, ctm, r->fill, std::nullopt, false, opacity);
        } else if (const auto* img = std::get_if<dl::ImageItem>(&item)) {
            drawImage(*img, ctm, opacity);
        } else if (const auto* bm = std::get_if<dl::Bookmark>(&item)) {
            OutlineEntry e;
            e.title = bm->title;
            e.level = std::max(1, bm->level);
            e.pageIndex = pages.size() - 1;
            const Point p = ctm.apply(bm->pos);
            e.x = p.x;
            e.y = page().height - p.y;
            outline.push_back(std::move(e));
        } else if (const auto* group = std::get_if<dl::Group>(&item)) {
            std::string& out = page().content;
            out += "q\n";
            if (group->clip) {
                const Rect& c = *group->clip;
                const Point p[4] = {ctm.apply({c.x, c.y}), ctm.apply({c.right(), c.y}),
                                    ctm.apply({c.right(), c.bottom()}), ctm.apply({c.x, c.bottom()})};
                float x0 = p[0].x, y0 = p[0].y, x1 = x0, y1 = y0;
                for (const Point& q : p) {
                    x0 = std::min(x0, q.x); y0 = std::min(y0, q.y);
                    x1 = std::max(x1, q.x); y1 = std::max(y1, q.y);
                }
                const float H = page().height;
                out += num(x0) + " " + num(H - y1) + " " + num(x1 - x0) + " " + num(y1 - y0) +
                       " re W n\n";
            }
            resetState();
            drawItems(group->children, multiply(ctm, group->xform), opacity * group->opacity);
            out += "Q\n";
            resetState();
        }
    }
}

void PdfWriter::addPage(const dl::DisplayList& list) {
    Impl::Page p;
    p.width = list.page.w;
    p.height = list.page.h;
    impl_->pages.push_back(std::move(p));
    impl_->resetState();
    impl_->drawItems(list.items, Matrix::identity(), 1.0f);
}

//------------------------------------------------------------------------------
// 書き出し
//------------------------------------------------------------------------------

namespace {

/// 使用グリフの幅配列 /W
std::string buildWidthArray(const glyphware::Face& face, const std::set<uint32_t>& glyphs,
                            float unitsPerEm) {
    if (glyphs.empty()) return std::string();
    std::string out = "[";
    auto it = glyphs.begin();
    while (it != glyphs.end()) {
        const uint32_t start = *it;
        std::vector<int> run;
        uint32_t expected = start;
        while (it != glyphs.end() && *it == expected) {
            glyphware::GlyphMetrics m;
            int w = 1000;
            if (face.glyphMetricsUnscaled(*it, m)) {
                w = static_cast<int>(m.advanceX * 1000.0f / unitsPerEm + 0.5f);
            }
            run.push_back(w);
            ++it;
            ++expected;
        }
        out += " " + std::to_string(start) + " [";
        for (size_t i = 0; i < run.size(); ++i) {
            if (i) out += " ";
            out += std::to_string(run[i]);
        }
        out += "]";
    }
    out += " ]";
    return out;
}

std::string buildToUnicodeCMap(const std::map<uint32_t, uint32_t>& map) {
    std::string body =
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\n"
        "begincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Adobe-Identity-UCS def\n"
        "/CMapType 2 def\n"
        "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";

    std::vector<std::pair<uint32_t, uint32_t>> entries(map.begin(), map.end());
    for (size_t i = 0; i < entries.size(); i += 100) {
        const size_t n = std::min<size_t>(100, entries.size() - i);
        body += std::to_string(n) + " beginbfchar\n";
        for (size_t k = 0; k < n; ++k) {
            const uint32_t gid = entries[i + k].first;
            const uint32_t cp = entries[i + k].second;
            body += "<" + hex4(gid) + "> <";
            if (cp <= 0xFFFF) {
                body += hex4(cp);
            } else {
                const uint32_t v = cp - 0x10000;
                body += hex4(0xD800 | (v >> 10));
                body += hex4(0xDC00 | (v & 0x3FF));
            }
            body += ">\n";
        }
        body += "endbfchar\n";
    }
    body += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    return body;
}

} // namespace

std::string PdfWriter::build() {
    std::vector<std::string> objects;   // 1-based（objects[0] が 1 0 obj）
    const auto addObject = [&](std::string body) -> int {
        objects.push_back(std::move(body));
        return static_cast<int>(objects.size());
    };

    const int catalogId = addObject("");
    const int pagesId = addObject("");

    // 圧縮するときは /Filter /FlateDecode を付ける（画像は個別に扱う）
    const auto stream = [&](const std::string& dictExtra, const std::string& data) -> std::string {
        if (!impl_->compressStreams || data.size() < 64) return streamObject(dictExtra, data);
        const std::string z = deflateBytes(data);
        if (z.empty() || z.size() >= data.size()) return streamObject(dictExtra, data);
        return streamObject(dictExtra + (dictExtra.empty() ? "" : " ") + "/Filter /FlateDecode", z);
    };

    // --- フォント ---
    std::string fontDictEntries;
    for (const auto& fontPtr : impl_->fonts) {
        Impl::FontResource& font = *fontPtr;
        if (!font.embeddable || font.usedGlyphs.empty()) continue;

        const float upem = static_cast<float>(font.sfnt.unitsPerEm);
        const float toPdf = 1000.0f / upem;

        std::string fontData;
        std::string baseFont = font.baseFont;
        if (impl_->subsetFonts) {
            fontData = subsetFont(font.data, font.dataSize, font.face->faceIndex(), font.usedGlyphs);
            if (fontData.empty()) {
                impl_->warnings.push_back("subsetting failed, embedding the full font: " + font.baseFont);
            } else {
                baseFont = subsetTag(font.usedGlyphs) + "+" + font.baseFont;
            }
        }
        if (fontData.empty()) {
            fontData.assign(reinterpret_cast<const char*>(font.data), font.dataSize);
        }
        const size_t fontLength = fontData.size();
        int fontFileId;
        if (font.sfnt.isCFF) {
            fontFileId = addObject(stream("/Subtype /OpenType", fontData));
        } else {
            fontFileId = addObject(stream("/Length1 " + std::to_string(fontLength), fontData));
        }

        int flags = 4;   // Symbolic
        if (font.sfnt.isSerif) flags |= 2;
        if (font.sfnt.isFixedPitch) flags |= 1;

        std::string descriptor =
            "<< /Type /FontDescriptor /FontName /" + baseFont +
            " /Flags " + std::to_string(flags) +
            " /FontBBox [" + std::to_string(static_cast<int>(font.sfnt.xMin * toPdf)) + " " +
            std::to_string(static_cast<int>(font.sfnt.yMin * toPdf)) + " " +
            std::to_string(static_cast<int>(font.sfnt.xMax * toPdf)) + " " +
            std::to_string(static_cast<int>(font.sfnt.yMax * toPdf)) + "]" +
            " /ItalicAngle " + num(font.sfnt.italicAngle) +
            " /Ascent " + std::to_string(static_cast<int>(font.sfnt.ascender * toPdf)) +
            " /Descent " + std::to_string(static_cast<int>(font.sfnt.descender * toPdf)) +
            " /CapHeight " +
            std::to_string(static_cast<int>(
                (font.sfnt.capHeight ? font.sfnt.capHeight : font.sfnt.ascender) * toPdf)) +
            " /StemV 80" +
            (font.sfnt.isCFF ? " /FontFile3 " : " /FontFile2 ") +
            std::to_string(fontFileId) + " 0 R >>";
        const int descriptorId = addObject(std::move(descriptor));

        const std::string widths = buildWidthArray(*font.face, font.usedGlyphs, upem);
        std::string cidFont =
            "<< /Type /Font /Subtype /" +
            std::string(font.sfnt.isCFF ? "CIDFontType0" : "CIDFontType2") +
            " /BaseFont /" + baseFont +
            " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>"
            " /FontDescriptor " + std::to_string(descriptorId) + " 0 R"
            " /DW 1000";
        if (!widths.empty()) cidFont += " /W " + widths;
        if (!font.sfnt.isCFF) cidFont += " /CIDToGIDMap /Identity";
        cidFont += " >>";
        const int cidFontId = addObject(std::move(cidFont));

        int toUnicodeId = 0;
        if (impl_->embedToUnicode && !font.toUnicode.empty()) {
            toUnicodeId = addObject(stream("", buildToUnicodeCMap(font.toUnicode)));
        }

        std::string type0 =
            "<< /Type /Font /Subtype /Type0 /BaseFont /" + baseFont +
            " /Encoding /Identity-H /DescendantFonts [" + std::to_string(cidFontId) + " 0 R]";
        if (toUnicodeId) type0 += " /ToUnicode " + std::to_string(toUnicodeId) + " 0 R";
        type0 += " >>";
        const int type0Id = addObject(std::move(type0));

        fontDictEntries += " /" + font.resourceName + " " + std::to_string(type0Id) + " 0 R";
    }

    // --- 画像 XObject ---
    std::string xobjEntries;
    for (const Impl::ImageResource& res : impl_->images) {
        const dl::Image& img = *res.image;
        std::string dict = "/Type /XObject /Subtype /Image /Width " + std::to_string(img.width) +
                           " /Height " + std::to_string(img.height) + " /BitsPerComponent 8";
        std::string data;
        int jpegComponents = 0;
        if (img.encoding == dl::Image::Encoding::Jpeg) jpegComponents = jpegComponentCount(img.encoded);
        if (jpegComponents == 1 || jpegComponents == 3) {
            // JPEG はそのまま埋め込む
            dict += (jpegComponents == 3) ? " /ColorSpace /DeviceRGB" : " /ColorSpace /DeviceGray";
            dict += " /Filter /DCTDecode";
            data.assign(img.encoded.begin(), img.encoded.end());
        } else {
            std::string rgb;
            rgb.reserve(static_cast<size_t>(img.width) * img.height * 3);
            for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
                rgb.push_back(static_cast<char>(img.rgba[i]));
                rgb.push_back(static_cast<char>(img.rgba[i + 1]));
                rgb.push_back(static_cast<char>(img.rgba[i + 2]));
            }
            dict += " /ColorSpace /DeviceRGB /Filter /FlateDecode";
            data = deflateBytes(rgb);
            if (image::hasAlpha(img)) {
                std::string a;
                a.reserve(static_cast<size_t>(img.width) * img.height);
                for (size_t i = 3; i < img.rgba.size(); i += 4) a.push_back(static_cast<char>(img.rgba[i]));
                const int smaskId = addObject(streamObject(
                    "/Type /XObject /Subtype /Image /Width " + std::to_string(img.width) +
                    " /Height " + std::to_string(img.height) +
                    " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode", deflateBytes(a)));
                dict += " /SMask " + std::to_string(smaskId) + " 0 R";
            }
        }
        const int id = addObject(streamObject(dict, data));
        xobjEntries += " /" + res.name + " " + std::to_string(id) + " 0 R";
    }

    // --- ExtGState（透明度）---
    std::string gsDictEntries;
    for (const auto& entry : impl_->alphaStates) {
        const float alpha = static_cast<float>(entry.first) / 255.0f;
        const int id = addObject("<< /Type /ExtGState /ca " + num(alpha) + " /CA " + num(alpha) + " >>");
        gsDictEntries += " /" + entry.second + " " + std::to_string(id) + " 0 R";
    }

    std::string resources = "<< /ProcSet [/PDF /Text /ImageC]";
    if (!fontDictEntries.empty()) resources += " /Font <<" + fontDictEntries + " >>";
    if (!gsDictEntries.empty()) resources += " /ExtGState <<" + gsDictEntries + " >>";
    if (!xobjEntries.empty()) resources += " /XObject <<" + xobjEntries + " >>";
    resources += " >>";
    const int resourcesId = addObject(resources);

    // --- ページ ---
    std::vector<int> pageIds;
    for (const auto& p : impl_->pages) {
        const int contentId = addObject(stream("", p.content));
        const int pageId = addObject(
            "<< /Type /Page /Parent " + std::to_string(pagesId) + " 0 R"
            " /MediaBox [0 0 " + num(p.width) + " " + num(p.height) + "]"
            " /Resources " + std::to_string(resourcesId) + " 0 R"
            " /Contents " + std::to_string(contentId) + " 0 R >>");
        pageIds.push_back(pageId);
    }

    std::string kids = "[";
    for (size_t i = 0; i < pageIds.size(); ++i) {
        if (i) kids += " ";
        kids += std::to_string(pageIds[i]) + " 0 R";
    }
    kids += "]";
    objects[pagesId - 1] = "<< /Type /Pages /Kids " + kids + " /Count " +
                           std::to_string(pageIds.size()) + " >>";

    // --- しおり（アウトライン）: レベルで木にする ---
    std::string outlineRef;
    if (!impl_->outline.empty()) {
        const auto& ol = impl_->outline;
        const size_t n = ol.size();
        const int rootId = addObject("");
        std::vector<int> ids(n);
        for (size_t i = 0; i < n; ++i) ids[i] = addObject("");
        std::vector<int> parent(n, -1);          // -1 = root
        std::vector<std::vector<size_t>> children(n + 1);   // children[n] = root の子
        std::vector<int> stack;                  // 直近の各レベルのエントリ
        for (size_t i = 0; i < n; ++i) {
            while (!stack.empty() && ol[stack.back()].level >= ol[i].level) stack.pop_back();
            parent[i] = stack.empty() ? -1 : stack.back();
            children[parent[i] < 0 ? n : static_cast<size_t>(parent[i])].push_back(i);
            stack.push_back(static_cast<int>(i));
        }
        // 子孫の数（開いた状態で正の Count）
        std::vector<int> descendants(n, 0);
        for (size_t i = n; i-- > 0;) {
            int c = 0;
            for (size_t ch : children[i]) c += 1 + descendants[ch];
            descendants[i] = c;
        }
        auto refOf = [&](int idx) { return std::to_string(idx < 0 ? rootId : ids[idx]) + " 0 R"; };
        for (size_t i = 0; i < n; ++i) {
            const std::vector<size_t>& sib = children[parent[i] < 0 ? n : static_cast<size_t>(parent[i])];
            const auto it = std::find(sib.begin(), sib.end(), i);
            std::string o = "<< /Title " + utf16String(ol[i].title) + " /Parent " + refOf(parent[i]);
            if (it != sib.begin()) o += " /Prev " + std::to_string(ids[*(it - 1)]) + " 0 R";
            if (it + 1 != sib.end()) o += " /Next " + std::to_string(ids[*(it + 1)]) + " 0 R";
            if (!children[i].empty()) {
                o += " /First " + std::to_string(ids[children[i].front()]) + " 0 R";
                o += " /Last " + std::to_string(ids[children[i].back()]) + " 0 R";
                o += " /Count " + std::to_string(descendants[i]);
            }
            if (ol[i].pageIndex < pageIds.size()) {
                o += " /Dest [" + std::to_string(pageIds[ol[i].pageIndex]) + " 0 R /XYZ " +
                     num(ol[i].x) + " " + num(ol[i].y) + " null]";
            }
            o += " >>";
            objects[ids[i] - 1] = o;
        }
        int total = 0;
        for (size_t ch : children[n]) total += 1 + descendants[ch];
        std::string rootObj = "<< /Type /Outlines";
        if (!children[n].empty()) {
            rootObj += " /First " + std::to_string(ids[children[n].front()]) + " 0 R";
            rootObj += " /Last " + std::to_string(ids[children[n].back()]) + " 0 R";
            rootObj += " /Count " + std::to_string(total);
        }
        rootObj += " >>";
        objects[rootId - 1] = rootObj;
        outlineRef = " /Outlines " + std::to_string(rootId) + " 0 R /PageMode /UseOutlines";
    }

    objects[catalogId - 1] = "<< /Type /Catalog /Pages " + std::to_string(pagesId) + " 0 R" +
                             outlineRef + " >>";

    // --- 文書情報 ---
    int infoId = 0;
    {
        std::string info = "<<";
        if (!impl_->title.empty()) info += " /Title (" + escapeString(impl_->title) + ")";
        if (!impl_->author.empty()) info += " /Author (" + escapeString(impl_->author) + ")";
        if (!impl_->creator.empty()) info += " /Creator (" + escapeString(impl_->creator) + ")";
        info += " /Producer (typeset) >>";
        infoId = addObject(std::move(info));
    }

    // --- 直列化 ---
    std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offsets(objects.size() + 1, 0);
    for (size_t i = 0; i < objects.size(); ++i) {
        offsets[i + 1] = out.size();
        out += std::to_string(i + 1) + " 0 obj\n";
        out += objects[i];
        out += "\nendobj\n";
    }
    const size_t xrefOffset = out.size();
    out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (size_t i = 1; i <= objects.size(); ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010zu 00000 n \n", offsets[i]);
        out += buf;
    }
    out += "trailer\n<< /Size " + std::to_string(objects.size() + 1) +
           " /Root " + std::to_string(catalogId) + " 0 R";
    if (infoId) out += " /Info " + std::to_string(infoId) + " 0 R";
    out += " >>\nstartxref\n" + std::to_string(xrefOffset) + "\n%%EOF\n";
    return out;
}

bool PdfWriter::save(const std::string& path) {
    const std::string data = build();
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(file);
}

} // namespace typeset::backend
