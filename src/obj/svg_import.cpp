/**
 * svg_import.cpp — SVG サブセットの読み込み
 *
 * 小さな XML パーサ（要素・属性・コメント・自己終了タグ）と、SVG の描画要素の解釈。
 */

#include "typeset/obj/svg_import.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace typeset::obj {

namespace {

//------------------------------------------------------------------------------
// 最小 XML
//------------------------------------------------------------------------------

struct XmlNode {
    std::string name;
    std::map<std::string, std::string> attrs;
    std::vector<XmlNode> children;
};

struct XmlParser {
    const std::string& s;
    size_t i = 0;
    std::vector<std::string> comments;

    explicit XmlParser(const std::string& src) : s(src) {}

    void skipWs() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }

    bool startsWith(const char* t) const { return s.compare(i, std::strlen(t), t) == 0; }

    std::string decodeEntities(std::string v) {
        const std::pair<const char*, const char*> ents[] = {
            {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
        for (const auto& e : ents) {
            size_t p = 0;
            while ((p = v.find(e.first, p)) != std::string::npos) {
                v.replace(p, std::strlen(e.first), e.second);
                p += 1;
            }
        }
        return v;
    }

    /// 先頭の要素を読む。無ければ false
    bool parseElement(XmlNode& out) {
        for (;;) {
            skipWs();
            if (i >= s.size()) return false;
            if (startsWith("<!--")) {
                const size_t e = s.find("-->", i + 4);
                comments.push_back(s.substr(i + 4, e == std::string::npos ? std::string::npos : e - i - 4));
                i = (e == std::string::npos) ? s.size() : e + 3;
                continue;
            }
            if (startsWith("<?") || startsWith("<!")) {
                if (startsWith("<![CDATA[")) {
                    const size_t e = s.find("]]>", i);
                    i = (e == std::string::npos) ? s.size() : e + 3;
                } else {
                    const size_t e = s.find('>', i);
                    i = (e == std::string::npos) ? s.size() : e + 1;
                }
                continue;
            }
            if (s[i] != '<') {
                // テキスト: 読み飛ばす
                const size_t e = s.find('<', i);
                i = (e == std::string::npos) ? s.size() : e;
                continue;
            }
            if (startsWith("</")) return false;   // 親の終了
            break;
        }
        ++i;   // '<'
        const size_t ns = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/') ++i;
        out.name = s.substr(ns, i - ns);
        // 名前空間接頭辞を落とす（svg:path 等）
        const size_t colon = out.name.find(':');
        if (colon != std::string::npos) out.name = out.name.substr(colon + 1);

        // 属性
        for (;;) {
            skipWs();
            if (i >= s.size()) return true;
            if (s[i] == '/') {
                i += 2;   // "/>"
                return true;
            }
            if (s[i] == '>') { ++i; break; }
            const size_t as = i;
            while (i < s.size() && s[i] != '=' && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/') ++i;
            std::string key = s.substr(as, i - as);
            skipWs();
            std::string value;
            if (i < s.size() && s[i] == '=') {
                ++i;
                skipWs();
                if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
                    const char q = s[i++];
                    const size_t vs = i;
                    while (i < s.size() && s[i] != q) ++i;
                    value = s.substr(vs, i - vs);
                    if (i < s.size()) ++i;
                }
            }
            const size_t kc = key.find(':');
            if (kc != std::string::npos && key.compare(0, 6, "xlink:") == 0) key = key.substr(6);
            out.attrs[key] = decodeEntities(value);
        }
        // 子要素
        for (;;) {
            XmlNode child;
            if (parseElement(child)) {
                out.children.push_back(std::move(child));
                continue;
            }
            skipWs();
            if (i >= s.size()) return true;
            if (startsWith("</")) {
                const size_t e = s.find('>', i);
                i = (e == std::string::npos) ? s.size() : e + 1;
                return true;
            }
            // parseElement が false を返したがテキストや不明: 進める
            if (s[i] != '<') ++i;
        }
    }
};

//------------------------------------------------------------------------------
// 数値・単位・変形
//------------------------------------------------------------------------------

struct Ctx {
    SvgImportOptions opts;
};

float parseLength(const std::string& v, const Ctx& ctx, bool* ok = nullptr) {
    if (v.empty()) { if (ok) *ok = false; return 0.0f; }
    char* end = nullptr;
    const double num = std::strtod(v.c_str(), &end);
    if (end == v.c_str()) { if (ok) *ok = false; return 0.0f; }
    std::string unit(end);
    unit.erase(std::remove_if(unit.begin(), unit.end(), [](unsigned char c) { return std::isspace(c); }), unit.end());
    float f = 0.75f;   // px → pt（96dpi）
    if (unit.empty() || unit == "px") f = 0.75f;
    else if (unit == "pt") f = 1.0f;
    else if (unit == "mm") f = 72.0f / 25.4f;
    else if (unit == "cm") f = 72.0f / 2.54f;
    else if (unit == "in") f = 72.0f;
    else if (unit == "pc") f = 12.0f;
    else if (unit == "em") f = ctx.opts.fontSize;
    else if (unit == "ex") f = ctx.opts.fontSize * ctx.opts.exRatio;
    else if (unit == "%") { if (ok) *ok = false; return 0.0f; }
    if (ok) *ok = true;
    return static_cast<float>(num) * f;
}

std::vector<float> parseNumbers(const std::string& v) {
    std::vector<float> out;
    const char* p = v.c_str();
    while (*p) {
        while (*p && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) ++p;
        if (!*p) break;
        char* end = nullptr;
        const double d = std::strtod(p, &end);
        if (end == p) { ++p; continue; }
        out.push_back(static_cast<float>(d));
        p = end;
    }
    return out;
}

Matrix parseTransform(const std::string& t) {
    Matrix m;
    size_t i = 0;
    while (i < t.size()) {
        while (i < t.size() && (std::isspace(static_cast<unsigned char>(t[i])) || t[i] == ',')) ++i;
        const size_t ns = i;
        while (i < t.size() && std::isalpha(static_cast<unsigned char>(t[i]))) ++i;
        const std::string name = t.substr(ns, i - ns);
        const size_t open = t.find('(', i);
        if (open == std::string::npos) break;
        const size_t close = t.find(')', open);
        if (close == std::string::npos) break;
        const std::vector<float> a = parseNumbers(t.substr(open + 1, close - open - 1));
        i = close + 1;
        Matrix op;
        if (name == "matrix" && a.size() >= 6) {
            op.xx = a[0]; op.yx = a[1]; op.xy = a[2]; op.yy = a[3]; op.dx = a[4]; op.dy = a[5];
        } else if (name == "translate" && !a.empty()) {
            op.dx = a[0]; op.dy = a.size() > 1 ? a[1] : 0.0f;
        } else if (name == "scale" && !a.empty()) {
            op.xx = a[0]; op.yy = a.size() > 1 ? a[1] : a[0];
        } else if (name == "rotate" && !a.empty()) {
            const float r = a[0] * 3.14159265f / 180.0f;
            const float c = std::cos(r), s = std::sin(r);
            Matrix rot; rot.xx = c; rot.xy = -s; rot.yx = s; rot.yy = c;
            if (a.size() >= 3) {
                op = multiply(Matrix::translation(a[1], a[2]), multiply(rot, Matrix::translation(-a[1], -a[2])));
            } else {
                op = rot;
            }
        } else if (name == "skewX" && !a.empty()) {
            op.xy = std::tan(a[0] * 3.14159265f / 180.0f);
        } else if (name == "skewY" && !a.empty()) {
            op.yx = std::tan(a[0] * 3.14159265f / 180.0f);
        } else {
            continue;
        }
        m = multiply(m, op);
    }
    return m;
}

//------------------------------------------------------------------------------
// 塗り・線
//------------------------------------------------------------------------------

struct Paint {
    std::optional<Color> fill = Color{0, 0, 0, 255};   // SVG の既定は黒塗り
    bool fillNone = false;
    std::optional<Color> stroke;
    Pt strokeWidth = 1.0f;
    float opacity = 1.0f;
    float fillOpacity = 1.0f;
    float strokeOpacity = 1.0f;
    bool evenOdd = false;
    StrokeCap cap = StrokeCap::Butt;
    StrokeJoin join = StrokeJoin::Miter;
};

std::optional<Color> parseColor(std::string v, bool& none) {
    none = false;
    v.erase(std::remove_if(v.begin(), v.end(), [](unsigned char c) { return std::isspace(c); }), v.end());
    if (v.empty() || v == "inherit") return std::nullopt;
    if (v == "none" || v == "transparent") { none = true; return std::nullopt; }
    if (v == "currentColor") return Color{0, 0, 0, 255};
    if (v[0] == '#') {
        const std::string h = v.substr(1);
        auto hex = [&](size_t a, size_t n) {
            return static_cast<uint8_t>(std::strtol(h.substr(a, n).c_str(), nullptr, 16));
        };
        if (h.size() == 3) {
            const uint8_t r = hex(0, 1), g = hex(1, 1), b = hex(2, 1);
            return Color{static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17), static_cast<uint8_t>(b * 17), 255};
        }
        if (h.size() == 6) return Color{hex(0, 2), hex(2, 2), hex(4, 2), 255};
        if (h.size() == 8) return Color{hex(0, 2), hex(2, 2), hex(4, 2), hex(6, 2)};
        return std::nullopt;
    }
    if (v.compare(0, 4, "rgb(") == 0 || v.compare(0, 5, "rgba(") == 0) {
        const size_t open = v.find('(');
        const std::vector<float> a = parseNumbers(v.substr(open + 1));
        if (a.size() >= 3) {
            auto ch = [&](float x) { return static_cast<uint8_t>(std::clamp(x, 0.0f, 255.0f)); };
            const uint8_t alpha = a.size() >= 4 ? static_cast<uint8_t>(std::clamp(a[3] * 255.0f, 0.0f, 255.0f)) : 255;
            return Color{ch(a[0]), ch(a[1]), ch(a[2]), alpha};
        }
        return std::nullopt;
    }
    static const std::map<std::string, uint32_t> named = {
        {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"green", 0x008000}, {"blue", 0x0000FF},
        {"gray", 0x808080}, {"grey", 0x808080}, {"yellow", 0xFFFF00}, {"orange", 0xFFA500}, {"purple", 0x800080},
        {"cyan", 0x00FFFF}, {"magenta", 0xFF00FF}, {"lime", 0x00FF00}, {"navy", 0x000080}, {"teal", 0x008080},
        {"silver", 0xC0C0C0}, {"maroon", 0x800000}, {"olive", 0x808000}, {"brown", 0xA52A2A}, {"pink", 0xFFC0CB},
        {"darkgray", 0xA9A9A9}, {"lightgray", 0xD3D3D3}, {"darkblue", 0x00008B}, {"darkgreen", 0x006400},
        {"darkred", 0x8B0000}, {"steelblue", 0x4682B4}, {"tomato", 0xFF6347}, {"gold", 0xFFD700},
    };
    auto it = named.find(v);
    if (it != named.end()) return Color::argb(0xFF000000u | it->second);
    return std::nullopt;
}

/// style="a:b;c:d" を属性に展開
void applyStyle(const std::string& style, std::map<std::string, std::string>& attrs) {
    size_t i = 0;
    while (i < style.size()) {
        const size_t colon = style.find(':', i);
        if (colon == std::string::npos) break;
        size_t semi = style.find(';', colon);
        if (semi == std::string::npos) semi = style.size();
        std::string k = style.substr(i, colon - i), v = style.substr(colon + 1, semi - colon - 1);
        auto trim = [](std::string& x) {
            while (!x.empty() && std::isspace(static_cast<unsigned char>(x.front()))) x.erase(x.begin());
            while (!x.empty() && std::isspace(static_cast<unsigned char>(x.back()))) x.pop_back();
        };
        trim(k); trim(v);
        if (!k.empty()) attrs[k] = v;
        i = semi + 1;
    }
}

Paint inheritPaint(const Paint& parent, const std::map<std::string, std::string>& a, const Ctx& ctx) {
    Paint p = parent;
    auto get = [&](const char* k) -> const std::string* {
        auto it = a.find(k);
        return it == a.end() ? nullptr : &it->second;
    };
    if (const std::string* v = get("fill")) {
        bool none = false;
        const std::optional<Color> c = parseColor(*v, none);
        if (none) { p.fill.reset(); p.fillNone = true; }
        else if (c) { p.fill = c; p.fillNone = false; }
    }
    if (const std::string* v = get("stroke")) {
        bool none = false;
        const std::optional<Color> c = parseColor(*v, none);
        if (none) p.stroke.reset();
        else if (c) p.stroke = c;
    }
    if (const std::string* v = get("stroke-width")) p.strokeWidth = parseLength(*v, ctx) / 0.75f;   // ユーザ単位
    if (const std::string* v = get("opacity")) p.opacity *= static_cast<float>(std::atof(v->c_str()));
    if (const std::string* v = get("fill-opacity")) p.fillOpacity = static_cast<float>(std::atof(v->c_str()));
    if (const std::string* v = get("stroke-opacity")) p.strokeOpacity = static_cast<float>(std::atof(v->c_str()));
    if (const std::string* v = get("fill-rule")) p.evenOdd = (*v == "evenodd");
    if (const std::string* v = get("stroke-linecap")) {
        p.cap = (*v == "round") ? StrokeCap::Round : (*v == "square") ? StrokeCap::Square : StrokeCap::Butt;
    }
    if (const std::string* v = get("stroke-linejoin")) {
        p.join = (*v == "round") ? StrokeJoin::Round : (*v == "bevel") ? StrokeJoin::Bevel : StrokeJoin::Miter;
    }
    return p;
}

//------------------------------------------------------------------------------
// パス
//------------------------------------------------------------------------------

/// 楕円弧 → 3 次ベジェ列
void arcTo(Path& path, Point p0, float rx, float ry, float rotDeg, bool largeArc, bool sweep, Point p1) {
    if (rx == 0.0f || ry == 0.0f) { path.lineTo(p1.x, p1.y); return; }
    rx = std::fabs(rx); ry = std::fabs(ry);
    const float phi = rotDeg * 3.14159265f / 180.0f;
    const float cphi = std::cos(phi), sphi = std::sin(phi);
    const float dx2 = (p0.x - p1.x) * 0.5f, dy2 = (p0.y - p1.y) * 0.5f;
    const float x1p = cphi * dx2 + sphi * dy2;
    const float y1p = -sphi * dx2 + cphi * dy2;
    float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0f) { const float s = std::sqrt(lambda); rx *= s; ry *= s; }
    const float num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    const float den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    float coef = (den > 0.0f) ? std::sqrt(std::max(0.0f, num / den)) : 0.0f;
    if (largeArc == sweep) coef = -coef;
    const float cxp = coef * (rx * y1p / ry);
    const float cyp = coef * (-ry * x1p / rx);
    const float cx = cphi * cxp - sphi * cyp + (p0.x + p1.x) * 0.5f;
    const float cy = sphi * cxp + cphi * cyp + (p0.y + p1.y) * 0.5f;
    auto angle = [](float ux, float uy, float vx, float vy) {
        const float dot = ux * vx + uy * vy;
        const float len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        float a = std::acos(std::clamp(dot / std::max(len, 1e-12f), -1.0f, 1.0f));
        if (ux * vy - uy * vx < 0.0f) a = -a;
        return a;
    };
    const float theta1 = angle(1.0f, 0.0f, (x1p - cxp) / rx, (y1p - cyp) / ry);
    float dtheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && dtheta > 0.0f) dtheta -= 2.0f * 3.14159265f;
    else if (sweep && dtheta < 0.0f) dtheta += 2.0f * 3.14159265f;
    const int segs = std::max(1, static_cast<int>(std::ceil(std::fabs(dtheta) / (3.14159265f / 2.0f))));
    const float delta = dtheta / segs;
    const float t = 4.0f / 3.0f * std::tan(delta / 4.0f);
    float th = theta1;
    for (int i = 0; i < segs; ++i) {
        const float c1 = std::cos(th), s1 = std::sin(th);
        const float c2 = std::cos(th + delta), s2 = std::sin(th + delta);
        auto map = [&](float ex, float ey) {
            return Point{cx + cphi * rx * ex - sphi * ry * ey, cy + sphi * rx * ex + cphi * ry * ey};
        };
        const Point q1 = map(c1 - t * s1, s1 + t * c1);
        const Point q2 = map(c2 + t * s2, s2 - t * c2);
        const Point q3 = map(c2, s2);
        path.cubicTo(q1.x, q1.y, q2.x, q2.y, q3.x, q3.y);
        th += delta;
    }
}

