/**
 * tag_parser.cpp — 軽量インラインタグ記法（richtext 互換）
 *
 * タグを読んでスタイルのスタックを積み、本文を run に切って Paragraph を組み立てる。
 * 注記（ルビ・圏点など）は開いた位置と閉じた位置から範囲を作る。
 */

#include "typeset/inl/tag_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

#include "typeset/text/utf.hpp"

namespace typeset::inl {

namespace {

using Attrs = std::map<std::string, std::string>;

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isNameChar(char16_t c) {
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') ||
           c == u'-' || c == u'_' || c == u':';
}

void skipSpace(const std::u16string& s, size_t& i) {
    while (i < s.size() && (s[i] == u' ' || s[i] == u'\t' || s[i] == u'\n' || s[i] == u'\r')) ++i;
}

std::string parseName(const std::u16string& s, size_t& i) {
    std::string out;
    while (i < s.size() && isNameChar(s[i])) out += static_cast<char>(s[i++]);
    return out;
}

float toFloat(const std::string& s) { return static_cast<float>(std::atof(s.c_str())); }
int toInt(const std::string& s) { return std::atoi(s.c_str()); }

/// "#rrggbb" / "#aarrggbb" / "0xrrggbb" / 10 進の整数
Color parseColor(const std::string& value, Color fallback) {
    std::string v = value;
    if (!v.empty() && v[0] == '#') v = v.substr(1);
    if (v.size() >= 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v = v.substr(2);
    if (v.empty()) return fallback;
    const bool hex = v.size() == 6 || v.size() == 8;
    char* end = nullptr;
    const unsigned long n = std::strtoul(v.c_str(), &end, hex ? 16 : 10);
    if (end == v.c_str()) return fallback;
    if (v.size() == 8) return Color::argb(static_cast<uint32_t>(n));
    return Color::rgb(static_cast<uint8_t>((n >> 16) & 0xFF), static_cast<uint8_t>((n >> 8) & 0xFF),
                      static_cast<uint8_t>(n & 0xFF));
}

Color attrColor(const Attrs& a, Color fallback) {
    auto it = a.find("value");
    if (it != a.end()) return parseColor(it->second, fallback);
    it = a.find("color");
    if (it != a.end()) return parseColor(it->second, fallback);
    Color c = fallback;
    if ((it = a.find("r")) != a.end()) c.r = static_cast<uint8_t>(toInt(it->second));
    if ((it = a.find("g")) != a.end()) c.g = static_cast<uint8_t>(toInt(it->second));
    if ((it = a.find("b")) != a.end()) c.b = static_cast<uint8_t>(toInt(it->second));
    if ((it = a.find("a")) != a.end()) c.a = static_cast<uint8_t>(toInt(it->second));
    return c;
}

bool has(const Attrs& a, const char* key) { return a.find(key) != a.end(); }
std::string get(const Attrs& a, const char* key, const std::string& def = std::string()) {
    auto it = a.find(key);
    return it == a.end() ? def : it->second;
}

/// 実体参照を 1 つ読む（&amp; など）。読めなければ false
bool parseEntity(const std::u16string& s, size_t& i, std::u16string& out) {
    const size_t semi = s.find(u';', i);
    if (semi == std::u16string::npos || semi - i > 10) return false;
    std::string name;
    for (size_t k = i + 1; k < semi; ++k) name += static_cast<char>(s[k]);
    name = toLower(name);
    char16_t c = 0;
    if (name == "lt") c = u'<';
    else if (name == "gt") c = u'>';
    else if (name == "amp") c = u'&';
    else if (name == "quot") c = u'"';
    else if (name == "apos") c = u'\'';
    else if (name.size() > 1 && name[0] == '#') {
        const bool hex = name[1] == 'x';
        const unsigned long n = std::strtoul(name.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
        if (n == 0 || n > 0x10FFFF) return false;
        text::appendCodePoint(out, static_cast<char32_t>(n));
        i = semi + 1;
        return true;
    } else {
        return false;
    }
    out += c;
    i = semi + 1;
    return true;
}

/// 開いているタグ 1 つ分の状態
struct Frame {
    std::string tag;
    TextStyle style;            ///< 入る前のスタイル（閉じたら戻す）
    size_t start = 0;           ///< 本文での開始位置
    // 閉じたときに注記を作るための情報
    AnnotationType annotation = AnnotationType::Ruby;
    bool makeAnnotation = false;
    std::u16string annotationText;
    RubyMode rubyMode = RubyMode::Group;
    EmphasisMark mark = EmphasisMark::Sesame;
    bool oppositeSide = false;
    float scale = 0.5f;
    float offset = 0.0f;
    float jidoriEm = 0.0f;
    int linkIndex = -1;
};

EmphasisMark parseMark(const std::string& s) {
    const std::string v = toLower(s);
    if (v == "opensesame" || v == "white") return EmphasisMark::OpenSesame;
    if (v == "dot") return EmphasisMark::Dot;
    if (v == "circle" || v == "filledcircle") return EmphasisMark::FilledCircle;
    if (v == "opencircle") return EmphasisMark::OpenCircle;
    return EmphasisMark::Sesame;
}

RubyMode parseRubyMode(const std::string& s) {
    const std::string v = toLower(s);
    if (v == "mono") return RubyMode::Mono;
    if (v == "jukugo") return RubyMode::Jukugo;
    return RubyMode::Group;
}

/// 本文を溜めながら、スタイルが変わったところで run に切る
class Builder {
public:
    Builder(Paragraph& para, const TextStyle& base) : para_(para), style_(base) {}

    void setStyle(const TextStyle& s) {
        if (!pending_.empty() && !sameStyle(s, style_)) flush();
        style_ = s;
    }
    const TextStyle& style() const { return style_; }

    void add(const std::u16string& t) {
        pending_ += t;
        pos_ += t.size();
    }
    void add(char16_t c) {
        pending_ += c;
        ++pos_;
    }
    /// 本文には出さずに run を確定する（画像・プレースホルダを挟む前に呼ぶ）
    void flush() {
        if (pending_.empty()) return;
        para_.runs.push_back(InlineRun{pending_, style_});
        pending_.clear();
    }
    void addPlaceholder(Size size, const std::string& id) {
        flush();
        para_.addPlaceholder(size, style_, id);
        ++pos_;     // U+FFFC
    }
    size_t pos() const { return pos_; }

private:
    static bool sameStyle(const TextStyle& a, const TextStyle& b) {
        return a.font.family == b.font.family && a.font.weight == b.font.weight &&
               a.font.italic == b.font.italic && a.size == b.size && a.fill == b.fill &&
               a.stroke.has_value() == b.stroke.has_value() &&
               (!a.stroke || (a.stroke->color == b.stroke->color && a.stroke->width == b.stroke->width)) &&
               a.shadow.has_value() == b.shadow.has_value() &&
               a.layers.size() == b.layers.size() && a.letterSpacing == b.letterSpacing &&
               a.scaleX == b.scaleX && a.scaleY == b.scaleY && a.baselineShift == b.baselineShift &&
               a.underline.has_value() == b.underline.has_value() &&
               a.strikethrough.has_value() == b.strikethrough.has_value() &&
               a.language == b.language && a.features == b.features;
    }

    Paragraph& para_;
    TextStyle style_;
    std::u16string pending_;
    size_t pos_ = 0;
};

} // namespace

//------------------------------------------------------------------------------

TagParseResult parseTaggedText(const std::u16string& text, const TagParseOptions& options) {
    TagParseResult result;
    result.paragraph.style = options.paragraphStyle;
    Builder b(result.paragraph, options.baseStyle);
    std::vector<Frame> stack;

    size_t i = 0;
    while (i < text.size()) {
        const char16_t c = text[i];
        if (c == u'&') {
            std::u16string decoded;
            size_t j = i;
            if (parseEntity(text, j, decoded)) {
                b.add(decoded);
                i = j;
                continue;
            }
            b.add(c);
            ++i;
            continue;
        }
        if (c != u'<') {
            b.add(c);
            ++i;
            continue;
        }

        // --- 閉じタグ ---
        if (i + 1 < text.size() && text[i + 1] == u'/') {
            size_t j = i + 2;
            const std::string name = toLower(parseName(text, j));
            skipSpace(text, j);
            if (name.empty() || j >= text.size() || text[j] != u'>') {
                b.add(c);
                ++i;
                continue;
            }
            ++j;
            // 対応する開きタグを探す（無ければ捨てる）
            auto it = std::find_if(stack.rbegin(), stack.rend(),
                                   [&](const Frame& f) { return f.tag == name; });
            if (it == stack.rend()) {
                result.errors.push_back("unmatched close tag: </" + name + ">");
                i = j;
                continue;
            }
            // 内側の閉じ忘れは記録して一緒に閉じる
            while (&stack.back() != &(*it)) {
                result.errors.push_back("unclosed tag: <" + stack.back().tag + ">");
                stack.pop_back();
            }
            Frame f = stack.back();
            stack.pop_back();
            b.flush();
            b.setStyle(f.style);
            if (f.linkIndex >= 0) result.links[static_cast<size_t>(f.linkIndex)].end = b.pos();
            if (f.makeAnnotation && b.pos() > f.start) {
                Annotation a;
                a.type = f.annotation;
                a.start = f.start;
                a.end = b.pos();
                a.text = f.annotationText;
                a.rubyMode = f.rubyMode;
                a.mark = f.mark;
                a.oppositeSide = f.oppositeSide;
                a.scale = f.scale;
                a.offset = f.offset;
                a.jidoriEm = f.jidoriEm;
                result.paragraph.annotations.push_back(std::move(a));
            }
            i = j;
            continue;
        }

        // --- 開きタグ ---
        size_t j = i + 1;
        const std::string name = toLower(parseName(text, j));
        if (name.empty()) {
            b.add(c);
            ++i;
            continue;
        }
        Attrs attrs;
        bool selfClosing = false;
        bool ok = false;
        while (j < text.size()) {
            skipSpace(text, j);
            if (j < text.size() && text[j] == u'/') {
                selfClosing = true;
                ++j;
                continue;
            }
            if (j < text.size() && text[j] == u'>') {
                ++j;
                ok = true;
                break;
            }
            const std::string key = toLower(parseName(text, j));
            if (key.empty()) break;      // 読めない文字: タグとして扱わない
            skipSpace(text, j);
            std::string value;
            if (j < text.size() && text[j] == u'=') {
                ++j;
                skipSpace(text, j);
                if (j < text.size() && (text[j] == u'"' || text[j] == u'\'')) {
                    const char16_t quote = text[j++];
                    std::u16string raw;
                    while (j < text.size() && text[j] != quote) {
                        if (text[j] == u'&') {
                            std::u16string decoded;
                            size_t k = j;
                            if (parseEntity(text, k, decoded)) { raw += decoded; j = k; continue; }
                        }
                        raw += text[j++];
                    }
                    if (j < text.size()) ++j;
                    value = text::utf16ToUtf8(raw);
                } else {
                    std::u16string raw;
                    while (j < text.size() && text[j] != u'>' && text[j] != u' ' && text[j] != u'\t' &&
                           text[j] != u'/') {
                        raw += text[j++];
                    }
                    value = text::utf16ToUtf8(raw);
                }
            }
            attrs[key] = value;
        }
        if (!ok) {           // '>' が来ない: そのままの文字として扱う
            b.add(c);
            ++i;
            continue;
        }

        TextStyle style = b.style();
        Frame frame;
        frame.tag = name;
        frame.style = b.style();
        frame.start = b.pos();
        bool container = true;      // 閉じタグを待つか

        if (name == "font") {
            if (has(attrs, "size")) style.size = toFloat(get(attrs, "size"));
            if (has(attrs, "weight")) style.font.weight = toInt(get(attrs, "weight"));
            if (has(attrs, "spacing")) style.letterSpacing = toFloat(get(attrs, "spacing"));
            if (has(attrs, "width")) style.scaleX = toFloat(get(attrs, "width"));
            if (has(attrs, "height")) style.scaleY = toFloat(get(attrs, "height"));
            if (has(attrs, "italic")) style.font.italic = get(attrs, "italic") != "0";
            if (has(attrs, "face")) {
                const std::string face = get(attrs, "face");
                auto it = options.namedFamilies.find(face);
                style.font.family = it != options.namedFamilies.end() ? it->second
                                                                      : std::vector<std::string>{face};
            }
            if (has(attrs, "language")) style.language = get(attrs, "language");
        } else if (name == "b" || name == "strong") {
            style.font.weight = 700;
        } else if (name == "i" || name == "em") {
            style.font.italic = true;
        } else if (name == "u") {
            TextDecoration d;
            if (has(attrs, "color")) d.color = attrColor(attrs, Color::rgb(0, 0, 0));
            style.underline = d;
        } else if (name == "s" || name == "strike" || name == "del") {
            TextDecoration d;
            if (has(attrs, "color")) d.color = attrColor(attrs, Color::rgb(0, 0, 0));
            style.strikethrough = d;
        } else if (name == "sup" || name == "sub") {
            style.size *= options.supScale;
            style.baselineShift = (name == "sup") ? options.supOffset : options.subOffset;
        } else if (name == "color") {
            style.fill = attrColor(attrs, style.fill.solid());
        } else if (name == "outline") {
            Stroke s;
            s.color = attrColor(attrs, Color::rgb(0, 0, 0));
            s.width = has(attrs, "width") ? toFloat(get(attrs, "width")) : 2.0f;
            s.join = StrokeJoin::Round;
            if (has(attrs, "add")) {
                // 層を重ねる（二重縁取り）。既存の外観を層にしてから縁取りを下へ足す
                if (style.layers.empty()) {
                    style.layers.push_back(TextLayer{style.fill, style.stroke, Point{}, 0.0f});
                }
                style.layers.insert(style.layers.begin(), TextLayer::outlined(s));
            } else {
                style.stroke = s;
            }
        } else if (name == "shadow") {
            TextShadow sh;
            sh.color = attrColor(attrs, Color::rgba(0, 0, 0, 128));
            sh.offset = Point{has(attrs, "x") ? toFloat(get(attrs, "x")) : 2.0f,
                              has(attrs, "y") ? toFloat(get(attrs, "y")) : 2.0f};
            sh.blur = toFloat(get(attrs, "blur", "0"));
            if (has(attrs, "add") && style.shadow) {
                // 2 枚目以降の影は層として足す
                if (style.layers.empty()) {
                    style.layers.push_back(TextLayer{style.fill, style.stroke, Point{}, 0.0f});
                }
                TextLayer l;
                l.fill = Paint(sh.color);
                l.offset = sh.offset;
                l.blur = sh.blur;
                style.layers.insert(style.layers.begin(), l);
            } else {
                style.shadow = sh;
            }
        } else if (name == "style") {
            auto it = options.namedStyles.find(get(attrs, "name"));
            if (it != options.namedStyles.end()) style = it->second;
            else result.errors.push_back("unknown style: " + get(attrs, "name"));
        } else if (name == "ruby") {
            frame.makeAnnotation = true;
            frame.annotation = AnnotationType::Ruby;
            frame.annotationText = text::utf8ToUtf16(get(attrs, "text"));
            frame.rubyMode = parseRubyMode(get(attrs, "mode"));
            frame.scale = has(attrs, "scale") ? toFloat(get(attrs, "scale")) : 0.5f;
            frame.offset = toFloat(get(attrs, "offset", "0"));
        } else if (name == "emphasis" || name == "dot") {
            frame.makeAnnotation = true;
            frame.annotation = AnnotationType::Emphasis;
            frame.mark = parseMark(get(attrs, "mark"));
            frame.oppositeSide = has(attrs, "opposite");
            frame.scale = has(attrs, "scale") ? toFloat(get(attrs, "scale")) : 0.5f;
            frame.offset = toFloat(get(attrs, "offset", "0"));
        } else if (name == "tcy") {
            frame.makeAnnotation = true;
            frame.annotation = AnnotationType::TateChuYoko;
        } else if (name == "warichu") {
            frame.makeAnnotation = true;
            frame.annotation = AnnotationType::Warichu;
            frame.annotationText = text::utf8ToUtf16(get(attrs, "text"));
            frame.scale = has(attrs, "scale") ? toFloat(get(attrs, "scale")) : 0.5f;
        } else if (name == "jidori") {
            frame.makeAnnotation = true;
            frame.annotation = AnnotationType::Jidori;
            frame.jidoriEm = toFloat(get(attrs, "em", "0"));
        } else if (name == "link") {
            TagLink li;
            li.name = get(attrs, "name");
            li.start = b.pos();
            li.end = b.pos();
            result.links.push_back(std::move(li));
            frame.linkIndex = static_cast<int>(result.links.size()) - 1;
        } else if (name == "br") {
            b.add(u'\n');
            container = false;
        } else if (name == "sp") {
            const int n = has(attrs, "width") ? std::max(1, toInt(get(attrs, "width"))) : 1;
            for (int k = 0; k < n; ++k) b.add(u' ');
            container = false;
        } else if (name == "graph") {
            const Pt em = style.size;
            Size size{toFloat(get(attrs, "width", "0")), toFloat(get(attrs, "height", "0"))};
            if (size.w <= 0.0f) size.w = em * options.graphDefaultSize;
            if (size.h <= 0.0f) size.h = em * options.graphDefaultSize;
            TagPlaceholder ph;
            ph.name = get(attrs, "name");
            ph.charIndex = b.pos();
            ph.size = size;
            b.addPlaceholder(size, ph.name);
            result.placeholders.push_back(std::move(ph));
            container = false;
        } else if (name == "start" || name == "delay" || name == "wait" || name == "sync" ||
                   name == "keywait") {
            TagMarker m;
            m.kind = name;
            m.value = get(attrs, "value");
            if (m.value.empty() && has(attrs, "diff")) m.value = get(attrs, "diff");
            if (m.value.empty() && has(attrs, "all")) m.value = get(attrs, "all");
            m.charIndex = b.pos();
            result.markers.push_back(std::move(m));
            container = false;
        } else if (name == "eval") {
            std::u16string shown;
            const std::string evalName = get(attrs, "name");
            if (options.evaluate) shown = options.evaluate(evalName);
            if (shown.empty()) shown = text::utf8ToUtf16(get(attrs, "alt"));
            if (shown.empty()) shown = text::utf8ToUtf16(evalName);
            b.add(shown);
            container = false;
        } else {
            result.errors.push_back("unknown tag: <" + name + ">");
            if (options.keepUnknownTags) {
                for (size_t k = i; k < j; ++k) b.add(text[k]);
            }
            i = j;
            continue;
        }

        if (container && !selfClosing) {
            b.setStyle(style);
            stack.push_back(std::move(frame));
        } else if (container && selfClosing) {
            // <ruby text="..."/> のような自己閉じは範囲を持たないので、スタイルだけ変えて閉じる
            b.setStyle(style);
            b.flush();
            b.setStyle(frame.style);
        }
        i = j;
    }

    while (!stack.empty()) {
        Frame f = stack.back();
        stack.pop_back();
        result.errors.push_back("unclosed tag: <" + f.tag + ">");
        b.flush();
        b.setStyle(f.style);
        if (f.linkIndex >= 0) result.links[static_cast<size_t>(f.linkIndex)].end = b.pos();
        if (f.makeAnnotation && b.pos() > f.start) {
            Annotation a;
            a.type = f.annotation;
            a.start = f.start;
            a.end = b.pos();
            a.text = f.annotationText;
            a.rubyMode = f.rubyMode;
            a.mark = f.mark;
            a.oppositeSide = f.oppositeSide;
            a.scale = f.scale;
            a.offset = f.offset;
            a.jidoriEm = f.jidoriEm;
            result.paragraph.annotations.push_back(std::move(a));
        }
    }
    b.flush();
    return result;
}

std::u16string stripTags(const std::u16string& text) {
    TagParseOptions opts;
    return parseTaggedText(text, opts).paragraph.text();
}

} // namespace typeset::inl
