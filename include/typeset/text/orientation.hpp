#ifndef TYPESET_TEXT_ORIENTATION_HPP
#define TYPESET_TEXT_ORIENTATION_HPP

#include <cstdint>

/**
 * text/orientation — UAX #50 Vertical_Orientation 相当の判定
 */
namespace typeset::text {

/// 縦組み中の 1 文字の向き
enum class CharOrientation : uint8_t {
    Upright,    ///< 正立（縦字形置換は HarfBuzz の vert/vrt2 が行う）
    Rotated,    ///< 横倒し（時計回りに 90 度）
};

/**
 * U / Tu / Tr を Upright、R を Rotated として返す。Tu・Tr（縦字形へ置換して
 * 正立させる類。括弧・句読点・波ダッシュ等）を Upright に寄せているのは、
 * TTB でシェイピングして vert/vrt2 を効かせるため。
 */
CharOrientation getCharOrientation(char32_t cp);

} // namespace typeset::text

#endif // TYPESET_TEXT_ORIENTATION_HPP
