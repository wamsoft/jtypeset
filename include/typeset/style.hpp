#ifndef TYPESET_STYLE_HPP
#define TYPESET_STYLE_HPP

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
};

/**
 * 文字スタイル
 */
struct TextStyle {
    FontSpec font;
    Pt size = 10.0f;
    Color fill{0, 0, 0, 255};
    std::optional<Stroke> stroke;           ///< 縁取り

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

    /// BCP47（シェイピングの言語タグ。"ja" で日本語字形が選ばれる）
    std::string language = "ja";
};

/**
 * 約物の詰め・和欧間・ぶら下げ
 */
struct SpacingOptions {
    /// 約物の詰め（JLReq のアキ量表を適用する）。false ならベタ組み
    bool punctuationSpacing = true;
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

struct BreakOptions {
    LineBreakStrategy strategy = LineBreakStrategy::Greedy;
    /// 行末を揃える（グルーを伸縮させる）。false なら自然幅のまま
    bool justify = true;
    /// Knuth–Plass が許容するグルーの伸び率上限
    float tolerance = 3.0f;
    /// Knuth–Plass の行あたりのペナルティ（行数を増やしにくくする）
    float linePenalty = 10.0f;
};

enum class Align : uint8_t { Start, End, Center, Justify };

/**
 * 段落スタイル
 */
struct ParagraphStyle {
    Align align = Align::Justify;

    /// 一字下げ（em 単位。日本語段落の既定は 1）
    float firstLineIndent = 0.0f;

    /// 行送り（pt）。0 なら size × lineHeight
    Pt linePitch = 0.0f;
    float lineHeight = 1.7f;

    /// 縦組み中の文字の向き（既定: 和文正立・欧文横倒し）
    TextOrientation orientation = TextOrientation::Mixed;

    SpacingOptions spacing;
    BreakOptions lineBreak;

    /// 実効の行送り
    Pt resolvedLinePitch(Pt fontSize) const {
        return linePitch > 0.0f ? linePitch : fontSize * lineHeight;
    }
};

} // namespace typeset

#endif // TYPESET_STYLE_HPP
