#ifndef TYPESET_BACKEND_SVG_WRITER_HPP
#define TYPESET_BACKEND_SVG_WRITER_HPP

#include <string>

#include "typeset/dl/display_list.hpp"

/**
 * backend/svg_writer — 表示リストを SVG へ
 *
 * グリフはアウトラインを <path> にする（<text> は使わない。ビューアの
 * シェイピングに任せると組版結果が崩れる）。同じ face・gid のアウトラインは
 * <defs> に 1 回だけ置いて <use> で参照する。
 * 単位は pt をそのまま user unit にし、width/height に pt を付ける。
 */
namespace typeset::backend {

struct SvgOptions {
    int precision = 3;              ///< 座標の小数桁
    bool includeText = true;        ///< GlyphRun::text があれば <desc> に原文を入れる
    bool useDefs = true;            ///< グリフを <defs> + <use> で共有する
};

std::string writeSvg(const dl::DisplayList& list, const SvgOptions& opts = {});

bool saveSvg(const dl::DisplayList& list, const std::string& path, const SvgOptions& opts = {});

} // namespace typeset::backend

#endif // TYPESET_BACKEND_SVG_WRITER_HPP
