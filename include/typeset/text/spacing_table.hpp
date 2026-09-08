#ifndef TYPESET_TEXT_SPACING_TABLE_HPP
#define TYPESET_TEXT_SPACING_TABLE_HPP

#include "typeset/text/char_class.hpp"

/**
 * text/spacing_table — 文字クラス間のアキ量表
 *
 * JLReq 表 3（連続する文字クラス間の空き量）を、隣接ペアから引ける形で持つ。
 * 単位は em（フォントサイズ基準）。
 *
 * アキは「自然値・伸び・縮み」の 3 値（TeX の glue）で持つ。この形にした
 * 時点で、追い込み・追い出し・両端揃えは「グルーの伸縮でどう行長に合わせるか」
 * という 1 つの問題に統一される。
 *
 * 約物そのものの仮想ボディは全角のままで、ここで扱うのは**文字と文字の間**の
 * アキ。字面が半角に寄っている約物は、ボディ幅を半角にしてから前後にアキを
 * 入れる形で組む（getBodyAlign を参照）。
 */
namespace typeset::text {

/// 伸縮するアキ（em 単位）
struct GlueSpec {
    float natural = 0.0f;
    float stretch = 0.0f;
    float shrink = 0.0f;

    bool isZero() const { return natural == 0.0f && stretch == 0.0f && shrink == 0.0f; }
};

/// 隣接する 2 文字の間に入るアキ量
GlueSpec getSpacing(CharClass before, CharClass after);

/**
 * 仮想ボディ幅（em 単位）
 *
 * 字面が半角に寄る約物は 0.5em、それ以外の和字は 1.0em。欧文はここでは
 * 決まらない（シェイパーが返すアドバンスをそのまま使う）ので 0 を返す。
 */
float getBodyWidth(CharClass c);

} // namespace typeset::text

#endif // TYPESET_TEXT_SPACING_TABLE_HPP
