#ifndef TYPESET_FONT_FONT_SET_HPP
#define TYPESET_FONT_FONT_SET_HPP

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glyphware/Face.h>

#include "typeset/geom.hpp"

struct hb_font_t;

namespace typeset { struct FontSpec; }

/**
 * font — フォント層（glyphware の薄い包み）
 *
 *  - face を開く（loadFile / loadMemory）か、メタデータだけ宣言して初回使用時に開く（declare）
 *  - 同じ family に複数の face（ウェイト・斜体）を登録でき、FontSpec の weight / italic に最も近い face を選ぶ
 *    （CSS Fonts の font-matching と同じ規則。無ければ通常の face に落ち、組版層がフェイクボールド／斜体にする）
 *  - FontSpec（family のフォールバック列）と文字から face を解決する。言語 → family の置換表
 *    （setLanguageFonts）と宣言の languages により、言語の付いた文字は先にその言語のフォントを試す
 *  - シェイピング用の hb_font_t を face ごとに 1 つ持つ。glyphware の Face::hb() は
 *    hb-ft（FT のピクセルサイズに縛られる）なので使わず、フォントのバイト列から
 *    OT funcs の hb_font を作り、スケールを unitsPerEm にして**フォントユニットで**
 *    位置を受け取る。vmtx / VORG による縦メトリクスもこの経路で効く
 */
namespace typeset::font {

/**
 * フォントの宣言（開かずに登録するためのメタデータ）
 *
 * family / weight / italic / languages / ranges は開かなくても分かる情報。開いたあとは name テーブルの family 名も
 * 引けるようになる。weight = 0 と italic = nullopt は「開いたときにフォントから取る」（開くまでは 400 / 非斜体）
 */
struct FontDeclaration {
    std::string key;                        ///< 一意なキー（TextStyle の family に書く名前）
    std::string path;                       ///< ファイル（初回使用時に開く）
    int faceIndex = 0;
    std::vector<std::string> family;        ///< 別名（同じ family の別ウェイトは同じ名前を書く）
    int weight = 0;                         ///< 100〜900。0 でフォントから
    std::optional<bool> italic;             ///< nullopt でフォントから
    std::vector<std::string> languages;     ///< BCP47。この言語のテキストで先に試される
    std::vector<glyphware::CodepointRange> ranges;  ///< カバレッジ（空なら開いて cmap を見る）
};

class FontSet {
public:
    FontSet();
    ~FontSet();

    FontSet(const FontSet&) = delete;
    FontSet& operator=(const FontSet&) = delete;

    /**
     * ファイルから開く。key を省略するとパスがキーになる。family / weight / italic は name・OS/2 から
     * @return 失敗時 nullptr
     */
    std::shared_ptr<glyphware::Face> loadFile(const std::string& path,
                                              const std::string& key = std::string(),
                                              int faceIndex = 0);

    /// メモリから開く（バイト列はコピーして保持する）
    std::shared_ptr<glyphware::Face> loadMemory(const std::string& key,
                                                const void* data, size_t size,
                                                int faceIndex = 0);

    /**
     * 開かずに宣言する。同じキーがあれば置き換える。ファイルは初回使用時に開く（無ければそのとき失敗し、
     * 以後は無いものとして扱う）
     * @return キーが空なら false
     */
    bool declare(FontDeclaration decl);

    /// 登録済みか（開いているかどうかは問わない）
    bool has(const std::string& key) const;
    /// 開いているか（declare しただけなら false。開くのに失敗したものも false）
    bool isLoaded(const std::string& key) const;
    /// 登録したキー（登録順）
    std::vector<std::string> keys() const;

    /// 登録済み face（キー、または family 名で。通常ウェイト・非斜体を優先。無ければ nullptr）。必要なら開く
    std::shared_ptr<glyphware::Face> find(const std::string& keyOrFamily);

    /// キー／family 名と weight / italic に最も近い face。必要なら開く。
    /// バリアブルフォントなら wght 軸を weight に合わせたインスタンス（variations に他の軸も指定できる）
    std::shared_ptr<glyphware::Face> select(const std::string& keyOrFamily, int weight, bool italic,
                                            const std::map<std::string, float>& variations = {});