Path parsePathData(const std::string& d) {
    Path path;
    const char* p = d.c_str();
    char cmd = 0;
    Point cur{0, 0}, start{0, 0}, lastCtrl{0, 0};
    char lastCmd = 0;
    auto skip = [&]() { while (*p && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) ++p; };
    auto number = [&](float& out) -> bool {
        skip();
        if (!*p) return false;
        char* end = nullptr;
        const double v = std::strtod(p, &end);
        if (end == p) return false;
        out = static_cast<float>(v);
        p = end;
        return true;
    };
    auto flag = [&](bool& out) -> bool {
        skip();
        if (*p != '0' && *p != '1') return false;
        out = (*p == '1');
        ++p;
        return true;
    };
    while (true) {
        skip();
        if (!*p) break;
        if (std::isalpha(static_cast<unsigned char>(*p))) cmd = *p++;
        else if (!cmd) break;
        const bool rel = std::islower(static_cast<unsigned char>(cmd));
        const char C = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
        float a, b, c, e, f, g;
        switch (C) {
        case 'M':
            if (!number(a) || !number(b)) return path;
            if (rel) { a += cur.x; b += cur.y; }
            path.moveTo(a, b); cur = start = {a, b};
            cmd = rel ? 'l' : 'L';   // 続く座標は lineTo
            break;
        case 'L':
            if (!number(a) || !number(b)) return path;
            if (rel) { a += cur.x; b += cur.y; }
            path.lineTo(a, b); cur = {a, b};
            break;
        case 'H':
            if (!number(a)) return path;
            if (rel) a += cur.x;
            path.lineTo(a, cur.y); cur.x = a;
            break;
        case 'V':
            if (!number(a)) return path;
            if (rel) a += cur.y;
            path.lineTo(cur.x, a); cur.y = a;
            break;
        case 'C':
            if (!number(a) || !number(b) || !number(c) || !number(e) || !number(f) || !number(g)) return path;
            if (rel) { a += cur.x; b += cur.y; c += cur.x; e += cur.y; f += cur.x; g += cur.y; }
            path.cubicTo(a, b, c, e, f, g); lastCtrl = {c, e}; cur = {f, g};
            break;
        case 'S': {
            if (!number(c) || !number(e) || !number(f) || !number(g)) return path;
            if (rel) { c += cur.x; e += cur.y; f += cur.x; g += cur.y; }
            const bool prevCubic = (lastCmd == 'C' || lastCmd == 'S');
            const Point c1 = prevCubic ? Point{2 * cur.x - lastCtrl.x, 2 * cur.y - lastCtrl.y} : cur;
            path.cubicTo(c1.x, c1.y, c, e, f, g); lastCtrl = {c, e}; cur = {f, g};
            break;
        }
        case 'Q':
            if (!number(a) || !number(b) || !number(c) || !number(e)) return path;
            if (rel) { a += cur.x; b += cur.y; c += cur.x; e += cur.y; }
            path.quadTo(a, b, c, e); lastCtrl = {a, b}; cur = {c, e};
            break;
        case 'T': {
            if (!number(c) || !number(e)) return path;
            if (rel) { c += cur.x; e += cur.y; }
            const bool prevQuad = (lastCmd == 'Q' || lastCmd == 'T');
            const Point q = prevQuad ? Point{2 * cur.x - lastCtrl.x, 2 * cur.y - lastCtrl.y} : cur;
            path.quadTo(q.x, q.y, c, e); lastCtrl = q; cur = {c, e};
            break;
        }
        case 'A': {
            bool large = false, sweep = false;
            if (!number(a) || !number(b) || !number(c) || !flag(large) || !flag(sweep) || !number(f) || !number(g)) return path;
            if (rel) { f += cur.x; g += cur.y; }
            arcTo(path, cur, a, b, c, large, sweep, {f, g}); cur = {f, g};
            break;
        }
        case 'Z':
            path.close(); cur = start;
            break;
        default:
            return path;
        }
        lastCmd = C;
    }
    return path;
}

