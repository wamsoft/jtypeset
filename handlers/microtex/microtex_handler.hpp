#ifndef TYPESET_HANDLERS_MICROTEX_HPP
#define TYPESET_HANDLERS_MICROTEX_HPP

#include <string>

#include "typeset/font/font_set.hpp"
#include "typeset/obj/object.hpp"

/**
 * MicroTeX ハンドラ — LaTeX 数式を obj::ObjectResult（GlyphRun + Path）にする
 *
 * MicroTeX（https://github.com/NanoMichael/MicroTeX）の描画抽象を typeset の表示リストに写す。
 * 数式のグリフは MicroTeX 付属のフォント（res/fonts、OFL / Knuth ライセンス）を glyphware で開き、
 * GlyphRun として返すので、PDF でも字として埋め込まれる（サブセット化・検索の対象）。
 * `\text{}` などのテキストは textFamily のフォントで組む。
 *
 * ソースは LaTeX の数式（`$` は付けない）。params:
 *   style = "display" | "text"   （省略時: 別行立てなら display、行内なら text）
 *   color = "#rrggbb"            （既定: 黒）
 */
namespace typeset::handlers {

struct MicroTexOptions {
    /// MicroTeX の res ディレクトリ（空ならビルド時に埋め込んだ場所）
    std::string resDir;
    /// \text{} や \mathrm 以外の文字（CJK など）に使う FontSet のキー／family
    std::string textFamily = "serif";
    std::string sansFamily = "sans";
};

/// ハンドラを作る。fonts はハンドラより長生きすること。初期化に失敗すると error 付きの結果を返す
obj::ObjectHandler makeMicroTexHandler(font::FontSet& fonts, MicroTexOptions options = {});

/// MicroTeX を明示的に解放する（プロセス終了時に呼ばなくてもよい）
void releaseMicroTex();

} // namespace typeset::handlers

#endif // TYPESET_HANDLERS_MICROTEX_HPP
