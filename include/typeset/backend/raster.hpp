#ifndef TYPESET_BACKEND_RASTER_HPP
#define TYPESET_BACKEND_RASTER_HPP

#include <cstdint>
#include <memory>
#include <vector>

#include "typeset/dl/display_list.hpp"

/**
 * backend/raster — 表示リストをピクセルへ
 *
 * グリフは glyphware（FreeType）にカバレッジマスクを作らせ（変形はアウトラインに
 * 焼き込み）、自前で合成する。パスは自前のスキャンライン（AA）で塗る。
 * richtext の Raster.cpp / GlyphRenderer.cpp の移植。
 */
namespace typeset::backend {

/**
 * ARGB8888（非前乗算）画像
 */
struct Bitmap {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> argb;

    uint32_t* row(int y) { return argb.data() + static_cast<size_t>(y) * width; }
    const uint32_t* row(int y) const { return argb.data() + static_cast<size_t>(y) * width; }
};

struct RasterOptions {
    float dpi = 72.0f;
    Color background{255, 255, 255, 255};   ///< a=0 で透明
    bool useCache = true;
    /**
     * アンチエイリアス。false でカバレッジを閾値で 2 値化して描く（小サイズのゲーム用途。にじみが出ない）。
     * グリッドフィット（ヒンティング）は glyphware のマスク API が持たないので効かない
     */
    bool antialias = true;
    /// 2 値化の閾値（0〜255。カバレッジがこれ以上なら塗る）
    uint8_t alphaThreshold = 128;
};

class RasterRenderer {
public:
    RasterRenderer();
    ~RasterRenderer();

    RasterRenderer(const RasterRenderer&) = delete;
    RasterRenderer& operator=(const RasterRenderer&) = delete;

    /**
     * ページ全体を描いた Bitmap を返す
     */
    Bitmap render(const dl::DisplayList& list, const RasterOptions& opts = {});

    /**
     * 既存バッファへ描く（組み込み用）
     * @param pixels ARGB8888、stridePixels はピクセル単位
     * @param toDevice ページ座標（pt）→ ピクセル座標
     */
    /// @param alphaThreshold 0 でアンチエイリアス、> 0 でカバレッジをその値で 2 値化する
    void render(const dl::DisplayList& list,
                uint32_t* pixels, int width, int height, int stridePixels,
                const Matrix& toDevice, bool useCache = true, uint8_t alphaThreshold = 0);

    void clearCache();
    void setCacheMaxBytes(size_t bytes);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// PNG 書き出し（ラスタ backend の確認用。zlib で圧縮）
bool savePng(const Bitmap& bitmap, const std::string& path);

} // namespace typeset::backend

#endif // TYPESET_BACKEND_RASTER_HPP
