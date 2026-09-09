#ifndef TYPESET_STYLE_HPP
#define TYPESET_STYLE_HPP

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "typeset/geom.hpp"
#include "typeset/writing_mode.hpp"

/**
 * style — 文字・段落のスタイル
 *
 * CSS エンジンにはしない。明示フィールドの構造体で、継承は親からのコピー。
 */
namespace typeset {

/**
 * フォントの論理指定
 */
struct FontSpec {
    /// フォールバック順の family（FontSet に登録したキー、または family 名）
    std::vector<std::string> family;
    int weight = 400;
    bool italic = false;
    /// バリアブルフォントの軸の値（"wght" → 700、"wdth" → 75 など、デザイン座標）。
    /// wght を書かなくても、face に wght 軸があれば weight が入る。軸の無い face では無視される
    std::map<std::string, float> variations;
};

/**
 * 文字の外観の 1 層（塗り・縁取り・影）
 *
 * TextStyle::layers に下から上の順で並べる。1 層は同じグリフを offset だけずらし、blur でぼかして
 * fill（塗り）と stroke（縁取り）で描く。塗りだけ／縁取りだけの層も作れる（二重縁取り = 太い縁取りの層＋細い縁取りの層＋塗りの層）
 */
struct TextLayer {
    std::optional<Paint> fill;
    std::optional<Stroke> stroke;
    /// ずらし（pt、物理座標。右・下が正。縦組みでも同じ向き）
    Point offset;
    /// ぼかし半径（pt）。ラスタと SVG はガウスぼかし、PDF はぼかさずに置く
    Pt blur = 0.0f;

    static TextLayer filled(Paint c) { TextLayer l; l.fill = std::move(c); return l; }
    static TextLayer outlined(Stroke s) { TextLayer l; l.stroke = s; return l; }
};

/**
 * 影（TextStyle::shadow）。層の一番下に「塗り = color、offset、blur」の 1 層として置く糖衣
 */
struct TextShadow {
    Color color{0, 0, 0, 128};
    Point offset{1.0f, 1.0f};
    Pt blur = 0.0f;
};

/**
 * 下線・打消し線（TextStyle::underline / strikethrough）
 *
 * 横組みの下線はベースラインの下（フォントの post テーブルの位置）、縦組みでは文字の右側（傍線）。
 * 打消し線は横組みで x ハイトの中ほど（OS/2 の yStrikeoutPosition）、縦組みで列の中心線。
 * 太さ・位置はフォントのメトリクスから取り、無ければ size の 1/20・1/10 を使う
 */
struct TextDecoration {
    std::optional<Paint> color;     ///< 無ければ TextStyle::fill
    Pt thickness = 0.0f;            ///< 0 でフォントのメトリクス
    /// 位置の補正（em）。文字から離れる向きが正（横組み: 下、縦組み: 右）
    float offset = 0.0f;
};

/**
 * 絵文字の表示形式
 *  - Auto: 異体字セレクタ（VS15 = U+FE0E で字形、VS16 = U+FE0F で絵文字）に従い、無ければフォントの解決順のまま
 *  - Text: モノクロの字形を優先する（カラーフォントは最後に回し、選ばれてもカラーでは描かない）
 *  - Emoji: カラー絵文字を優先する
 */
enum class EmojiPresentation : uint8_t { Auto, Text, Emoji };

/**
 * 文字スタイル
 */
struct TextStyle {
    FontSpec font;
    Pt size = 10.0f;
    Paint fill{Color{0, 0, 0, 255}};        ///< 塗り（単色またはグラデーション）
    std::optional<Stroke> stroke;           ///< 縁取り

    /// 影。layers の下に 1 層足す
    std::optional<TextShadow> shadow;
    /// 外観の層（下から上）。空なら fill / stroke の 1 層。指定すると fill / stroke は描画に使わない
    /// （下線の既定色としては fill が使われる）
    std::vector<TextLayer> layers;

    std::optional<TextDecoration> underline;
    std::optional<TextDecoration> strikethrough;

    /// 字間（em 単位）。組版層ではクラスタ間の Glue として扱う
    float letterSpacing = 0.0f;

    /// 縦組み中の向きの部分指定（段落の指定を上書き）
    std::optional<TextOrientation> orientation;

    /// 平体・長体
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    /// 合成ボールド／斜体（フォントに該当ウェイト・斜体が無い場合に明示指定する）
    bool fakeBold = false;
    bool fakeItalic = false;

