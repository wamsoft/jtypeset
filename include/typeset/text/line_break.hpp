#ifndef TYPESET_TEXT_LINE_BREAK_HPP
#define TYPESET_TEXT_LINE_BREAK_HPP

#include <cstdint>
#include <string>
#include <vector>

/**
 * text/line_break — UAX #14 の行分割機会（libunibreak の包み）
 *
 * 和文の禁則は CharClass から直接求まるので、これは **欧文の単語内を割らない**
 * ためだけに使う（ハイフン・スラッシュの後ろで切れる、といった細部を UAX #14 に任せる）。
 */
namespace typeset::text {

enum class BreakOpportunity : uint8_t {
    Must,       ///< 強制改行（LF 等）
    Allowed,    ///< 切ってよい
    Prohibited, ///< 切れない
};

/**
 * 位置 i の文字の**直後**で切れるかを返す（text.size() 個）
 * @param lang BCP47（"ja" / "en" など）。空なら既定
 */
std::vector<BreakOpportunity> lineBreakOpportunities(const std::u16string& text,
                                                     const char* lang = "ja");

} // namespace typeset::text

#endif // TYPESET_TEXT_LINE_BREAK_HPP
