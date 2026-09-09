#ifndef TYPESET_TEXT_HYPHENATION_HPP
#define TYPESET_TEXT_HYPHENATION_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * text/hyphenation — 欧文のハイフネーション（Liang のパターン）
 *
 * TeX と同じパターン（hyph-en-us.tex などの `\patterns{}` / `\hyphenation{}`）を読んで、
 * 単語の中の分割位置を返す。辞書は言語ごとに用意する必要があるので、パターンは利用側が読み込む
 * （`hyph-utf8` や LibreOffice の辞書がそのまま使える。ライセンスがそれぞれ違うので同梱しない）。
 *
 * パターンを持たない言語でも、テキスト中の**ソフトハイフン U+00AD** は常に分割位置として扱う。
 */
namespace typeset::text {

class Hyphenator {
public:
    /**
     * TeX のパターンを読む。`\patterns{ ... }` と `\hyphenation{ ... }` のブロック、
     * または 1 行 1 パターンの素のリストを受け付ける（`%` から行末まではコメント）
     * @return 読めたパターン数
     */
    size_t addPatterns(const std::string& utf8);

    /// 例外（`as-so-ciate` のようにハイフンで区切った綴り）を 1 つ足す
    void addException(const std::string& utf8Word);

    bool empty() const { return patterns_.empty() && exceptions_.empty(); }
    size_t patternCount() const { return patterns_.size(); }

    /**
     * 単語の分割位置。返るのは「先頭から k 文字目の後ろで切ってよい」の k（1 始まり、昇順）。
     * 文字数は UTF-32 の符号位置で数える
     * @param minLeft 行末に残す最小の文字数
     * @param minRight 次の行に送る最小の文字数
     */
    std::vector<size_t> hyphenate(const std::u32string& word, int minLeft = 2, int minRight = 3) const;

    /// ファイルから読む（見つからなければ 0）
    size_t addPatternFile(const std::string& path);

private:
    /// パターン（数字を抜いた文字列）→ 位置ごとの値（長さ = 文字数 + 1）
    std::unordered_map<std::u32string, std::vector<uint8_t>> patterns_;
    /// 例外: 数字を抜いた単語 → 分割位置
    std::unordered_map<std::u32string, std::vector<size_t>> exceptions_;
};

/**
 * 言語ごとのパターン。`TextStyle::language`（BCP47）で引く。
 * 完全一致 → 主言語（"en-GB" なら "en"）→ 既定の言語（setDefaultLanguage）の順に探す。
 *
 * 既定の言語は「和文の文書に混ざる英単語」のためのもの。本文の言語が `ja` でも、欧文のハイフネーションは
 * その言語のパターンで行いたい、という指定に使う（`forLanguage()` で最初に足した言語が自動で既定になる）
 */
class HyphenationDictionary {
public:
    /// 言語にパターンを足す（無ければ作る）。最初に足した言語が既定になる
    Hyphenator& forLanguage(const std::string& language);
    /// 言語のパターン（無ければ既定の言語、それも無ければ nullptr）
    const Hyphenator* find(const std::string& language) const;
    /// 見つからなかったときに使う言語（空で無効）
    void setDefaultLanguage(const std::string& language);
    const std::string& defaultLanguage() const { return default_; }
    bool empty() const { return byLanguage_.empty(); }

private:
    /// std::map なので、返した参照は要素を消さないかぎり有効
    std::map<std::string, Hyphenator> byLanguage_;
    std::string default_;
};

/// ソフトハイフン
constexpr char32_t kSoftHyphen = 0x00AD;

} // namespace typeset::text

#endif // TYPESET_TEXT_HYPHENATION_HPP