//------------------------------------------------------------------------------
// 走査
//------------------------------------------------------------------------------

struct Walker {
    const Ctx& ctx;
    std::map<std::string, const XmlNode*> defs;   // id → 要素
    std::vector<dl::Item> out;
    int depth = 0;

    explicit Walker(const Ctx& c) : ctx(c) {}

    void collectDefs(const XmlNode& n) {
        auto it = n.attrs.find("id");
        if (it != n.attrs.end()) defs[it->second] = &n;
        for (const XmlNode& ch : n.children) collectDefs(ch);
    }

    void emitPath(Path path, const Paint& paint, const Matrix& ctm) {
        if (path.empty()) return;
        dl::PathItem item;
        item.path = path.transformed(ctm);
        if (paint.fill && !paint.fillNone) {
            Color c = *paint.fill;
            c.a = static_cast<uint8_t>(std::clamp(c.a * paint.opacity * paint.fillOpacity, 0.0f, 255.0f));
            item.fill = c;
        }
        if (paint.stroke && paint.strokeWidth > 0.0f) {
            Stroke s;
            Color sc = *paint.stroke;
            sc.a = static_cast<uint8_t>(std::clamp(sc.a * paint.opacity * paint.strokeOpacity, 0.0f, 255.0f));
            s.color = sc;
            s.width = paint.strokeWidth * std::sqrt(std::fabs(ctm.determinant()));
            s.cap = paint.cap;
            s.join = paint.join;
            item.stroke = s;
        }
        item.evenOdd = paint.evenOdd;
        if (item.fill || item.stroke) out.push_back(std::move(item));
    }