    /**
     * バリアブルフォントのインスタンス（軸の値を固定した別の Face）。同じ base と座標なら同じ Face を返す。
     * 軸の無い face、座標が空ならそのまま base
     */
    std::shared_ptr<glyphware::Face> instance(const std::shared_ptr<glyphware::Face>& base,
                                              const std::vector<glyphware::VarCoord>& coords);

    /**
     * FontSpec の family 列（language の置換表・宣言の languages が先）を順に見て、cp を持つ最初の face を返す。
     * family ごとに weight / italic の最近傍を選ぶ。
     * どれも持たなければ最初に見つかった face（.notdef が出る）。1 つも解決できなければ nullptr
     */
    std::shared_ptr<glyphware::Face> resolve(const FontSpec& spec, char32_t cp,
                                             const std::string& language = std::string(),
                                             int colorPreference = 0);

    /// FontSpec の第一候補（行のメトリクス基準に使う。言語の置換は見ない）
    std::shared_ptr<glyphware::Face> primary(const FontSpec& spec);

    /**
     * 言語 → 先に試す family の列。"zh-Hans" のように地域・用字系まで書いた言語は、その完全一致が無ければ
     * 主言語（"zh"）の表を使う。families が空なら削除
     */
    void setLanguageFonts(const std::string& language, std::vector<std::string> families);
    /// language に対応する family 列（置換表＋宣言の languages にその言語を持つキー）。無ければ空
    std::vector<std::string> languageFonts(const std::string& language) const;

    /**
     * シェイピング用 hb_font（フォントユニットスケール）。FontSet が所有する
     */
    hb_font_t* hbFont(const glyphware::Face& face);

    /// 登録した数（宣言だけのものも含む）
    size_t size() const { return entries_.size(); }

private:
    struct Entry {
        FontDeclaration decl;
        std::shared_ptr<glyphware::Face> face;
        bool failed = false;                    ///< 開こうとして失敗した
        std::vector<std::string> names;         ///< 開いたあとの name テーブル由来の名前
        int weight = 400;                       ///< 実効（宣言、または開いた face の値）
        bool italic = false;
        bool matches(const std::string& name) const;
        bool languageMatches(const std::string& language) const;
    };

    Entry* entryFor(const std::string& key);
    void adopt(Entry& e, std::shared_ptr<glyphware::Face> face);
    bool ensureLoaded(Entry& e);
    bool covers(Entry& e, char32_t cp);
    /// name に合う登録から weight / italic の最近傍（開かない）。無ければ nullptr
    Entry* best(const std::string& name, int weight, bool italic);
    std::shared_ptr<glyphware::Face> faceOf(Entry* e);

    /// spec の weight / italic / variations を face の軸に写す（軸が無ければ base）
    std::shared_ptr<glyphware::Face> withVariations(const std::shared_ptr<glyphware::Face>& base,
                                                    int weight, bool italic,
                                                    const std::map<std::string, float>& variations);

    std::vector<std::unique_ptr<Entry>> entries_;
    std::map<std::string, std::vector<std::string>> languageFonts_;
    std::map<const glyphware::Face*, hb_font_t*> hbFonts_;
    /// インスタンスのキャッシュ: base の Face* ＋ 座標 → Face。base は instances_ の Face が blob 経由で保持する
    std::map<std::pair<const glyphware::Face*, std::string>, std::shared_ptr<glyphware::Face>> instances_;
};

/// OpenType タグ（"wght"）を glyphware / HarfBuzz の 32bit タグに
inline uint32_t makeTag(const std::string& s) {
    uint32_t t = 0;
    for (size_t i = 0; i < 4; ++i) t = (t << 8) | static_cast<uint8_t>(i < s.size() ? s[i] : ' ');
    return t;
}

/// face の実効ウェイト。バリアブルフォントのインスタンスなら wght 軸の値、無ければ OS/2 の値
int effectiveWeight(const glyphware::Face& face);

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
