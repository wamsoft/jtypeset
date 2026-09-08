/**
 * spacing_table.cpp — JLReq 表 3 のアキ量
 *
 * N×N の表を持たず、判定の順序で表現する。JLReq の表は「片方のクラスが
 * 決まればもう片方は例外リストで済む」形をしているので、そのほうが
 * 元の規定と突き合わせやすい。
 */

#include "typeset/text/spacing_table.hpp"

namespace typeset::text {

namespace {

/// 二分アキ。行調整では全部詰められる
constexpr GlueSpec kHalf{0.5f, 0.0f, 0.5f};
/// 四分アキ（中点類の前後）
constexpr GlueSpec kQuarter{0.25f, 0.0f, 0.25f};
/// 全角アキ（区切り約物の後ろ）。二分までしか詰めない
constexpr GlueSpec kFullWidth{1.0f, 0.0f, 0.5f};
/// 和欧間の四分アキ。三分まで空け、八分まで詰める
constexpr GlueSpec kLatinGap{0.25f, 0.0833333f, 0.125f};
constexpr GlueSpec kNone{};

/// 直後に来たときに「手前のアキ」を吸収する（既にアキが入る）クラス
bool absorbsPrecedingSpace(CharClass c) {
    switch (c) {
    case CharClass::CloseBracket:
    case CharClass::FullStop:
    case CharClass::Comma:
    case CharClass::MiddleDot:
    case CharClass::Dividing:
        return true;
    default:
        return false;
    }
}

} // namespace

GlueSpec getSpacing(CharClass before, CharClass after) {
    // --- 始め括弧類の前は二分アキ（cl-01） ---
    // 直前が始め括弧、または既にアキが入るクラスなら入れない。
    // 行頭の場合は行分割側が先頭のアキを落とす（＝行頭の半角下げ）。
    if (after == CharClass::OpenBracket) {
        if (before == CharClass::OpenBracket) return kNone;
        if (absorbsPrecedingSpace(before)) return kNone;
        return kHalf;
    }

    // --- 終わり括弧類・句点類・読点類の後ろは二分アキ（cl-02 / cl-06 / cl-07） ---
    if (before == CharClass::CloseBracket || before == CharClass::FullStop ||
        before == CharClass::Comma) {
        if (absorbsPrecedingSpace(after)) return kNone;
        return kHalf;
    }

    // --- 中点類の前後は四分アキ（cl-05） ---
    if (after == CharClass::MiddleDot) {
        if (before == CharClass::OpenBracket) return kNone;
        return kQuarter;
    }
    if (before == CharClass::MiddleDot) {
        if (absorbsPrecedingSpace(after)) return kNone;
        return kQuarter;
    }

    // --- 区切り約物の後ろは全角アキ（cl-04） ---
    if (before == CharClass::Dividing) {
        if (absorbsPrecedingSpace(after)) return kNone;
        return kFullWidth;
    }

    // --- 省略記号は続く／先立つ数字と密着させる（cl-12 / cl-13） ---
    if (before == CharClass::PrefixAbbr || after == CharClass::PostfixAbbr) {
        return kNone;
    }

    // --- 和欧間は四分アキ ---
    if (isJapanese(before) && isWestern(after)) return kLatinGap;
    if (isWestern(before) && isJapanese(after)) return kLatinGap;

    return kNone;
}

float getBodyWidth(CharClass c) {
    if (isHalfWidthPunctuation(c)) return 0.5f;
    switch (c) {
    case CharClass::Western:
    case CharClass::Digit:
    case CharClass::Space:
        return 0.0f;   // シェイパーのアドバンスを使う
    default:
        return 1.0f;
    }
}

} // namespace typeset::text
