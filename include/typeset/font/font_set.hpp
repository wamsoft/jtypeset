#ifndef TYPESET_FONT_FONT_SET_HPP
#define TYPESET_FONT_FONT_SET_HPP

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <glyphware/Face.h>

#include "typeset/geom.hpp"

struct hb_font_t;

namespace typeset { struct FontSpec; }

/**
 * font — フォント層（glyphware の薄い包み）
 *
 *  - face を開いてキーで引く
 *  - FontSpec（family のフォールバック列）と文字から face を解決する
 *  - シェイピング用の hb_font_t を face ごとに 1 つ持つ。glyphware の Face::hb() は
 *    hb-ft（FT のピクセルサイズに縛られる）なので使わず、フォントのバイト列から
 *    OT funcs の hb_font を作り、スケールを unitsPerEm にして**フォントユニットで**
 *    位置を受け取る。vmtx / VORG による縦メトリクスもこの経路で効く
 */
namespace typeset::font {

class FontSet {
public:
    FontSet();
    ~FontSet();

    FontSet(const FontSet&) = delete;
    FontSet& operator=(const FontSet&) = delete;

    /**
     * ファイルから開く。key を省略するとパスがキーになる
     * @return 失敗時 nullptr
     */
    std::shared_ptr<glyphware::Face> loadFile(const std::string& path,
                                              const std::string& key = std::string(),
                                              int faceIndex = 0);

    /// メモリから開く（バイト列はコピーして保持する）
    std::shared_ptr<glyphware::Face> loadMemory(const std::string& key,
                                                const void* data, size_t size,
                                                int faceIndex = 0);

    /// 登録済み face（キー、または family 名で。無ければ nullptr）
    std::shared_ptr<glyphware::Face> find(const std::string& keyOrFamily) const;

    /**
     * FontSpec の family 列を順に見て、cp を持つ最初の face を返す。
     * どれも持たなければ最初に見つかった face（.notdef が出る）。
     * 1 つも解決できなければ nullptr
     */
    std::shared_ptr<glyphware::Face> resolve(const FontSpec& spec, char32_t cp) const;

    /// FontSpec の第一候補（行のメトリクス基準に使う）
    std::shared_ptr<glyphware::Face> primary(const FontSpec& spec) const;

    /**
     * シェイピング用 hb_font（フォントユニットスケール）。FontSet が所有する
     */
    hb_font_t* hbFont(const glyphware::Face& face);

    size_t size() const { return faces_.size(); }

private:
    std::map<std::string, std::shared_ptr<glyphware::Face>> faces_;
    std::map<const glyphware::Face*, hb_font_t*> hbFonts_;
};

/// face の unitsPerEm（取れなければ 1000）
float unitsPerEm(const glyphware::Face& face);

/// face の ascender / descender（正・正、pt）を size で返す
void faceAscentDescent(const glyphware::Face& face, Pt size, Pt& ascent, Pt& descent);

/**
 * 下線・打消し線のメトリクス（pt、size で換算）。位置はベースラインからの距離で下が正
 * （横組みの block 軸と同じ向き。打消し線は通常負）
 */
struct DecorationMetrics {
    Pt underlineOffset = 0.0f;
    Pt underlineThickness = 0.0f;
    Pt strikeoutOffset = 0.0f;
    Pt strikeoutThickness = 0.0f;
};

/// post / OS/2 から取る（hb_ot_metrics）。無ければ下線 0.1em / 太さ 0.05em、打消し線 -0.3em
DecorationMetrics decorationMetrics(FontSet& fonts, const glyphware::Face& face, Pt size);

} // namespace typeset::font

#endif // TYPESET_FONT_FONT_SET_HPP
