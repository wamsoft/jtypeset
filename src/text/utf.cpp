#include "typeset/text/utf.hpp"

#include <cstdint>
namespace typeset::text {

std::u16string utf8ToUtf16(const std::string& utf8) {
    std::u16string result;
    size_t i = 0;
    while (i < utf8.size()) {
        uint32_t cp = 0;
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if ((c & 0x80) == 0) {
            cp = c; i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = (c & 0x1F) << 6;
            if (i + 1 < utf8.size()) cp |= (utf8[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = (c & 0x0F) << 12;
            if (i + 1 < utf8.size()) cp |= (utf8[i + 1] & 0x3F) << 6;
            if (i + 2 < utf8.size()) cp |= (utf8[i + 2] & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = (c & 0x07) << 18;
            if (i + 1 < utf8.size()) cp |= (utf8[i + 1] & 0x3F) << 12;
            if (i + 2 < utf8.size()) cp |= (utf8[i + 2] & 0x3F) << 6;
            if (i + 3 < utf8.size()) cp |= (utf8[i + 3] & 0x3F);
            i += 4;
        } else {
            i += 1;
            continue;
        }
        appendCodePoint(result, cp);
    }
    return result;
}

std::string utf16ToUtf8(const std::u16string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        size_t len = 1;
        const char32_t cp = codePointAt(s, i, len);
        i += len;
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

} // namespace typeset::text
