#ifndef TYPESET_TEXT_UTF_HPP
#define TYPESET_TEXT_UTF_HPP

#include <cstddef>
#include <string>

/**
 * text/utf — UTF-16 ⇄ コードポイントの小さなヘルパ
 *
 * 組版層の文字位置はすべて UTF-16 単位（HarfBuzz のクラスタと揃える）。
 */
namespace typeset::text {

/// UTF-16 の位置 i からコードポイントを取り出す（サロゲートペア対応）
inline char32_t codePointAt(const std::u16string& s, size_t i, size_t& length) {
    const char16_t c = s[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size()) {
        const char16_t c2 = s[i + 1];
        if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
            length = 2;
            return 0x10000 + (static_cast<char32_t>(c - 0xD800) << 10) +
                   static_cast<char32_t>(c2 - 0xDC00);
        }
    }
    length = 1;
    return c;
}

inline char32_t codePointAt(const std::u16string& s, size_t i) {
    size_t len = 1;
    return codePointAt(s, i, len);
}

/// コードポイントを UTF-16 に追加する
inline void appendCodePoint(std::u16string& out, char32_t cp) {
    if (cp <= 0xFFFF) {
        out += static_cast<char16_t>(cp);
    } else if (cp <= 0x10FFFF) {
        const char32_t v = cp - 0x10000;
        out += static_cast<char16_t>(0xD800 | (v >> 10));
        out += static_cast<char16_t>(0xDC00 | (v & 0x3FF));
    }
}

std::u16string utf8ToUtf16(const std::string& utf8);
std::string utf16ToUtf8(const std::u16string& utf16);

} // namespace typeset::text

#endif // TYPESET_TEXT_UTF_HPP
