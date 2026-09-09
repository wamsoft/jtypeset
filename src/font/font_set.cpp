/**
 * font_set.cpp — フォント層
 */

#include "typeset/font/font_set.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include <glyphware/Blob.h>
#include <hb.h>
#include <hb-ot.h>

#include "typeset/style.hpp"

namespace typeset::font {

FontSet::FontSet() = default;

FontSet::~FontSet() {
    for (auto& kv : hbFonts_) hb_font_destroy(kv.second);
}

std::shared_ptr<glyphware::Face> FontSet::loadFile(const std::string& path,
                                                   const std::string& key,
                                                   int faceIndex) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return nullptr;
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return nullptr;

    const std::string k = key.empty() ? path : key;
    auto blob = std::make_shared<glyphware::OwnedFontBlob>(std::move(bytes));
    auto face = glyphware::Face::open(blob, k, faceIndex);
    if (!face) return nullptr;
    faces_[k] = face;
    return face;
}

std::shared_ptr<glyphware::Face> FontSet::loadMemory(const std::string& key,
                                                     const void* data, size_t size,
                                                     int faceIndex) {
    if (!data || size == 0) return nullptr;
    auto blob = std::make_shared<glyphware::OwnedFontBlob>(data, size);
    auto face = glyphware::Face::open(blob, key, faceIndex);
    if (!face) return nullptr;
    faces_[key] = face;
    return face;
}

std::shared_ptr<glyphware::Face> FontSet::find(const std::string& keyOrFamily) const {
    auto it = faces_.find(keyOrFamily);
    if (it != faces_.end()) return it->second;
    for (const auto& kv : faces_) {
        const glyphware::FontDescriptor& d = kv.second->descriptor();
        if (d.family == keyOrFamily || d.typographicFamily == keyOrFamily ||
            d.fullName == keyOrFamily || d.postScriptName == keyOrFamily) {
            return kv.second;
        }
    }
    return nullptr;
}

std::shared_ptr<glyphware::Face> FontSet::resolve(const FontSpec& spec, char32_t cp) const {
    std::shared_ptr<glyphware::Face> first;
    for (const std::string& name : spec.family) {
        auto face = find(name);
        if (!face) continue;
        if (!first) first = face;
        if (face->covers(cp)) return face;
    }
    if (!first && !faces_.empty()) first = faces_.begin()->second;
    return first;
}

std::shared_ptr<glyphware::Face> FontSet::primary(const FontSpec& spec) const {
    for (const std::string& name : spec.family) {
        auto face = find(name);
        if (face) return face;
    }
    return faces_.empty() ? nullptr : faces_.begin()->second;
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