    void walk(const XmlNode& n, Paint paint, Matrix ctm, bool inDefs) {
        if (++depth > 200) { --depth; return; }
        std::map<std::string, std::string> attrs = n.attrs;
        auto st = attrs.find("style");
        if (st != attrs.end()) applyStyle(st->second, attrs);
        paint = inheritPaint(paint, attrs, ctx);
        auto tr = attrs.find("transform");
        if (tr != attrs.end()) ctm = multiply(ctm, parseTransform(tr->second));
        auto num = [&](const char* k, float def = 0.0f) {
            auto it = attrs.find(k);
            return it == attrs.end() ? def : parseLength(it->second, ctx) / 0.75f;   // ユーザ単位（px）で扱う
        };

        const std::string& name = n.name;
        if (name == "defs" || name == "clipPath" || name == "mask" || name == "symbol" || name == "marker" ||
            name == "linearGradient" || name == "radialGradient" || name == "pattern" || name == "title" ||
            name == "desc" || name == "metadata" || name == "style") {
            if (!inDefs || name != "symbol") { --depth; return; }
        }
        if (name == "g" || name == "svg" || name == "symbol" || name == "a") {
            for (const XmlNode& ch : n.children) walk(ch, paint, ctm, inDefs);
        } else if (name == "path") {
            auto d = attrs.find("d");
            if (d != attrs.end()) emitPath(parsePathData(d->second), paint, ctm);
        } else if (name == "rect") {
            Path p;
            const float x = num("x"), y = num("y"), w = num("width"), h = num("height");
            const float rx = num("rx"), ry = num("ry", num("rx"));
            if (w > 0.0f && h > 0.0f) {
                if (rx > 0.0f || ry > 0.0f) {
                    const float ax = std::min(rx > 0 ? rx : ry, w * 0.5f), ay = std::min(ry > 0 ? ry : rx, h * 0.5f);
                    p.moveTo(x + ax, y);
                    p.lineTo(x + w - ax, y);
                    arcTo(p, {x + w - ax, y}, ax, ay, 0, false, true, {x + w, y + ay});
                    p.lineTo(x + w, y + h - ay);
                    arcTo(p, {x + w, y + h - ay}, ax, ay, 0, false, true, {x + w - ax, y + h});
                    p.lineTo(x + ax, y + h);
                    arcTo(p, {x + ax, y + h}, ax, ay, 0, false, true, {x, y + h - ay});
                    p.lineTo(x, y + ay);
                    arcTo(p, {x, y + ay}, ax, ay, 0, false, true, {x + ax, y});
                    p.close();
                } else {
                    p.addRect(Rect{x, y, w, h});
                }
                emitPath(p, paint, ctm);
            }
        } else if (name == "circle" || name == "ellipse") {
            const float cx = num("cx"), cy = num("cy");
            const float rx = name == "circle" ? num("r") : num("rx");
            const float ry = name == "circle" ? num("r") : num("ry");
            if (rx > 0.0f && ry > 0.0f) {
                Path p;
                p.addEllipse(Rect{cx - rx, cy - ry, rx * 2, ry * 2});
                emitPath(p, paint, ctm);
            }
        } else if (name == "line") {
            Path p;
            p.addLine({num("x1"), num("y1")}, {num("x2"), num("y2")});
            Paint pp = paint;
            pp.fill.reset(); pp.fillNone = true;
            emitPath(p, pp, ctm);
        } else if (name == "polyline" || name == "polygon") {
            auto pts = attrs.find("points");
            if (pts != attrs.end()) {
                const std::vector<float> v = parseNumbers(pts->second);
                Path p;
                for (size_t i = 0; i + 1 < v.size(); i += 2) {
                    if (i == 0) p.moveTo(v[i], v[i + 1]);
                    else p.lineTo(v[i], v[i + 1]);
                }
                if (name == "polygon") p.close();
                Paint pp = paint;
                if (name == "polyline" && attrs.find("fill") == attrs.end()) { pp.fill.reset(); pp.fillNone = true; }
                emitPath(p, pp, ctm);
            }
        } else if (name == "use") {
            auto href = attrs.find("href");
            if (href != attrs.end() && !href->second.empty() && href->second[0] == '#') {
                auto it = defs.find(href->second.substr(1));
                if (it != defs.end()) {
                    const Matrix shift = Matrix::translation(num("x"), num("y"));
                    walk(*it->second, paint, multiply(ctm, shift), true);
                }
            }
        }
        --depth;
    }
};

