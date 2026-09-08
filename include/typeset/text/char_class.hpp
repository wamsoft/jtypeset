#ifndef TYPESET_TEXT_CHAR_CLASS_HPP
#define TYPESET_TEXT_CHAR_CLASS_HPP

#include <cstdint>

/**
 * text/char_class — JLReq の文字クラス
 *
 * 「日本語組版処理の要件」附属書 A の文字クラスのうち、間隔処理（表 3）と
 * 禁則処理に必要なものを持つ。分類は 1 文字（コードポイント）単位。
 * クラスタ（結合文字列）の文字クラスは先頭のコードポイントで決める。
 * 書字方向には依存しない（縦横共通）。richtext CharClass の移植。
 */
namespace typeset::text {

/**
 * 文字クラス（コメントの cl-NN は JLReq の文字クラス番号）
 */
enum class CharClass : uint8_t {
    OpenBracket,        ///< cl-01 始め括弧類          「（〔［｛〈《【
    CloseBracket,       ///< cl-02 終わり括弧類        」）〕］｝〉》】
    Hyphen,             ///< cl-03 ハイフン類          ‐ 〜 ゠ –
    Dividing,           ///< cl-04 区切り約物          ！ ？ ‼ ⁇
    MiddleDot,          ///< cl-05 中点類              ・ ： ；
    FullStop,           ///< cl-06 句点類              。 ．
    Comma,              ///< cl-07 読点類              、 ，
    Inseparable,        ///< cl-08 分離禁止文字        — … ‥ 〳〴〵
    Iteration,          ///< cl-09 繰返し記号          々 〻 ゝゞヽヾ
    Prolonged,          ///< cl-10 長音記号            ー
    SmallKana,          ///< cl-11 小書きの仮名        ぁぃぅ… ァィゥ… っゃゅょ
    PrefixAbbr,         ///< cl-12 前置省略記号        ￥ ＄ £ ＃ €
    PostfixAbbr,        ///< cl-13 後置省略記号        ° ′ ″ ℃ ％ ‰
    Ideographic,        ///< cl-19 漢字等
    Hiragana,           ///< cl-20 平仮名
    Katakana,           ///< cl-21 片仮名
    IdeographicSpace,   ///< cl-24 和字間隔（全角空白）
    Space,              ///< cl-26 欧文間隔
    Western,            ///< cl-27 欧文用文字
    Digit,              ///< cl-27 のうち算用数字（縦中横の判定に使う）
    Unknown,            ///< 上記に該当しないもの（和字扱い）
};

/**
 * 仮想ボディ内での字面の寄り
 *
 * 約物は全角の仮想ボディの中で字面が半分に寄っている。詰めるとは
 * 「ボディを半角にする」ことなので、字面がボディのどちら側にあるかで
 * グリフの描画オフセットが決まる。
 */
enum class BodyAlign : uint8_t {
    Full,   ///< 全角ボディいっぱい（通常の和字・欧文）
    Start,  ///< 行の進む向きの手前側に寄る（句読点・終わり括弧）
    End,    ///< 奥側に寄る（始め括弧）
    Center, ///< 中央に寄る（中点類）
};

CharClass getCharClass(char32_t cp);

/// 半角に詰められる約物か（仮想ボディが全角、字面が半角のもの）
bool isHalfWidthPunctuation(CharClass c);

BodyAlign getBodyAlign(CharClass c);

/// 行頭禁則 — この文字の直前では改行できない
bool isLineStartProhibited(CharClass c);

/// 行末禁則 — この文字の直後では改行できない
bool isLineEndProhibited(CharClass c);

/// 和文の文字か（和欧間アキの判定に使う）
bool isJapanese(CharClass c);

/// 欧文の文字か（和欧間アキの判定に使う）
bool isWestern(CharClass c);

/// ぶら下げ可能な約物か（句点類・読点類）
bool isHangable(CharClass c);

} // namespace typeset::text

#endif // TYPESET_TEXT_CHAR_CLASS_HPP