    /// ベースラインのずらし（このスタイルの em 単位）。注記側が正（横組み: 上、縦組み: 右）。上付き文字・脚注記号用
    float baselineShift = 0.0f;

    /// BCP47（シェイピングの言語タグ。"ja" で日本語字形が選ばれる）
    std::string language = "ja";

    /// 絵文字の表示形式（既定は異体字セレクタに従う）
    EmojiPresentation emojiPresentation = EmojiPresentation::Auto;

    /// OpenType feature（HarfBuzz の書式: "palt" "+liga" "-kern" "liga=0" "ss01"）。そのまま HarfBuzz へ渡す。
    /// palt / halt / pwid / hwid など字幅を変える feature を有効にした文字は、JLReq の約物の詰め（仮想ボディの
    /// 半角化と約物間のアキ）を使わずフォントの送りをそのまま使う（二重に詰めない）
    std::vector<std::string> features;

    /// features に字幅を変える feature（palt / halt / pwid / hwid / vpal / vhal / twid / qwid）が有効に入っているか
    bool hasProportionalFeature() const {
        for (const std::string& f : features) {
            std::string tag = f;
            bool on = true;
            if (!tag.empty() && (tag[0] == '+' || tag[0] == '-')) { on = tag[0] == '+'; tag = tag.substr(1); }
            const size_t eq = tag.find('=');
            if (eq != std::string::npos) { on = tag.substr(eq + 1) != "0"; tag = tag.substr(0, eq); }
            const size_t br = tag.find('[');
            if (br != std::string::npos) tag = tag.substr(0, br);
            if (!on) continue;
            if (tag == "palt" || tag == "halt" || tag == "pwid" || tag == "hwid" || tag == "vpal" ||
                tag == "vhal" || tag == "twid" || tag == "qwid") return true;
        }
        return false;
    }

    /// 実際に描く層（下から上）: shadow → layers（空なら fill / stroke の 1 層）
    std::vector<TextLayer> resolvedLayers() const {
        std::vector<TextLayer> out;
        if (shadow) {
            TextLayer l;
            l.fill = shadow->color;
            l.offset = shadow->offset;
            l.blur = shadow->blur;
            out.push_back(l);
        }
        if (layers.empty()) {
            TextLayer l;
            l.fill = fill;
            l.stroke = stroke;
            out.push_back(l);
        } else {
            out.insert(out.end(), layers.begin(), layers.end());
        }
        return out;
    }
};

/**
 * 約物の詰め・和欧間・ぶら下げ
 */
/**
 * 禁則の強さ（CSS の line-break 相当）
 *  - Strict: JLReq 附属書 A のまま（小書きの仮名・長音・繰返し記号・ハイフン類も行頭に置かない）
 *  - Normal: 小書きの仮名・長音・繰返し記号・ハイフン類は「弱い禁則」（有限のペナルティ。他に切れる所が無ければ行頭に来る）
 *  - Loose: さらに句読点・終わり括弧・中点・区切り約物も弱い禁則にする（狭い段用）
 */
enum class KinsokuLevel : uint8_t { Strict, Normal, Loose };

struct SpacingOptions {
    /// 約物の詰め（JLReq のアキ量表を適用する）。false ならベタ組み
    bool punctuationSpacing = true;
    /// 禁則の強さ
    KinsokuLevel kinsoku = KinsokuLevel::Strict;
    /// 弱い禁則のペナルティ（Knuth–Plass の demerits に効く。Greedy では「切れる」扱い）
    float weakKinsokuPenalty = 500.0f;
    /// 禁則の追加・除外（KAG3 の wwFollowing / wwLeading 相当）。文字クラスより優先する
    std::u16string lineStartProhibited;     ///< 行頭に置かない文字を足す
    std::u16string lineStartAllowed;        ///< 行頭禁則から外す文字
    std::u16string lineEndProhibited;       ///< 行末に置かない文字を足す
    std::u16string lineEndAllowed;          ///< 行末禁則から外す文字
    /// 行末に来た句読点を版面外へ出す
    bool hangingPunctuation = false;
    /// 和欧間のアキを入れる
    bool latinGap = true;
    /// 和字同士の字間に許す伸び（em）。JLReq の追い出し（字間を空けて行末を揃える）に
    /// 使う。pTeX の kanjiskip の stretch に相当。0 でベタ組み固定
    float kanjiSkipStretch = 0.125f;
    /// 和字同士の字間に許す縮み（em）。通常 0（詰めるのは約物のアキだけ）
    float kanjiSkipShrink = 0.0f;
};

enum class LineBreakStrategy : uint8_t {
    Greedy,       ///< 各ブレーク候補で即断。画面のリアルタイム描画向け
    KnuthPlass,   ///< 段落全体でデメリットを最小化。組版品質が要る用途向け
};

/**
 * 折返しの方式
 *  - Mixed: 和文は字ごと、欧文は語（UAX #14）で切る（既定）
 *  - Char: 欧文の語の途中でも切る（CSS word-break: break-all）
 *  - Word: 和文も語（UAX #14 の機会があり、かつ和字同士でない所）でだけ切る（CSS word-break: keep-all）
 *  - None: 折り返さない（改行以外で切らない。行長を超えてはみ出す）
 */
enum class WrapMode : uint8_t { Mixed, Char, Word, None };

struct BreakOptions {
    LineBreakStrategy strategy = LineBreakStrategy::Greedy;
    WrapMode wrap = WrapMode::Mixed;
    /// 行末を揃える（グルーを伸縮させる）。false なら自然幅のまま
    bool justify = true;
    /// Knuth–Plass が許容するグルーの伸び率上限
    float tolerance = 3.0f;
    /// Knuth–Plass の行あたりのペナルティ（行数を増やしにくくする）
    float linePenalty = 10.0f;
};

enum class Align : uint8_t { Start, End, Center, Justify };

/// 行送り方向の揃え（箱の中での段落の位置。inl::originInBox）
enum class BlockAlign : uint8_t { Start, Center, End };

/// タブストップの揃え
enum class TabAlign : uint8_t { Left, Center, Right, Decimal };
struct TabStop {
    Pt position = 0.0f;             ///< 行頭からの位置（pt。一字下げは含まない）
    TabAlign align = TabAlign::Left;
    char32_t decimalChar = U'.';    ///< Decimal のときに揃える文字
};

/// 段落の基底方向（UAX #9）。Auto は最初の強い文字で決める（和文・欧文は LTR）
enum class Direction : uint8_t { Auto, Ltr, Rtl };

/**
 * 段落スタイル
 */
struct ParagraphStyle {
    Align align = Align::Justify;