std::optional<float> readBaselineComment(const std::vector<std::string>& comments, const Ctx& ctx) {
    for (const std::string& c : comments) {
        const size_t p = c.find("typeset");
        if (p == std::string::npos) continue;
        const size_t b = c.find("baseline", p);
        if (b == std::string::npos) continue;
        const size_t eq = c.find('=', b);
        if (eq == std::string::npos) continue;
        size_t i = eq + 1;
        while (i < c.size() && (std::isspace(static_cast<unsigned char>(c[i])) || c[i] == '"' || c[i] == '\'')) ++i;
        size_t j = i;
        while (j < c.size() && c[j] != '"' && c[j] != '\'' && !std::isspace(static_cast<unsigned char>(c[j])) && c[j] != '-') ++j;
        if (j < c.size() && c[j] == '-' && j == i) { ++j; while (j < c.size() && c[j] != '"' && !std::isspace(static_cast<unsigned char>(c[j]))) ++j; }
        const std::string v = c.substr(i, j - i);
        // 単位なしは pt（契約）。単位付きなら換算
        const bool hasUnit = std::any_of(v.begin(), v.end(), [](unsigned char ch) { return std::isalpha(ch) || ch == '%'; });
        return hasUnit ? parseLength(v, ctx) : static_cast<float>(std::atof(v.c_str()));
    }
    return std::nullopt;
}

} // namespace

