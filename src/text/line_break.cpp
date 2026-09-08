/**
 * line_break.cpp — libunibreak の包み
 */

#include "typeset/text/line_break.hpp"

#include <mutex>

#include <linebreak.h>

namespace typeset::text {

std::vector<BreakOpportunity> lineBreakOpportunities(const std::u16string& text,
                                                     const char* lang) {
    static std::once_flag once;
    std::call_once(once, [] { init_linebreak(); });

    std::vector<BreakOpportunity> out(text.size(), BreakOpportunity::Prohibited);
    if (text.empty()) return out;

    std::vector<char> brks(text.size());
    set_linebreaks_utf16(reinterpret_cast<const utf16_t*>(text.data()), text.size(),
                         (lang && *lang) ? lang : nullptr, brks.data());
    for (size_t i = 0; i < text.size(); ++i) {
        switch (brks[i]) {
        case LINEBREAK_MUSTBREAK:  out[i] = BreakOpportunity::Must; break;
        case LINEBREAK_ALLOWBREAK: out[i] = BreakOpportunity::Allowed; break;
        default:                   out[i] = BreakOpportunity::Prohibited; break;
        }
    }
    return out;
}

} // namespace typeset::text
