#ifndef TYPESET_INL_ANNOTATION_HPP
#define TYPESET_INL_ANNOTATION_HPP

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * inl/annotation — 行内に組み込む注記
 *
 * ルビ・縦中横・圏点・割注・字取りを、本文の文字範囲に対する注記として表す。
 * 組版層（item_builder）がこれを解釈して Box / Glue / Penalty 列に落とし込む
 * ので、行分割・約物の詰め・両端揃えと矛盾しない。
 *
 * 範囲は本文の UTF-16 単位で [start, end)。範囲が重なる注記は、種類が違えば
 * 併用できる（ルビと圏点など）が、同じ種類の重なりは未定義。
 */
namespace typeset::inl {

enum class AnnotationType : uint8_t {
    Ruby,           ///< ルビ（振り仮名）
    TateChuYoko,    ///< 縦中横（半角数字等を 1em 角に正立で収める。横組みでは無視）
    Emphasis,       ///< 圏点（縦組みでは親文字の右、横組みでは上）
    Warichu,        ///< 割注（行内に 2 行の子ブロックを組む）
    Jidori,         ///< 字取り（指定 em 数へ均等割り付け）
};

enum class RubyMode : uint8_t {
    Group,  ///< グループルビ。親文字列全体に 1 つのルビを中付きで配置する
    Mono,   ///< モノルビ。text を `|` で区切って親文字 1 文字ずつに対応させる
    /// 熟語ルビ。text を `|` で区切って親文字 1 文字ずつに対応させるが、はみ出す部分は
    /// 熟語内の隣の親文字に掛ける（JLReq 3.3.8）。全部が親に収まればモノルビと同じ、
    /// 熟語全体でも収まらなければグループルビとして親文字列を広げる
    Jukugo,
};

enum class EmphasisMark : uint8_t {
    Sesame,         ///< ゴマ点 U+FE45
    OpenSesame,     ///< 白ゴマ点 U+FE46
    Dot,            ///< 中点 U+30FB
    FilledCircle,   ///< ● U+25CF
    OpenCircle,     ///< ○ U+25CB
};

char32_t emphasisMarkCodePoint(EmphasisMark mark);

struct Annotation {
    AnnotationType type = AnnotationType::Ruby;
    size_t start = 0;
    size_t end = 0;
    std::u16string text;            ///< ルビ文字列 / 割注の内容
    RubyMode rubyMode = RubyMode::Group;
    float scale = 0.5f;             ///< ルビ・割注・圏点の文字サイズ倍率
    EmphasisMark mark = EmphasisMark::Sesame;
    float jidoriEm = 0.0f;          ///< 字取りの長さ（em）

    static Annotation ruby(size_t start, size_t end, std::u16string text,
                           RubyMode mode = RubyMode::Group, float scale = 0.5f) {
        Annotation a;
        a.type = AnnotationType::Ruby;
        a.start = start; a.end = end;
        a.text = std::move(text);
        a.rubyMode = mode;
        a.scale = scale;
        return a;
    }
    static Annotation tateChuYoko(size_t start, size_t end) {
        Annotation a;
        a.type = AnnotationType::TateChuYoko;
        a.start = start; a.end = end;
        return a;
    }
    static Annotation emphasis(size_t start, size_t end,
                               EmphasisMark mark = EmphasisMark::Sesame, float scale = 0.5f) {
        Annotation a;
        a.type = AnnotationType::Emphasis;
        a.start = start; a.end = end;
        a.mark = mark;
        a.scale = scale;
        return a;
    }
    static Annotation warichu(size_t start, size_t end, std::u16string text, float scale = 0.5f) {
        Annotation a;
        a.type = AnnotationType::Warichu;
        a.start = start; a.end = end;
        a.text = std::move(text);
        a.scale = scale;
        return a;
    }
    static Annotation jidori(size_t start, size_t end, float em) {
        Annotation a;
        a.type = AnnotationType::Jidori;
        a.start = start; a.end = end;
        a.jidoriEm = em;
        return a;
    }
};

} // namespace typeset::inl

#endif // TYPESET_INL_ANNOTATION_HPP