bool importSvg(const std::string& svg, const SvgImportOptions& opts, ObjectResult& out) {
    out = ObjectResult{};
    Ctx ctx{opts};
    XmlParser parser(svg);
    XmlNode root;
    // 先頭の要素（<svg>）まで進む
    if (!parser.parseElement(root) || root.name != "svg") {
        out.error = "not an <svg> document";
        return false;
    }

    std::map<std::string, std::string> attrs = root.attrs;
    auto st = attrs.find("style");
    if (st != attrs.end()) applyStyle(st->second, attrs);

    // viewBox と大きさ
    float vbX = 0, vbY = 0, vbW = 0, vbH = 0;
    auto vb = attrs.find("viewBox");
    if (vb != attrs.end()) {
        const std::vector<float> v = parseNumbers(vb->second);
        if (v.size() >= 4) { vbX = v[0]; vbY = v[1]; vbW = v[2]; vbH = v[3]; }
    }
    bool okW = false, okH = false;
    float w = 0.0f, h = 0.0f;
    auto aw = attrs.find("width");
    auto ah = attrs.find("height");
    if (aw != attrs.end()) w = parseLength(aw->second, ctx, &okW);
    if (ah != attrs.end()) h = parseLength(ah->second, ctx, &okH);
    if (!okW && vbW > 0.0f) w = vbW * 0.75f;
    if (!okH && vbH > 0.0f) h = vbH * 0.75f;
    if (vbW <= 0.0f || vbH <= 0.0f) { vbW = w / 0.75f; vbH = h / 0.75f; }
    if (w <= 0.0f || h <= 0.0f) {
        out.error = "svg has no usable width/height";
        return false;
    }
    out.size = Size{w, h};

    // ユーザ単位 → pt（viewBox → 箱）
    Matrix base = multiply(Matrix::scaling(w / vbW, h / vbH), Matrix::translation(-vbX, -vbY));

    Walker walker(ctx);
    walker.collectDefs(root);
    Paint paint;
    for (const XmlNode& ch : root.children) walker.walk(ch, paint, base, false);
    out.items = std::move(walker.out);

    // ベースライン
    if (auto b = readBaselineComment(parser.comments, ctx)) {
        out.baseline = *b;
        out.hasBaseline = true;
    } else {
        auto va = attrs.find("vertical-align");
        if (va != attrs.end()) {
            // MathJax: vertical-align: -0.5ex → ベースラインは下端から 0.5ex 上
            const float depth = -parseLength(va->second, ctx);
            out.baseline = h - depth;
            out.hasBaseline = true;
        }
    }
    return true;
}

} // namespace typeset::obj
