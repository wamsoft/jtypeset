#ifndef TYPESET_OBJ_SVG_IMPORT_HPP
#define TYPESET_OBJ_SVG_IMPORT_HPP

#include <string>

#include "typeset/obj/object.hpp"

/**
 * obj/svg_import — SVG のサブセットを表示リストへ
 *
 * 数式レンダラ（MathJax / dvisvgm / Typst）やグラフ（matplotlib）が出す SVG を受けるための
 * 読み込み。対応: svg(width/height/viewBox) / g(transform, 塗り・線の継承) / path(d 全コマンド、
 * 弧は 3 次ベジェへ) / rect / circle / ellipse / line / polyline / polygon / defs + use(href) /
 * fill・stroke・stroke-width・opacity・fill-rule（属性と style=）。text・画像・クリップ・グラデーションは無視する。
 *
 * 大きさ: width/height（単位 px = 0.75pt, pt, mm, cm, in, em, ex）を pt に。無ければ viewBox。
 * ベースライン: `<!-- typeset baseline="12.3" -->`（上端から pt）、または style の
 * `vertical-align: -1.2ex` から（MathJax）。
 */
namespace typeset::obj {

struct SvgImportOptions {
    Pt fontSize = 10.0f;    ///< em / ex の基準
    Pt exRatio = 0.45f;     ///< 1ex = fontSize × exRatio
};

/**
 * @return 失敗時 false（out.error に理由）
 */
bool importSvg(const std::string& svg, const SvgImportOptions& opts, ObjectResult& out);

} // namespace typeset::obj

#endif // TYPESET_OBJ_SVG_IMPORT_HPP
