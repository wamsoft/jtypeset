/**
 * orientation.cpp — UAX #50 Vertical_Orientation の簡易判定表
 */

#include "typeset/text/orientation.hpp"

#include <algorithm>
#include <iterator>

namespace typeset::text {

namespace {

/// 正立（U / Tu / Tr）となるコードポイント範囲。入らないものはすべて横倒し（R）
struct Range {
    char32_t first;
    char32_t last;
};

constexpr Range kUprightRanges[] = {
    {0x1100, 0x11FF},   // ハングル字母
    {0x2E80, 0x2EFF},   // CJK 部首補助
    {0x2F00, 0x2FDF},   // 康熙部首
    {0x2FF0, 0x2FFF},   // 漢字構成記述文字
    {0x3000, 0x303F},   // CJK 記号・句読点（括弧・波ダッシュは vert で縦字形へ）
    {0x3040, 0x309F},   // ひらがな
    {0x30A0, 0x30FF},   // カタカナ
    {0x3100, 0x312F},   // 注音字母
    {0x3130, 0x318F},   // ハングル互換字母
    {0x3190, 0x319F},   // 漢文用記号
    {0x31A0, 0x31BF},   // 注音字母拡張
    {0x31C0, 0x31EF},   // CJK の筆画
    {0x31F0, 0x31FF},   // カタカナ拡張
    {0x3200, 0x32FF},   // 囲み CJK
    {0x3300, 0x33FF},   // CJK 互換
    {0x3400, 0x4DBF},   // CJK 統合漢字拡張 A
    {0x4E00, 0x9FFF},   // CJK 統合漢字
    {0xA000, 0xA4CF},   // イ文字
    {0xA960, 0xA97F},   // ハングル字母拡張 A
    {0xAC00, 0xD7FF},   // ハングル音節・字母拡張 B
    {0xF900, 0xFAFF},   // CJK 互換漢字
    {0xFE10, 0xFE1F},   // 縦書き形
    {0xFE30, 0xFE4F},   // CJK 互換形
    {0xFE50, 0xFE6F},   // 小字形
    {0xFF01, 0xFF60},   // 全角形（半角カタカナ FF61-FF9F は横倒し）
    {0xFFE0, 0xFFE6},   // 全角記号
    {0x1B000, 0x1B16F}, // 仮名補助・仮名拡張
    {0x1F200, 0x1F2FF}, // 囲み文字補助
    {0x1F300, 0x1F5FF}, // その他の記号と絵文字
    {0x1F600, 0x1F64F}, // 顔文字
    {0x1F680, 0x1F6FF}, // 交通・地図記号
    {0x1F900, 0x1F9FF}, // 補助記号と絵文字
    {0x1FA70, 0x1FAFF}, // 記号と絵文字拡張 A
    {0x20000, 0x2FFFD}, // CJK 統合漢字拡張 B〜
    {0x30000, 0x3FFFD}, // CJK 統合漢字拡張 G〜
};

} // namespace

CharOrientation getCharOrientation(char32_t cp) {
    if (cp < kUprightRanges[0].first) return CharOrientation::Rotated;
    const auto* begin = std::begin(kUprightRanges);
    const auto* end = std::end(kUprightRanges);
    const auto* it = std::upper_bound(begin, end, cp,
                                      [](char32_t v, const Range& r) { return v < r.first; });
    if (it == begin) return CharOrientation::Rotated;
    --it;
    return (cp <= it->last) ? CharOrientation::Upright : CharOrientation::Rotated;
}

} // namespace typeset::text
