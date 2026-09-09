#ifndef TYPESET_INL_TAG_PARSER_HPP
#define TYPESET_INL_TAG_PARSER_HPP

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "typeset/inl/paragraph.hpp"
#include "typeset/style.hpp"

/**
 * inl/tag_parser — 軽量インラインタグ記法
 *
 * richtext（krkr_richtext）のタグ記法を読んで `inl::Paragraph`（run の列＋注記）にする。
 * ゲーム側のテキストをそのまま持ってこられるようにするための入口で、組版そのものには関わらない。
 *
 * 対応するタグ（richtext と同じ名前・属性）:
 *
 *  文字スタイル
 *   `<font size= weight= face= spacing= width= italic=>`  サイズ（pt）・ウェイト・family・字間（em）・平体（倍率）
 *   `<b>` `<strong>` / `<i>` `<em>`                       太字 / 斜体
 *   `<u>` / `<s>` `<strike>` `<del>`                      下線 / 打消し線
 *   `<sup>` / `<sub>`                                     上付き / 下付き
 *   `<color value="#rrggbb">` または `<color r= g= b= a=>` 文字色
 *   `<outline color= width= x= y= [add]>`                 縁取り（add で層を重ねる = 二重縁取り）
 *   `<shadow color= x= y= [blur=] [add]>`                 影（blur は typeset の拡張）
 *   `<style name="...">`                                  名前付きスタイル（TagParseOptions::namedStyles）
 *
 *  注記（typeset の拡張。richtext の `<ruby>` は同じ）
 *   `<ruby text="かんじ">漢字</ruby>`                      ルビ（mode= group|mono|jukugo、offset=）
 *   `<emphasis mark= [opposite]>` / `<tcy>` / `<warichu text=>` / `<jidori em=>`
 *
 *  そのほか
 *   `<br>` / `<sp width=n>`                               改行 / 空白
 *   `<link name="...">…</link>`                           リンク範囲（結果の links に文字範囲で返る）
 *   `<graph name= width= height=>`                        行内プレースホルダ（描かない箱。B10）
 *   `<start>` `<delay>` `<wait>` `<sync>` `<keywait>`      マーカー（結果の markers に位置で返る。B11）
 *   `<eval name= alt=>`                                   置換（TagParseOptions::evaluate で文字列を返す）
 *
 * 実体参照は `&lt; &gt; &amp; &quot; &apos;`。`<` が閉じないときはそのままの文字として扱う。
 */
namespace typeset::inl {

/// タイミング等のマーカー（描かない。ホストが位置を使う）
struct TagMarker {
    std::string kind;       ///< "start" / "delay" / "wait" / "sync" / "keywait"
    std::string value;      ///< 属性（value= または diff=/all= をそのまま）
    size_t charIndex = 0;   ///< 本文（UTF-16）での位置
};

/// リンクの範囲
struct TagLink {
    std::string name;
    size_t start = 0;
    size_t end = 0;
};

/// `<graph>` で置いた行内プレースホルダ
struct TagPlaceholder {
    std::string name;
    size_t charIndex = 0;
    Size size;
};

struct TagParseResult {
    Paragraph paragraph;
    std::vector<TagMarker> markers;
    std::vector<TagLink> links;
    std::vector<TagPlaceholder> placeholders;
    std::vector<std::string> errors;    ///< 未知のタグ・閉じ忘れ（読み飛ばして続ける）
};

struct TagParseOptions {
    TextStyle baseStyle;
    ParagraphStyle paragraphStyle;
    /// `<style name="...">` で引くスタイル
    std::map<std::string, TextStyle> namedStyles;
    /// `<font face="...">` の family。指定が namedFamilies にあればその列、無ければその名前 1 つ
    std::map<std::string, std::vector<std::string>> namedFamilies;
    /// `<eval name="...">` の置換（空を返したら alt、それも無ければ name をそのまま出す）
    std::function<std::u16string(const std::string&)> evaluate;

    float supOffset = 0.6f;     ///< 上付きのベースラインのずらし（em）
    float subOffset = -0.4f;    ///< 下付き
    float supScale = 0.6f;      ///< 上付き・下付きの文字サイズ倍率
    /// `<graph>` に width / height が無いときの大きさ（文字サイズに対する倍率）
    float graphDefaultSize = 1.0f;
    /// 未知のタグを本文としてそのまま残す（false なら捨てて errors に記録）
    bool keepUnknownTags = false;
};

/// タグ付きテキストを段落にする
TagParseResult parseTaggedText(const std::u16string& text, const TagParseOptions& options);

/// タグを取り除いた本文だけを返す（位置合わせの確認用）
std::u16string stripTags(const std::u16string& text);

} // namespace typeset::inl

#endif // TYPESET_INL_TAG_PARSER_HPP
