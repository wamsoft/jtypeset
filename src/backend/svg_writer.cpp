/**
 * svg_writer.cpp — 表示リスト → SVG
 *
 * グリフはアウトラインを <path> にする。同じ face・gid は <defs> に 1 回だけ置き
 * <use> で参照する。
 */

#include "typeset/backend/svg_writer.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <unordered_map>

#include <glyphware/Face.h>

#include "typeset/font/font_set.hpp"
#include "typeset/image/image.hpp"

namespace typeset::backend {

namespace {

std::string fmt(float v, int precision) {
    if (!std::isfinite(v)) v = 0.0f;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, static_cast<double>(v));
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s.empty() || s == "-0") s = "0";
    return s;
}

std::string hexColor(Color c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    return std::string(buf);
}

std::string escapeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

std::string utf16ToUtf8(const std::u16string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size()) {
            const uint32_t lo = s[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

/// SVG の matrix(a b c d e f): x' = a x + c y + e, y' = b x + d y + f
std::string matrixAttr(const Matrix& m, int precision) {
    return "matrix(" + fmt(m.xx, precision) + " " + fmt(m.yx, precision) + " " +
           fmt(m.xy, precision) + " " + fmt(m.yy, precision) + " " +
           fmt(m.dx, precision) + " " + fmt(m.dy, precision) + ")";
}

/// アウトライン → path d 属性（フォントユニット、y を反転して y-down にする）
class PathSink : public glyphware::OutlineSink {
public:
    std::string d;
    void moveTo(float x, float y) override { d += "M" + fmt(x, 1) + " " + fmt(-y, 1); }
    void lineTo(float x, float y) override { d += "L" + fmt(x, 1) + " " + fmt(-y, 1); }
    void quadTo(float cx, float cy, float x, float y) override {
        d += "Q" + fmt(cx, 1) + " " + fmt(-cy, 1) + " " + fmt(x, 1) + " " + fmt(-y, 1);
    }
    void cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override {
        d += "C" + fmt(c1x, 1) + " " + fmt(-c1y, 1) + " " + fmt(c2x, 1) + " " + fmt(-c2y, 1) +
             " " + fmt(x, 1) + " " + fmt(-y, 1);
    }
    void close() override { d += "Z"; }
};

std::string pathData(const Path& path, int precision) {
    std::string d;
    size_t pi = 0;
    for (Path::Cmd c : path.cmds) {
        switch (c) {
        case Path::Cmd::Move: {
            const Point p = path.pts[pi++];
            d += "M" + fmt(p.x, precision) + " " + fmt(p.y, precision);
            break;
        }
        case Path::Cmd::Line: {
            const Point p = path.pts[pi++];
            d += "L" + fmt(p.x, precision) + " " + fmt(p.y, precision);
            break;
        }
        case Path::Cmd::Quad: {
            const Point q = path.pts[pi++];
            const Point p = path.pts[pi++];
            d += "Q" + fmt(q.x, precision) + " " + fmt(q.y, precision) + " " +
                 fmt(p.x, precision) + " " + fmt(p.y, precision);
            break;
        }
        case Path::Cmd::Cubic: {
            const Point c1 = path.pts[pi++];
            const Point c2 = path.pts[pi++];
            const Point p = path.pts[pi++];
            d += "C" + fmt(c1.x, precision) + " " + fmt(c1.y, precision) + " " +
                 fmt(c2.x, precision) + " " + fmt(c2.y, precision) + " " +
                 fmt(p.x, precision) + " " + fmt(p.y, precision);
            break;
        }
        case Path::Cmd::Close:
            d += "Z";
            break;
        }
    }
    return d;
}

std::string paintAttrs(const std::optional<Color>& fill, const std::optional<Stroke>& stroke,
                       float strokeScale, float opacity, int precision) {
    std::string a;
    if (fill && fill->a > 0) {
        a += " fill=\"" + hexColor(*fill) + "\"";
        const float fa = fill->a / 255.0f * opacity;
        if (fa < 0.999f) a += " fill-opacity=\"" + fmt(fa, 3) + "\"";
    } else {
        a += " fill=\"none\"";
    }
    if (stroke && stroke->color.a > 0 && stroke->width > 0.0f) {
        a += " stroke=\"" + hexColor(stroke->color) + "\"";
        a += " stroke-width=\"" + fmt(stroke->width * strokeScale, precision) + "\"";
        const float sa = stroke->color.a / 255.0f * opacity;
        if (sa < 0.999f) a += " stroke-opacity=\"" + fmt(sa, 3) + "\"";
        if (stroke->join == StrokeJoin::Round) a += " stroke-linejoin=\"round\"";
        else if (stroke->join == StrokeJoin::Bevel) a += " stroke-linejoin=\"bevel\"";
        if (stroke->cap == StrokeCap::Round) a += " stroke-linecap=\"round\"";
        else if (stroke->cap == StrokeCap::Square) a += " stroke-linecap=\"square\"";
    }
    return a;
}

struct Writer {
    const SvgOptions& opts;
    std::string body;
    std::string defs;
    std::unordered_map<const glyphware::Face*, int> faceIds;
    std::map<std::pair<int, uint32_t>, std::string> glyphIds;   // (face, gid) → id
    int clipCounter = 0;

    explicit Writer(const SvgOptions& o) : opts(o) {}

    std::string glyphDef(const std::shared_ptr<glyphware::Face>& face, uint32_t gid) {
        int fid;
        auto fit = faceIds.find(face.get());
        if (fit == faceIds.end()) {
            fid = static_cast<int>(faceIds.size());
            faceIds.emplace(face.get(), fid);
        } else {
            fid = fit->second;
        }
        const auto key = std::make_pair(fid, gid);
        auto it = glyphIds.find(key);
        if (it != glyphIds.end()) return it->second;

        PathSink sink;
        face->glyphOutline(gid, sink);
        const std::string id = "g" + std::to_string(fid) + "_" + std::to_string(gid);
        defs += "<path id=\"" + id + "\" d=\"" + sink.d + "\"/>\n";
        glyphIds.emplace(key, id);
        return id;
    }

    std::map<int, std::string> blurFilters;   ///< 量子化した stdDeviation → filter id

    /// ガウスぼかしの <filter>（stdDeviation = 半径 / 2、ユーザー座標）。同じ半径は 1 つにまとめる
    std::string blurFilter(float radius) {
        const int key = static_cast<int>(std::lround(radius * 100.0f));
        auto it = blurFilters.find(key);
        if (it != blurFilters.end()) return it->second;
        const std::string id = "blur" + std::to_string(blurFilters.size());
        defs += "<filter id=\"" + id + "\" x=\"-100%\" y=\"-100%\" width=\"300%\" height=\"300%\">"
                "<feGaussianBlur stdDeviation=\"" + fmt(radius * 0.5f, opts.precision) + "\"/></filter>\n";
        blurFilters.emplace(key, id);
        return id;
    }

    void glyphRun(const dl::GlyphRun& run, const Matrix& ctm, float opacity) {
        if (!run.face || run.glyphs.empty()) return;
        const float upem = font::unitsPerEm(*run.face);
        const float s = run.size / upem;
        Matrix base;                     // フォントユニット（y は defs で反転済み）→ pt
        base.xx = s; base.yy = s;
        const float devScale = std::sqrt(std::fabs(ctm.determinant()));

        // グリフ座標系に掛かるスケールを線幅から打ち消す（一様スケール前提）
        std::optional<Stroke> stroke = run.stroke;
        if (stroke && run.embolden > 0.0f) stroke->width += run.embolden;
        std::string attrs = paintAttrs(run.fill, stroke, 1.0f / (s * devScale), opacity, opts.precision);
        if (run.embolden > 0.0f && run.fill && !run.stroke) {
            // フェイクボールドは同色の縁取りで太らせる
            Stroke fake;
            fake.color = *run.fill;
            fake.width = run.embolden;
            fake.join = StrokeJoin::Round;
            attrs = paintAttrs(run.fill, fake, 1.0f / (s * devScale), opacity, opts.precision);
        }

        body += "<g" + attrs;
        if (run.blur > 0.0f) body += " filter=\"url(#" + blurFilter(run.blur * devScale) + ")\"";
        if (opts.includeText && run.text) {
            body += " aria-label=\"" + escapeXml(utf16ToUtf8(*run.text)) + "\"";
        }
        body += ">\n";
        for (const dl::Glyph& g : run.glyphs) {
            const std::string id = opts.useDefs ? glyphDef(run.face, g.gid) : std::string();
            Matrix m = multiply(Matrix::fromMat2(g.xform), base);
            m = multiply(Matrix::translation(g.pos.x, g.pos.y), m);
            m = multiply(ctm, m);
            if (opts.useDefs) {
                body += "<use href=\"#" + id + "\" transform=\"" + matrixAttr(m, opts.precision) + "\"/>\n";
            } else {
                PathSink sink;
                run.face->glyphOutline(g.gid, sink);
                body += "<path d=\"" + sink.d + "\" transform=\"" + matrixAttr(m, opts.precision) + "\"/>\n";
            }
        }
        body += "</g>\n";
    }

    void pathItem(const Path& path, const std::optional<Color>& fill,
                  const std::optional<Stroke>& stroke, bool evenOdd, const Matrix& ctm,
                  float opacity) {
        if (path.empty()) return;
        const float devScale = std::sqrt(std::fabs(ctm.determinant()));
        body += "<path d=\"" + pathData(path.transformed(ctm), opts.precision) + "\"" +
                paintAttrs(fill, stroke, devScale, opacity, opts.precision);
        if (evenOdd) body += " fill-rule=\"evenodd\"";
        body += "/>\n";
    }

    void items(const std::vector<dl::Item>& list, const Matrix& ctm, float opacity) {
        for (const dl::Item& item : list) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                glyphRun(*run, ctm, opacity);
            } else if (const auto* p = std::get_if<dl::PathItem>(&item)) {
                pathItem(p->path, p->fill, p->stroke, p->evenOdd, ctm, opacity);
            } else if (const auto* r = std::get_if<dl::RectItem>(&item)) {
                Path path;
                path.addRect(r->rect);
                pathItem(path, r->fill, std::nullopt, false, ctm, opacity);
            } else if (const auto* img = std::get_if<dl::ImageItem>(&item)) {
                if (!img->image || img->image->width <= 0) continue;
                const dl::Image& im = *img->image;
                std::string mime = "image/png";
                std::string bytes;
                if (im.encoding == dl::Image::Encoding::Jpeg && !im.encoded.empty()) {
                    mime = "image/jpeg";
                    bytes.assign(im.encoded.begin(), im.encoded.end());
                } else {
                    bytes = image::toPng(im);
                }
                const Matrix m = multiply(ctm, img->xform);
                body += "<image width=\"" + std::to_string(im.width) + "\" height=\"" +
                        std::to_string(im.height) + "\" preserveAspectRatio=\"none\" transform=\"" +
                        matrixAttr(m, opts.precision) + "\"";
                const float a = img->opacity * opacity;
                if (a < 0.999f) body += " opacity=\"" + fmt(a, 3) + "\"";
                body += " href=\"data:" + mime + ";base64," + image::base64(bytes) + "\"/>\n";
            } else if (const auto* group = std::get_if<dl::Group>(&item)) {
                // クリップは親座標系なので、変形の無い外側の <g> に付ける
                std::string open;
                if (group->clip) {
                    const Rect c = *group->clip;
                    const Path clipPath = Path{}.transformed(ctm);   // dummy for type
                    (void)clipPath;
                    const std::string id = "clip" + std::to_string(++clipCounter);
                    Path rectPath;
                    rectPath.addRect(c);
                    defs += "<clipPath id=\"" + id + "\"><path d=\"" +
                            pathData(rectPath.transformed(ctm), opts.precision) + "\"/></clipPath>\n";
                    open += "<g clip-path=\"url(#" + id + ")\">\n";
                }
                body += open;
                items(group->children, multiply(ctm, group->xform), opacity * group->opacity);
                if (group->clip) body += "</g>\n";
            }
        }
    }
};

} // namespace

std::string writeSvg(const dl::DisplayList& list, const SvgOptions& opts) {
    Writer w(opts);
    w.items(list.items, Matrix::identity(), 1.0f);

    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\"";
    out += " width=\"" + fmt(list.page.w, opts.precision) + "pt\"";
    out += " height=\"" + fmt(list.page.h, opts.precision) + "pt\"";
    out += " viewBox=\"0 0 " + fmt(list.page.w, opts.precision) + " " + fmt(list.page.h, opts.precision) + "\">\n";
    if (!w.defs.empty()) out += "<defs>\n" + w.defs + "</defs>\n";
    out += w.body;
    out += "</svg>\n";
    return out;
}

bool saveSvg(const dl::DisplayList& list, const std::string& path, const SvgOptions& opts) {
    const std::string data = writeSvg(list, opts);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

} // namespace typeset::backend
