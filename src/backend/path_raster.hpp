#ifndef TYPESET_BACKEND_PATH_RASTER_HPP
#define TYPESET_BACKEND_PATH_RASTER_HPP

#include <cstdint>
#include <vector>

#include "typeset/geom.hpp"

/**
 * path_raster — 多角形のスキャンライン塗り（AA）とストロークの多角形化
 *
 * ラスタ backend の内部用。グリフはここを通らない（glyphware が FreeType の
 * ラスタライザでマスクを作る）。罫線・矩形・吹き出し等のパス用。
 */
namespace typeset::backend::detail {

/**
 * 8bit カバレッジマスク（デバイスピクセル）
 */
struct Coverage {
    int x0 = 0, y0 = 0;     ///< マスク左上のピクセル座標
    int width = 0, height = 0;
    std::vector<uint8_t> a; ///< width * height
};

/**
 * 多角形列を塗る
 * @param polys デバイスピクセル座標の多角形（暗黙に閉じる）
 * @param evenOdd 塗り規則（false = nonzero）
 * @param clipW,clipH 描画先の大きさ（[0,clipW)×[0,clipH) に切る）
 * @return 何か塗るものがあれば true
 */
bool rasterizePolygons(const std::vector<std::vector<Point>>& polys, bool evenOdd,
                       int clipW, int clipH, Coverage& out);

/**
 * 折れ線をストロークの多角形（nonzero で塗る）へ
 * @param polylines 折れ線（デバイスピクセル座標）
 * @param closed 各折れ線が閉じているか
 * @param width 線幅（ピクセル）
 */
std::vector<std::vector<Point>> strokeToPolygons(const std::vector<std::vector<Point>>& polylines,
                                                 const std::vector<bool>& closed,
                                                 float width, StrokeJoin join, StrokeCap cap);

} // namespace typeset::backend::detail

#endif // TYPESET_BACKEND_PATH_RASTER_HPP
