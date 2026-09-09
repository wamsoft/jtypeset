/**
 * font_set.cpp — フォント層
 */

#include "typeset/font/font_set.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

#include <glyphware/Blob.h>
#include <hb.h>
#include <hb-ot.h>

#include "typeset/style.hpp"

namespace typeset::font {

namespace {

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// BCP47 の主言語（"zh-Hans" → "zh"）
std::string primaryLanguage(const std::string& language) {
    const size_t p = language.find_first_of("-_");
    return lowerAscii(p == std::string::npos ? language : language.substr(0, p));
}

bool sameLanguage(const std::string& a, const std::string& b) {
    return lowerAscii(a) == lowerAscii(b);
}

/**
 * CSS Fonts 4 の font-weight のマッチング順。小さいほど望ましい。
 *  - 400〜500 が望みなら、まず 500 までの上、次に下、最後に 500 より上
 *  - 400 未満なら下から、500 より上なら上から
 */
int weightDistance(int desired, int actual) {
    if (actual == desired) return 0;
    if (desired >= 400 && desired <= 500) {
        if (actual > desired && actual <= 500) return actual - desired;              // 1〜100
        if (actual < desired) return 200 + (desired - actual);                       // 200〜
        return 1000 + (actual - desired);                                            // 500 より上
    }
    if (desired < 400) {
        if (actual < desired) return desired - actual;
        return 1000 + (actual - desired);
    }
    if (actual > desired) return actual - desired;
    return 1000 + (desired - actual);
}

/// 開いた Face のバイト列を指す blob（インスタンスの Face が base を生かしておく）
class ViewBlob final : public glyphware::FontBlob {
public:
    explicit ViewBlob(std::shared_ptr<glyphware::Face> owner) : owner_(std::move(owner)) {}
    const std::uint8_t* data() const noexcept override { return owner_->data(); }
    std::size_t size() const noexcept override { return owner_->size(); }
private:
    std::shared_ptr<glyphware::Face> owner_;
};

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

} // namespace

//------------------------------------------------------------------------------

bool FontSet::Entry::matches(const std::string& name) const {
    if (decl.key == name) return true;
    for (const std::string& f : decl.family) if (f == name) return true;
    for (const std::string& f : names) if (f == name) return true;
    return false;
}

bool FontSet::Entry::languageMatches(const std::string& language) const {
    if (language.empty()) return false;
    const std::string prim = primaryLanguage(language);
    for (const std::string& l : decl.languages) {
        if (sameLanguage(l, language)) return true;
        if (primaryLanguage(l) == prim && l.find_first_of("-_") == std::string::npos) return true;
    }
    return false;
}

FontSet::FontSet() = default;

FontSet::~FontSet() {
    for (auto& kv : hbFonts_) hb_font_destroy(kv.second);
}

FontSet::Entry* FontSet::entryFor(const std::string& key) {
    for (auto& e : entries_) if (e->decl.key == key) return e.get();
    return nullptr;
}

void FontSet::adopt(Entry& e, std::shared_ptr<glyphware::Face> face) {
    e.face = std::move(face);
    e.failed = false;
    const glyphware::FontDescriptor& d = e.face->descriptor();
    e.names.clear();
    for (const std::string* n : {&d.family, &d.typographicFamily, &d.fullName, &d.postScriptName}) {
        if (!n->empty()) e.names.push_back(*n);
    }
    if (e.decl.weight > 0) e.weight = e.decl.weight;
    else e.weight = static_cast<int>(d.weight) > 0 ? static_cast<int>(d.weight) : (d.bold ? 700 : 400);
    e.italic = e.decl.italic.has_value() ? *e.decl.italic : (d.slant != glyphware::Slant::Normal);
}

std::shared_ptr<glyphware::Face> FontSet::loadFile(const std::string& path,
                                                   const std::string& key,
                                                   int faceIndex) {
    std::string bytes = readFile(path);
    if (bytes.empty()) return nullptr;
    const std::string k = key.empty() ? path : key;
    auto blob = std::make_shared<glyphware::OwnedFontBlob>(std::move(bytes));
    auto face = glyphware::Face::open(blob, k, faceIndex);
    if (!face) return nullptr;

    Entry* e = entryFor(k);
    if (!e) {
        entries_.push_back(std::make_unique<Entry>());
        e = entries_.back().get();
    }
    e->decl = FontDeclaration{};
    e->decl.key = k;
    e->decl.path = path;
    e->decl.faceIndex = faceIndex;
    adopt(*e, face);
    return face;
}

std::shared_ptr<glyphware::Face> FontSet::loadMemory(const std::string& key,
                                                     const void* data, size_t size,
                                                     int faceIndex) {
    if (!data || size == 0) return nullptr;
    auto blob = std::make_shared<glyphware::OwnedFontBlob>(data, size);
    auto face = glyphware::Face::open(blob, key, faceIndex);
    if (!face) return nullptr;

    Entry* e = entryFor(key);
    if (!e) {
        entries_.push_back(std::make_unique<Entry>());
        e = entries_.back().get();
    }
    e->decl = FontDeclaration{};
    e->decl.key = key;
    e->decl.faceIndex = faceIndex;
    adopt(*e, face);
    return face;
}

bool FontSet::declare(FontDeclaration decl) {
    if (decl.key.empty()) return false;
    Entry* e = entryFor(decl.key);
    if (!e) {
        entries_.push_back(std::make_unique<Entry>());
        e = entries_.back().get();
    }
    e->face = nullptr;
    e->failed = false;
    e->names.clear();
    e->weight = decl.weight > 0 ? decl.weight : 400;
    e->italic = decl.italic.value_or(false);
    e->decl = std::move(decl);
    return true;
}

bool FontSet::has(const std::string& key) const {
    for (const auto& e : entries_) if (e->decl.key == key) return true;
    return false;
}

bool FontSet::isLoaded(const std::string& key) const {
    for (const auto& e : entries_) if (e->decl.key == key) return static_cast<bool>(e->face);
    return false;
}

std::vector<std::string> FontSet::keys() const {
    std::vector<std::string> out;
    for (const auto& e : entries_) out.push_back(e->decl.key);
    return out;
}

bool FontSet::ensureLoaded(Entry& e) {
    if (e.face) return true;
    if (e.failed) return false;
    std::string bytes = e.decl.path.empty() ? std::string() : readFile(e.decl.path);
    if (bytes.empty()) {
        e.failed = true;
        return false;
    }
    auto blob = std::make_shared<glyphware::OwnedFontBlob>(std::move(bytes));
    auto face = glyphware::Face::open(blob, e.decl.key, e.decl.faceIndex);
    if (!face) {
        e.failed = true;
        return false;
    }
    adopt(e, face);
    return true;
}

bool FontSet::covers(Entry& e, char32_t cp) {
    if (!e.decl.ranges.empty()) {
        for (const glyphware::CodepointRange& r : e.decl.ranges) {
            if (cp >= r.lo && cp <= r.hi) return ensureLoaded(e);
        }
        return false;
    }
    if (!ensureLoaded(e)) return false;
    return e.face->covers(cp);
}

FontSet::Entry* FontSet::best(const std::string& name, int weight, bool italic) {
    Entry* bestEntry = nullptr;
    int bestScore = 0;
    for (auto& up : entries_) {
        Entry& e = *up;
        if (e.failed || !e.matches(name)) continue;
        // 斜体の一致を最優先し、次にウェイトの近さ
        const int score = (e.italic == italic ? 0 : 100000) + weightDistance(weight, e.weight);
        if (!bestEntry || score < bestScore) {
            bestEntry = &e;
            bestScore = score;
        }
    }
    return bestEntry;
}

std::shared_ptr<glyphware::Face> FontSet::faceOf(Entry* e) {
    if (!e) return nullptr;
    return ensureLoaded(*e) ? e->face : nullptr;
}

std::shared_ptr<glyphware::Face> FontSet::find(const std::string& keyOrFamily) {
    return select(keyOrFamily, 400, false);
}

std::shared_ptr<glyphware::Face> FontSet::select(const std::string& keyOrFamily, int weight, bool italic,
                                                 const std::map<std::string, float>& variations) {
    // 開けないものが混ざっていても、次に近いものへ落ちる
    for (;;) {
        Entry* e = best(keyOrFamily, weight, italic);
        if (!e) return nullptr;
        if (auto face = faceOf(e)) return withVariations(face, weight, italic, variations);
    }
}

std::shared_ptr<glyphware::Face> FontSet::instance(const std::shared_ptr<glyphware::Face>& base,
                                                   const std::vector<glyphware::VarCoord>& coords) {
    if (!base || coords.empty() || base->descriptor().axes.empty()) return base;
    // 既定値と同じ座標だけなら base のまま
    std::vector<glyphware::VarCoord> effective;
    for (const glyphware::VarCoord& c : coords) {
        float mn = 0.0f, def = 0.0f, mx = 0.0f;
        if (!base->axisRange(c.tag, mn, def, mx)) continue;
        const float v = std::max(mn, std::min(mx, c.value));
        if (v != def) effective.push_back(glyphware::VarCoord{c.tag, v});
    }
    if (effective.empty()) return base;
    std::sort(effective.begin(), effective.end(),
              [](const glyphware::VarCoord& a, const glyphware::VarCoord& b) { return a.tag < b.tag; });
    std::string key;
    for (const glyphware::VarCoord& c : effective) key += std::to_string(c.tag) + "=" + std::to_string(c.value) + ";";
    // base 自身がインスタンスなら、その元を base にする（blob は共有）
    auto it = instances_.find({base.get(), key});
    if (it != instances_.end()) return it->second;
    auto blob = std::make_shared<ViewBlob>(base);
    auto face = glyphware::Face::open(blob, base->descriptor().key, base->faceIndex());
    if (!face) return base;
    face->setVariations(effective);
    instances_.emplace(std::make_pair(base.get(), key), face);
    return face;
}

std::shared_ptr<glyphware::Face> FontSet::withVariations(const std::shared_ptr<glyphware::Face>& base,
                                                         int weight, bool italic,
                                                         const std::map<std::string, float>& variations) {
    if (!base || base->descriptor().axes.empty()) return base;
    std::vector<glyphware::VarCoord> coords;
    bool hasWght = false, hasItal = false;
    for (const auto& kv : variations) {
        const uint32_t tag = makeTag(kv.first);
        if (tag == makeTag("wght")) hasWght = true;
        if (tag == makeTag("ital")) hasItal = true;
        coords.push_back(glyphware::VarCoord{tag, kv.second});
    }
    float mn = 0.0f, def = 0.0f, mx = 0.0f;
    if (!hasWght && base->axisRange(makeTag("wght"), mn, def, mx)) {
        coords.push_back(glyphware::VarCoord{makeTag("wght"), static_cast<float>(weight)});
    }
    if (!hasItal && italic && base->axisRange(makeTag("ital"), mn, def, mx)) {
        coords.push_back(glyphware::VarCoord{makeTag("ital"), 1.0f});
    }
    return instance(base, coords);
}

int effectiveWeight(const glyphware::Face& face) {
    for (const glyphware::VarCoord& c : face.variations()) {
        if (c.tag == makeTag("wght")) return static_cast<int>(std::lround(c.value));
    }
    const glyphware::FontDescriptor& d = face.descriptor();
    return static_cast<int>(d.weight) > 0 ? static_cast<int>(d.weight) : (d.bold ? 700 : 400);
}

void FontSet::setLanguageFonts(const std::string& language, std::vector<std::string> families) {
    const std::string key = lowerAscii(language);
    if (families.empty()) languageFonts_.erase(key);
    else languageFonts_[key] = std::move(families);
}

std::vector<std::string> FontSet::languageFonts(const std::string& language) const {
    std::vector<std::string> out;
    if (language.empty()) return out;
    auto it = languageFonts_.find(lowerAscii(language));
    if (it == languageFonts_.end()) it = languageFonts_.find(primaryLanguage(language));
    if (it != languageFonts_.end()) out = it->second;
    for (const auto& e : entries_) {
        if (e->languageMatches(language) &&
            std::find(out.begin(), out.end(), e->decl.key) == out.end()) {
            out.push_back(e->decl.key);
        }
    }
    return out;
}

std::shared_ptr<glyphware::Face> FontSet::resolve(const FontSpec& spec, char32_t cp,
                                                  const std::string& language, int colorPreference) {
    std::shared_ptr<glyphware::Face> first;
    std::shared_ptr<glyphware::Face> secondChoice;   // 色の好みに合わないが cp を持つ face
    auto tryName = [&](const std::string& name) -> std::shared_ptr<glyphware::Face> {
        // 同じ名前の登録を近い順に見て、cp を持つ最初のものを返す。カバレッジは宣言の ranges があれば開かずに判定
        std::vector<Entry*> seen;
        for (;;) {
            Entry* e = nullptr;
            int bestScore = 0;
            for (auto& up : entries_) {
                Entry& c = *up;
                if (c.failed || !c.matches(name)) continue;
                if (std::find(seen.begin(), seen.end(), &c) != seen.end()) continue;
                const int score = (c.italic == spec.italic ? 0 : 100000) + weightDistance(spec.weight, c.weight);
                if (!e || score < bestScore) { e = &c; bestScore = score; }
            }
            if (!e) return nullptr;
            seen.push_back(e);
            if (covers(*e, cp)) {
                auto face = withVariations(e->face, spec.weight, spec.italic, spec.variations);
                // 絵文字の表示形式: カラーフォントを優先／後回しにする（好みに合わなければ控えて次の family へ）
                if (colorPreference != 0) {
                    const bool color = face->descriptor().color;
                    const bool wanted = colorPreference > 0;
                    if (color != wanted) {
                        if (!secondChoice) secondChoice = face;
                        continue;
                    }
                }
                return face;
            }
            if (!first && e->face) first = withVariations(e->face, spec.weight, spec.italic, spec.variations);
        }
    };
    for (const std::string& name : languageFonts(language)) {
        if (auto face = tryName(name)) return face;
    }
    for (const std::string& name : spec.family) {
        if (auto face = tryName(name)) return face;
    }
    if (secondChoice) return secondChoice;
    if (first) return first;
    // どの名前も無い: 開けるものの先頭
    for (auto& up : entries_) {
        if (auto face = faceOf(up.get())) return face;
    }
    return nullptr;
}

std::shared_ptr<glyphware::Face> FontSet::primary(const FontSpec& spec) {
    for (const std::string& name : spec.family) {
        if (auto face = select(name, spec.weight, spec.italic, spec.variations)) return face;
    }
    for (auto& up : entries_) {
        if (auto face = faceOf(up.get())) return face;
    }
    return nullptr;
}

hb_font_t* FontSet::hbFont(const glyphware::Face& face) {
    auto it = hbFonts_.find(&face);
    if (it != hbFonts_.end()) return it->second;

    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(face.data()),
                                     static_cast<unsigned int>(face.size()),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* hbFace = hb_face_create(blob, static_cast<unsigned int>(face.faceIndex()));
    hb_blob_destroy(blob);
    hb_font_t* font = hb_font_create(hbFace);
    hb_face_destroy(hbFace);

    // フォントユニットのまま受け取る（サイズは組版層が掛ける）
    const int upem = static_cast<int>(unitsPerEm(face));
    hb_font_set_scale(font, upem, upem);
    hb_ot_font_set_funcs(font);

    // バリアブルフォントの軸座標を face の状態に合わせる
    const std::vector<glyphware::VarCoord> coords = face.variations();
    if (!coords.empty()) {
        std::vector<hb_variation_t> vars;
        for (const auto& c : coords) {
            hb_variation_t v;
            v.tag = c.tag;
            v.value = c.value;
            vars.push_back(v);
        }
        hb_font_set_variations(font, vars.data(), static_cast<unsigned int>(vars.size()));
    }

    hbFonts_.emplace(&face, font);
    return font;
}

float unitsPerEm(const glyphware::Face& face) {
    const float upem = face.lineMetrics().unitsPerEm;
    return upem > 0.0f ? upem : 1000.0f;
}

void faceAscentDescent(const glyphware::Face& face, Pt size, Pt& ascent, Pt& descent) {
    const glyphware::LineMetrics lm = face.lineMetrics();
    const float upem = unitsPerEm(face);
    ascent = lm.ascenderUnits / upem * size;
    descent = -lm.descenderUnits / upem * size;
    if (ascent <= 0.0f && descent <= 0.0f) {
        ascent = size * 0.88f;
        descent = size * 0.12f;
    }
}

DecorationMetrics decorationMetrics(FontSet& fonts, const glyphware::Face& face, Pt size) {
    DecorationMetrics m;
    m.underlineOffset = size * 0.1f;
    m.underlineThickness = size * 0.05f;
    m.strikeoutOffset = -size * 0.3f;
    m.strikeoutThickness = size * 0.05f;
    hb_font_t* font = fonts.hbFont(face);
    if (!font) return m;
    const float upem = unitsPerEm(face);
    // hb_ot_metrics は y-up（下線の位置は負）。block 軸（下が正）に合わせて符号を反転する
    hb_position_t v = 0;
    if (hb_ot_metrics_get_position(font, HB_OT_METRICS_TAG_UNDERLINE_OFFSET, &v) && v != 0) {
        m.underlineOffset = -static_cast<float>(v) / upem * size;
    }
    if (hb_ot_metrics_get_position(font, HB_OT_METRICS_TAG_UNDERLINE_SIZE, &v) && v > 0) {
        m.underlineThickness = static_cast<float>(v) / upem * size;
    }
    if (hb_ot_metrics_get_position(font, HB_OT_METRICS_TAG_STRIKEOUT_OFFSET, &v) && v != 0) {
        m.strikeoutOffset = -static_cast<float>(v) / upem * size;
    }
    if (hb_ot_metrics_get_position(font, HB_OT_METRICS_TAG_STRIKEOUT_SIZE, &v) && v > 0) {
        m.strikeoutThickness = static_cast<float>(v) / upem * size;
    }
    return m;
}

} // namespace typeset::font