    /// 基底方向。RTL の段落では Start / End が入れ替わり、一字下げは行の終端側（右）に付く。
    /// 行の中の双方向テキスト（アラビア文字・ヘブライ文字の混在）は方向に関わらず UAX #9 で並べ替える
    Direction direction = Direction::Auto;

    /// 一字下げ（em 単位。日本語段落の既定は 1）
    float firstLineIndent = 0.0f;
    /// ぶら下げインデント: 2 行目以降の字下げ（em）。途中からの字下げは Annotation::indent
    float hangingIndent = 0.0f;

    /// 行数上限（ParagraphLayouter::layout の maxLines）で切れたとき、最後の行の末尾に置く省略記号（空なら置かない）
    std::u16string ellipsis;

    /// タブストップ（行頭からの位置、pt）。空なら tabWidth × em ごとの左揃えタブ。preserveSpaces のときは使わず空白に展開する
    std::vector<TabStop> tabStops;

    /// 行送り（pt）。0 なら size × lineHeight
    Pt linePitch = 0.0f;
    float lineHeight = 1.7f;

    /// 縦組み中の文字の向き（既定: 和文正立・欧文横倒し）
    TextOrientation orientation = TextOrientation::Mixed;

    SpacingOptions spacing;
    BreakOptions lineBreak;

    /// 欧文間隔を伸縮しない固定幅の箱にする（コードブロック等。行頭の空白も残る）
    bool preserveSpaces = false;
    /// preserveSpaces のとき、タブを何桁ごとのタブ位置で空白に展開するか
    int tabWidth = 4;

    /// 段落全体の回転（度。時計回りが正。行頭（emitParagraph の origin）を中心に回す）
    float rotation = 0.0f;

    /// 実効の行送り
    Pt resolvedLinePitch(Pt fontSize) const {
        return linePitch > 0.0f ? linePitch : fontSize * lineHeight;
    }
};

} // namespace typeset

#endif // TYPESET_STYLE_HPP
