/**
 * hyphenation.cpp — Liang のハイフネーション
 *
 * TeX と同じ手順: 単語を `.word.` にして、部分文字列に一致するパターンの数字を位置ごとに最大で重ね、
 * 奇数の位置を分割可能とする。例外リスト（`\hyphenation{}`）は単語ごとの上書き。
 */

#include "typeset/text/hyphenation.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

#include "typeset/text/utf.hpp"

namespace typeset::text {

namespace {

/// ASCII と Latin-1 の範囲を小文字にする（パターンは小文字で書かれている）
char32_t lowerChar(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 32;
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return c + 32;   // À-Þ（×を除く）
    if (c >= 0x0100 && c < 0x0180 && (c % 2) == 0) return c + 1;    // ラテン拡張 A の多くは偶数が大文字
    return c;
}

std::u32string toLower(const std::u32string& s) {
    std::u32string out;
    out.reserve(s.size());
    for (char32_t c : s) out += lowerChar(c);
    return out;
}

std::u32string utf8ToUtf32(const std::string& utf8) {
    const std::u16string u16 = utf8ToUtf16(utf8);
    std::u32string out;
    for (size_t i = 0; i < u16.size();) {
        size_t len = 1;
        out += codePointAt(u16, i, len);
        i += len;
    }
    return out;
}

bool isDigit(char32_t c) { return c >= U'0' && c <= U'9'; }

} // namespace

//------------------------------------------------------------------------------

size_t Hyphenator::addPatterns(const std::string& utf8) {
    // `\patterns{...}` / `\hyphenation{...}` のブロックを見て、無ければ全体を素のリストとして読む
    size_t added = 0;
    enum class Mode { Plain, Patterns, Exceptions } mode = Mode::Plain;
    std::string token;
    bool inComment = false;

    auto flush = [&]() {
        if (token.empty()) return;
        if (mode == Mode::Exceptions) {
            addException(token);
        } else {
            const std::u32string raw = utf8ToUtf32(token);
            std::u32string letters;
            std::vector<uint8_t> values;
            values.push_back(0);
            for (char32_t c : raw) {
                if (isDigit(c)) {
                    values.back() = static_cast<uint8_t>(c - U'0');
                } else {
                    letters += lowerChar(c);
                    values.push_back(0);
                }
            }
            if (!letters.empty()) {
                patterns_[letters] = std::move(values);
                ++added;
            }
        }
        token.clear();
    };

    for (size_t i = 0; i < utf8.size(); ++i) {
        const char c = utf8[i];
        if (inComment) {
            if (c == '\n') inComment = false;
            continue;
        }
        if (c == '%') {
            flush();
            inComment = true;
            continue;
        }
        if (c == '\\') {
            flush();
            // コマンド名を読む
            std::string cmd;
            size_t j = i + 1;
            while (j < utf8.size() && std::isalpha(static_cast<unsigned char>(utf8[j]))) cmd += utf8[j++];
            i = j - 1;
            if (cmd == "patterns") mode = Mode::Patterns;
            else if (cmd == "hyphenation") mode = Mode::Exceptions;
            continue;
        }
        if (c == '{') { flush(); continue; }
        if (c == '}') {
            flush();
            mode = Mode::Plain;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            flush();
            continue;
        }
        token += c;
    }
    flush();
    return added;
}

void Hyphenator::addException(const std::string& utf8Word) {
    const std::u32string raw = utf8ToUtf32(utf8Word);
    std::u32string letters;
    std::vector<size_t> breaks;
    for (char32_t c : raw) {
        if (c == U'-' || c == kSoftHyphen) {
            if (!letters.empty()) breaks.push_back(letters.size());
        } else {
            letters += lowerChar(c);
        }
    }
    if (!letters.empty()) exceptions_[letters] = std::move(breaks);
}

size_t Hyphenator::addPatternFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;
    const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return addPatterns(bytes);
}

std::vector<size_t> Hyphenator::hyphenate(const std::u32string& word, int minLeft, int minRight) const {
    std::vector<size_t> out;
    const size_t n = word.size();
    minLeft = std::max(1, minLeft);
    minRight = std::max(1, minRight);
    if (n < static_cast<size_t>(minLeft + minRight)) return out;

    const std::u32string lower = toLower(word);
    auto ex = exceptions_.find(lower);
    if (ex != exceptions_.end()) {
        for (size_t k : ex->second) {
            if (k >= static_cast<size_t>(minLeft) && n - k >= static_cast<size_t>(minRight)) out.push_back(k);
        }
        return out;
    }
    if (patterns_.empty()) return out;

    // `.word.` にして、すべての部分文字列でパターンを引く
    std::u32string w = U".";
    w += lower;
    w += U".";
    std::vector<uint8_t> values(w.size() + 1, 0);
    for (size_t start = 0; start < w.size(); ++start) {
        for (size_t len = 1; len <= w.size() - start; ++len) {
            auto it = patterns_.find(w.substr(start, len));
            if (it == patterns_.end()) continue;
            const std::vector<uint8_t>& v = it->second;
            for (size_t k = 0; k < v.size() && start + k < values.size(); ++k) {
                values[start + k] = std::max(values[start + k], v[k]);
            }
        }
    }
    // values[i] は w の i 文字目の手前の値。単語の k 文字目の後ろ = w の (k + 1) 文字目の手前
    for (size_t k = static_cast<size_t>(minLeft); k + static_cast<size_t>(minRight) <= n; ++k) {
        if (values[k + 1] % 2 == 1) out.push_back(k);
    }
    return out;
}

//------------------------------------------------------------------------------

namespace {

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string primaryLanguage(const std::string& language) {
    const size_t p = language.find_first_of("-_");
    return lowerAscii(p == std::string::npos ? language : language.substr(0, p));
}

} // namespace

Hyphenator& HyphenationDictionary::forLanguage(const std::string& language) {
    return byLanguage_[lowerAscii(language)];
}

const Hyphenator* HyphenationDictionary::find(const std::string& language) const {
    if (language.empty()) return nullptr;
    auto it = byLanguage_.find(lowerAscii(language));
    if (it == byLanguage_.end()) it = byLanguage_.find(primaryLanguage(language));
    return it == byLanguage_.end() ? nullptr : &it->second;
}

} // namespace typeset::text
