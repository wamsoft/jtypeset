/**
 * char_class.cpp — JLReq 文字クラスの分類表
 *
 * 個別のコードポイントは表引き、まとまった範囲は範囲判定で分ける。
 */

#include "typeset/text/char_class.hpp"

#include <algorithm>
#include <iterator>

namespace typeset::text {

namespace {

struct ClassEntry {
    char32_t cp;
    CharClass cls;
};

/// 個別指定（コードポイント昇順）。ここに載らない文字は範囲判定へ落ちる
constexpr ClassEntry kSingles[] = {
    // --- ASCII ---
    // ASCII の約物（. , : ; ! ? ( ) [ ] { }）は欧文用文字として扱う。JLReq の約物処理
    // （半角詰め・アキ）は全角形（U+FF01〜）と CJK 記号（U+3000〜）にだけ掛ける。
    // "1." "e.g." "f(x)" にアキが入らないようにするため
    {0x0020, CharClass::Space},          // SPACE
    {0x00A0, CharClass::Space},          // NBSP
    {0x00A3, CharClass::PrefixAbbr},     // £
    {0x00B0, CharClass::PostfixAbbr},    // °
    {0x2010, CharClass::Hyphen},         // ‐
    {0x2013, CharClass::Hyphen},         // – EN DASH
    {0x2014, CharClass::Inseparable},    // — EM DASH
    {0x2015, CharClass::Inseparable},    // ― HORIZONTAL BAR
    {0x2018, CharClass::OpenBracket},    // ‘
    {0x2019, CharClass::CloseBracket},   // ’
    {0x201C, CharClass::OpenBracket},    // “
    {0x201D, CharClass::CloseBracket},   // ”
    {0x2025, CharClass::Inseparable},    // ‥
    {0x2026, CharClass::Inseparable},    // …
    {0x2030, CharClass::PostfixAbbr},    // ‰
    {0x2032, CharClass::PostfixAbbr},    // ′
    {0x2033, CharClass::PostfixAbbr},    // ″
    {0x203C, CharClass::Dividing},       // ‼
    {0x2047, CharClass::Dividing},       // ⁇
    {0x2048, CharClass::Dividing},       // ⁈
    {0x2049, CharClass::Dividing},       // ⁉
    {0x20AC, CharClass::PrefixAbbr},     // €
    {0x2103, CharClass::PostfixAbbr},    // ℃
    {0x2116, CharClass::PrefixAbbr},     // №
    {0x3001, CharClass::Comma},          // 、
    {0x3002, CharClass::FullStop},       // 。
    {0x3005, CharClass::Iteration},      // 々
    {0x3008, CharClass::OpenBracket},    // 〈
    {0x3009, CharClass::CloseBracket},   // 〉
    {0x300A, CharClass::OpenBracket},    // 《
    {0x300B, CharClass::CloseBracket},   // 》
    {0x300C, CharClass::OpenBracket},    // 「
    {0x300D, CharClass::CloseBracket},   // 」
    {0x300E, CharClass::OpenBracket},    // 『
    {0x300F, CharClass::CloseBracket},   // 』
    {0x3010, CharClass::OpenBracket},    // 【
    {0x3011, CharClass::CloseBracket},   // 】
    {0x3014, CharClass::OpenBracket},    // 〔
    {0x3015, CharClass::CloseBracket},   // 〕
    {0x3016, CharClass::OpenBracket},    // 〖
    {0x3017, CharClass::CloseBracket},   // 〗
    {0x3018, CharClass::OpenBracket},    // 〘
    {0x3019, CharClass::CloseBracket},   // 〙
    {0x301A, CharClass::OpenBracket},    // 〚
    {0x301B, CharClass::CloseBracket},   // 〛
    {0x301C, CharClass::Hyphen},         // 〜
    {0x301D, CharClass::OpenBracket},    // 〝
    {0x301F, CharClass::CloseBracket},   // 〟
    {0x3030, CharClass::Hyphen},         // 〰
    {0x303B, CharClass::Iteration},      // 〻
    {0x309D, CharClass::Iteration},      // ゝ
    {0x309E, CharClass::Iteration},      // ゞ
    {0x30A0, CharClass::Hyphen},         // ゠
    {0x30FB, CharClass::MiddleDot},      // ・
    {0x30FC, CharClass::Prolonged},      // ー
    {0x30FD, CharClass::Iteration},      // ヽ
    {0x30FE, CharClass::Iteration},      // ヾ
    {0xFF01, CharClass::Dividing},       // ！
    {0xFF03, CharClass::PrefixAbbr},     // ＃
    {0xFF04, CharClass::PrefixAbbr},     // ＄
    {0xFF05, CharClass::PostfixAbbr},    // ％
    {0xFF08, CharClass::OpenBracket},    // （
    {0xFF09, CharClass::CloseBracket},   // ）
    {0xFF0C, CharClass::Comma},          // ，
    {0xFF0E, CharClass::FullStop},       // ．
    {0xFF1A, CharClass::MiddleDot},      // ：
    {0xFF1B, CharClass::MiddleDot},      // ；
    {0xFF1F, CharClass::Dividing},       // ？
    {0xFF3B, CharClass::OpenBracket},    // ［
    {0xFF3D, CharClass::CloseBracket},   // ］
    {0xFF5B, CharClass::OpenBracket},    // ｛
    {0xFF5D, CharClass::CloseBracket},   // ｝
    {0xFF5F, CharClass::OpenBracket},    // ｟
    {0xFF60, CharClass::CloseBracket},   // ｠
    {0xFFE1, CharClass::PrefixAbbr},     // ￡
    {0xFFE5, CharClass::PrefixAbbr},     // ￥
};

/// 小書きの仮名（cl-11）
constexpr char32_t kSmallKana[] = {
    0x3041, 0x3043, 0x3045, 0x3047, 0x3049,             // ぁぃぅぇぉ
    0x3063, 0x3083, 0x3085, 0x3087, 0x308E,             // っゃゅょゎ
    0x3095, 0x3096,                                     // ゕゖ
    0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9,             // ァィゥェォ
    0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE,             // ッャュョヮ
    0x30F5, 0x30F6,                                     // ヵヶ
    0x31F0, 0x31F1, 0x31F2, 0x31F3, 0x31F4,             // ㇰㇱㇲㇳㇴ
    0x31F5, 0x31F6, 0x31F7, 0x31F8, 0x31F9,
    0x31FA, 0x31FB, 0x31FC, 0x31FD, 0x31FE, 0x31FF,
};

bool lookupSingle(char32_t cp, CharClass& out) {
    const auto* begin = std::begin(kSingles);
    const auto* end = std::end(kSingles);
    const auto* it = std::lower_bound(begin, end, cp,
                                      [](const ClassEntry& e, char32_t v) { return e.cp < v; });
    if (it != end && it->cp == cp) {
        out = it->cls;
        return true;
    }
    return false;
}

bool isSmallKana(char32_t cp) {
    return std::find(std::begin(kSmallKana), std::end(kSmallKana), cp) != std::end(kSmallKana);
}

} // namespace

CharClass getCharClass(char32_t cp) {
    CharClass cls;
    if (lookupSingle(cp, cls)) return cls;
    if (isSmallKana(cp)) return CharClass::SmallKana;

    // 数字（半角・全角）
    if (cp >= 0x0030 && cp <= 0x0039) return CharClass::Digit;
    if (cp >= 0xFF10 && cp <= 0xFF19) return CharClass::Digit;

    // 欧文用文字
    if (cp >= 0x0021 && cp <= 0x007E) return CharClass::Western;
    if (cp >= 0x00C0 && cp <= 0x024F) return CharClass::Western;
    if (cp >= 0x0370 && cp <= 0x058F) return CharClass::Western;   // ギリシャ・キリル
    if (cp >= 0xFF21 && cp <= 0xFF3A) return CharClass::Western;   // 全角ラテン大文字
    if (cp >= 0xFF41 && cp <= 0xFF5A) return CharClass::Western;   // 全角ラテン小文字

    // 和字間隔
    if (cp == 0x3000) return CharClass::IdeographicSpace;

    // 仮名
    if (cp >= 0x3041 && cp <= 0x309F) return CharClass::Hiragana;
    if (cp >= 0x30A0 && cp <= 0x30FF) return CharClass::Katakana;
    if (cp >= 0x31F0 && cp <= 0x31FF) return CharClass::Katakana;
    if (cp >= 0xFF66 && cp <= 0xFF9F) return CharClass::Katakana;  // 半角カタカナ

    // 漢字等
    if (cp >= 0x2E80 && cp <= 0x2FDF) return CharClass::Ideographic;
    if (cp >= 0x3005 && cp <= 0x3007) return CharClass::Ideographic;
    if (cp >= 0x3400 && cp <= 0x4DBF) return CharClass::Ideographic;
    if (cp >= 0x4E00 && cp <= 0x9FFF) return CharClass::Ideographic;
    if (cp >= 0xF900 && cp <= 0xFAFF) return CharClass::Ideographic;
    if (cp >= 0x20000 && cp <= 0x3FFFD) return CharClass::Ideographic;

    // ハングル・その他の全角
    if (cp >= 0xAC00 && cp <= 0xD7AF) return CharClass::Ideographic;
    if (cp >= 0x3130 && cp <= 0x318F) return CharClass::Ideographic;

    return CharClass::Unknown;
}

bool isHalfWidthPunctuation(CharClass c) {
    switch (c) {
    case CharClass::OpenBracket:
    case CharClass::CloseBracket:
    case CharClass::FullStop:
    case CharClass::Comma:
    case CharClass::MiddleDot:
        return true;
    default:
        return false;
    }
}

BodyAlign getBodyAlign(CharClass c) {
    switch (c) {
    case CharClass::OpenBracket:
        return BodyAlign::End;
    case CharClass::CloseBracket:
    case CharClass::FullStop:
    case CharClass::Comma:
        return BodyAlign::Start;
    case CharClass::MiddleDot:
        return BodyAlign::Center;
    default:
        return BodyAlign::Full;
    }
}

bool isLineStartProhibited(CharClass c) {
    switch (c) {
    case CharClass::CloseBracket:
    case CharClass::FullStop:
    case CharClass::Comma:
    case CharClass::MiddleDot:
    case CharClass::Dividing:
    case CharClass::SmallKana:
    case CharClass::Prolonged:
    case CharClass::Iteration:
    case CharClass::Hyphen:
    case CharClass::PostfixAbbr:
        return true;
    default:
        return false;
    }
}

bool isLineEndProhibited(CharClass c) {
    switch (c) {
    case CharClass::OpenBracket:
    case CharClass::PrefixAbbr:
        return true;
    default:
        return false;
    }
}

bool isJapanese(CharClass c) {
    switch (c) {
    case CharClass::Western:
    case CharClass::Digit:
    case CharClass::Space:
        return false;
    default:
        return true;
    }
}

bool isWestern(CharClass c) {
    return c == CharClass::Western || c == CharClass::Digit;
}

bool isHangable(CharClass c) {
    return c == CharClass::FullStop || c == CharClass::Comma;
}

} // namespace typeset::text
