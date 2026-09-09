/**
 * raster.cpp — 表示リスト → ARGB8888
 *
 * グリフのマスク生成は glyphware（FreeType）に任せ、ここは合成だけを行う。
 * richtext の Raster.cpp / GlyphRenderer.cpp の移植。
 */

#include "typeset/backend/raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <fstream>
#include <unordered_map>

#include <glyphware/Face.h>

#include "typeset/font/font_set.hpp"
#include "typeset/image/image.hpp"
#include "path_raster.hpp"

namespace typeset::backend {

namespace {

//------------------------------------------------------------------------------
// 描画先
//------------------------------------------------------------------------------

struct Target {
    uint32_t* pixels = nullptr;
    int width = 0, height = 0, stride = 0;
    // クリップ（ピクセル、[x0,x1)×[y0,y1)）
    int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;

    uint32_t* row(int y) const { return pixels + static_cast<ptrdiff_t>(stride) * y; }
};

inline uint32_t pack(uint32_t a, uint32_t r, uint32_t g, uint32_t b) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/// SRC_OVER（非前乗算 ARGB。sa は 0..255 の実効アルファ）
inline void blendPixel(uint32_t& dst, uint32_t sr, uint32_t sg, uint32_t sb, uint32_t sa) {
    if (sa == 0) return;
    if (sa == 255) {
        dst = pack(255, sr, sg, sb);
        return;
    }
    const uint32_t da = (dst >> 24) & 0xFF;
    const uint32_t dr = (dst >> 16) & 0xFF;
    const uint32_t dg = (dst >> 8) & 0xFF;
    const uint32_t db = dst & 0xFF;
    const uint32_t ia = 255 - sa;
    const uint32_t contrib = da * ia / 255;
    const uint32_t outA = sa + contrib;
    if (outA == 0) { dst = 0; return; }
    const uint32_t outR = (sr * sa + dr * contrib) / outA;
    const uint32_t outG = (sg * sa + dg * contrib) / outA;
    const uint32_t outB = (sb * sa + db * contrib) / outA;
    dst = pack(outA, outR, outG, outB);
}

inline uint32_t effectiveAlpha(Color c, float opacity) {
    return static_cast<uint32_t>(std::clamp(c.a * opacity + 0.5f, 0.0f, 255.0f));
}

/// 8bit カバレッジマスクを単色で合成
void blendMask(const Target& t, const uint8_t* mask, int maskW, int maskH, int pitch,
               int originX, int originY, Color c, uint32_t alpha) {
    if (!mask || maskW <= 0 || maskH <= 0 || alpha == 0) return;
    const int sy0 = std::max(0, t.cy0 - originY);
    const int sy1 = std::min(maskH, t.cy1 - originY);
    const int sx0 = std::max(0, t.cx0 - originX);
    const int sx1 = std::min(maskW, t.cx1 - originX);
    if (sx0 >= sx1 || sy0 >= sy1) return;

    for (int my = sy0; my < sy1; ++my) {
        const uint8_t* src = mask + static_cast<ptrdiff_t>(pitch) * my;
        uint32_t* dst = t.row(originY + my);
        for (int mx = sx0; mx < sx1; ++mx) {
            const uint32_t cov = src[mx];
            if (!cov) continue;
            blendPixel(dst[originX + mx], c.r, c.g, c.b, (cov * alpha + 127) / 255);
        }
    }
}

/// 軸に平行な矩形（サブピクセル境界は被覆率で按分）
void fillRectF(const Target& t, float x, float y, float w, float h, Color c, uint32_t alpha) {
    if (w <= 0.0f || h <= 0.0f || alpha == 0) return;
    const float fx0 = x, fy0 = y, fx1 = x + w, fy1 = y + h;
    const int ix0 = std::max(t.cx0, static_cast<int>(std::floor(fx0)));
    const int iy0 = std::max(t.cy0, static_cast<int>(std::floor(fy0)));
    const int ix1 = std::min(t.cx1, static_cast<int>(std::ceil(fx1)));
    const int iy1 = std::min(t.cy1, static_cast<int>(std::ceil(fy1)));
    if (ix0 >= ix1 || iy0 >= iy1) return;

    for (int py = iy0; py < iy1; ++py) {
        const float top = std::max(fy0, static_cast<float>(py));
        const float bottom = std::min(fy1, static_cast<float>(py + 1));
        const float covY = bottom - top;
        if (covY <= 0.0f) continue;
        uint32_t* dst = t.row(py);
        for (int px = ix0; px < ix1; ++px) {
            const float left = std::max(fx0, static_cast<float>(px));
            const float right = std::min(fx1, static_cast<float>(px + 1));
            const float covX = right - left;
            if (covX <= 0.0f) continue;
            blendPixel(dst[px], c.r, c.g, c.b,
                       static_cast<uint32_t>(alpha * covX * covY + 0.5f));
        }
    }
}

/// RGBA8888 画像をアフィン変換して合成（バイリニア）
void blendImage(const Target& t, const uint8_t* rgba, int imgW, int imgH,
                const Matrix& m, uint32_t alpha) {
    if (!rgba || imgW <= 0 || imgH <= 0 || alpha == 0) return;

    const float cx[4] = {0.0f, static_cast<float>(imgW), static_cast<float>(imgW), 0.0f};
    const float cy[4] = {0.0f, 0.0f, static_cast<float>(imgH), static_cast<float>(imgH)};
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (int i = 0; i < 4; ++i) {
        const Point p = m.apply({cx[i], cy[i]});
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    const int x0 = std::max(t.cx0, static_cast<int>(std::floor(minX)));
    const int y0 = std::max(t.cy0, static_cast<int>(std::floor(minY)));
    const int x1 = std::min(t.cx1, static_cast<int>(std::ceil(maxX)) + 1);
    const int y1 = std::min(t.cy1, static_cast<int>(std::ceil(maxY)) + 1);
    if (x0 >= x1 || y0 >= y1) return;

    bool ok = false;
    const Matrix inv = invert(m, &ok);
    if (!ok) return;

    for (int y = y0; y < y1; ++y) {
        uint32_t* dst = t.row(y);
        for (int x = x0; x < x1; ++x) {
            const Point s = inv.apply({static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f});
            const float u = s.x - 0.5f, v = s.y - 0.5f;
            if (u < -1.0f || v < -1.0f || u > imgW || v > imgH) continue;
            const int u0 = static_cast<int>(std::floor(u));
            const int v0 = static_cast<int>(std::floor(v));
            const float fu = u - u0, fv = v - v0;
            float acc[4] = {0, 0, 0, 0};
            for (int dy = 0; dy < 2; ++dy) {
                const int sy = std::clamp(v0 + dy, 0, imgH - 1);
                const float wy = dy ? fv : (1.0f - fv);
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = std::clamp(u0 + dx, 0, imgW - 1);
                    const float w = wy * (dx ? fu : (1.0f - fu));
                    if (w <= 0.0f) continue;
                    const uint8_t* p = rgba + (static_cast<ptrdiff_t>(sy) * imgW + sx) * 4;
                    const float sa = p[3] / 255.0f;
                    acc[0] += p[0] * sa * w;
                    acc[1] += p[1] * sa * w;
                    acc[2] += p[2] * sa * w;
                    acc[3] += p[3] * w;
                }
            }
            const uint32_t sa = static_cast<uint32_t>(std::clamp(acc[3], 0.0f, 255.0f));
            if (!sa) continue;
            const float unpre = 255.0f / std::max(1.0f, acc[3]);
            const uint32_t sr = static_cast<uint32_t>(std::clamp(acc[0] * unpre, 0.0f, 255.0f));
            const uint32_t sg = static_cast<uint32_t>(std::clamp(acc[1] * unpre, 0.0f, 255.0f));
            const uint32_t sb = static_cast<uint32_t>(std::clamp(acc[2] * unpre, 0.0f, 255.0f));
            blendPixel(dst[x], sr, sg, sb, (sa * alpha + 127) / 255);
        }
    }
}

//------------------------------------------------------------------------------
// グリフマスクのキャッシュ
//------------------------------------------------------------------------------

// マスクは変形を焼き込んだ結果なので、キーは 2x2 成分とサブピクセル位相まで含める
// （平行移動の整数部だけがキャッシュ間で共有される）
struct MaskKey {
    uintptr_t face;
    uint32_t gid;
    int32_t xxQ, xyQ, yxQ, yyQ;   // * 4096
    uint32_t strokeQ;             // * 64
    uint8_t phaseX, phaseY;       // 1/4 px

    bool operator==(const MaskKey& o) const {
        return face == o.face && gid == o.gid && xxQ == o.xxQ && xyQ == o.xyQ &&
               yxQ == o.yxQ && yyQ == o.yyQ && strokeQ == o.strokeQ &&
               phaseX == o.phaseX && phaseY == o.phaseY;
    }
};

struct MaskKeyHash {
    size_t operator()(const MaskKey& k) const {
        size_t h = k.face;
        h ^= static_cast<size_t>(k.gid) * 2654435761u;
        h ^= static_cast<size_t>(k.xxQ) * 3266489917u;
        h ^= static_cast<size_t>(k.xyQ) * 374761393u;
        h ^= static_cast<size_t>(k.yxQ) * 2246822519u;
        h ^= static_cast<size_t>(k.yyQ) * 668265263u;
        h ^= static_cast<size_t>(k.strokeQ) * 40503u;
        h ^= static_cast<size_t>(k.phaseX) * 31u + static_cast<size_t>(k.phaseY) * 131u;
        return h;
    }
};

struct CachedMask {
    std::vector<uint8_t> coverage;
    int left = 0, top = 0, width = 0, rows = 0;   // top は y-up
};

inline int32_t quantizeMat(float v) { return static_cast<int32_t>(std::lround(v * 4096.0f)); }

inline uint8_t quantizePhase(float v, float& base) {
    base = std::floor(v);
    const int q = static_cast<int>((v - base) * 4.0f + 0.5f) & 3;
    return static_cast<uint8_t>(q);
}

glyphware::StrokeJoin toGw(StrokeJoin j) {
    switch (j) {
    case StrokeJoin::Miter: return glyphware::StrokeJoin::Miter;
    case StrokeJoin::Bevel: return glyphware::StrokeJoin::Bevel;
    default: return glyphware::StrokeJoin::Round;
    }
}
glyphware::StrokeCap toGw(StrokeCap c) {
    switch (c) {
    case StrokeCap::Butt: return glyphware::StrokeCap::Butt;
    case StrokeCap::Square: return glyphware::StrokeCap::Square;
    default: return glyphware::StrokeCap::Round;
    }
}

} // namespace

//------------------------------------------------------------------------------

struct RasterRenderer::Impl {
    std::unordered_map<MaskKey, CachedMask, MaskKeyHash> cache;
    size_t cacheBytes = 0;
    size_t cacheMax = 64u * 1024u * 1024u;

    void evict() {
        if (cacheMax && cacheBytes > cacheMax) {
            cache.clear();
            cacheBytes = 0;
        }
    }

    void drawItems(Target& t, const std::vector<dl::Item>& items, const Matrix& ctm,
                   float opacity, bool useCache);
    void drawGlyphRun(Target& t, const dl::GlyphRun& run, const Matrix& ctm, float opacity,
                      bool useCache);
    /// ぼかし付きの GlyphRun（影）。行全体のカバレッジを 1 枚に集めてぼかし、1 回で合成する
    void drawBlurredGlyphRun(Target& t, const dl::GlyphRun& run, const Matrix& ctm, float opacity);
    void drawPath(Target& t, const dl::PathItem& item, const Matrix& ctm, float opacity);
    void drawRect(Target& t, const dl::RectItem& item, const Matrix& ctm, float opacity);
    void drawImage(Target& t, const dl::ImageItem& item, const Matrix& ctm, float opacity);

    /**
     * グリフ 1 つを合成する
     * @param m フォントユニット(y-up) → デバイス(y-down) の変換
     */
    void blendGlyph(Target& t, glyphware::Face& face, uint32_t gid, const Matrix& m,
                    float strokeWidth, StrokeJoin join, StrokeCap cap,
                    Color color, uint32_t alpha, bool useCache);
};

void RasterRenderer::Impl::blendGlyph(Target& t, glyphware::Face& face, uint32_t gid,
                                      const Matrix& m, float strokeWidth,
                                      StrokeJoin join, StrokeCap cap,
                                      Color color, uint32_t alpha, bool useCache) {
    if (alpha == 0) return;

    // バックエンドは y-up で受け取るので y 行を反転して渡す。返るマスクは
    // (left, -top) を左上として y-down に並ぶ
    glyphware::RenderParams params;
    params.transform.xx = m.xx;
    params.transform.xy = m.xy;
    params.transform.dx = m.dx;
    params.transform.yx = -m.yx;
    params.transform.yy = -m.yy;
    params.transform.dy = -m.dy;
    params.strokeWidth = strokeWidth;
    params.join = toGw(join);
    params.cap = toGw(cap);

    if (!useCache) {
        glyphware::GlyphMask mask;
        if (!face.renderGlyphMask(gid, params, mask)) return;
        blendMask(t, mask.buffer, mask.width, mask.rows, mask.pitch, mask.left, -mask.top,
                  color, alpha);
        return;
    }

    float baseX = 0.0f, baseY = 0.0f;
    const uint8_t phaseX = quantizePhase(params.transform.dx, baseX);
    const uint8_t phaseY = quantizePhase(params.transform.dy, baseY);

    MaskKey key{};
    key.face = reinterpret_cast<uintptr_t>(&face);
    key.gid = gid;
    key.xxQ = quantizeMat(params.transform.xx);
    key.xyQ = quantizeMat(params.transform.xy);
    key.yxQ = quantizeMat(params.transform.yx);
    key.yyQ = quantizeMat(params.transform.yy);
    key.strokeQ = static_cast<uint32_t>(strokeWidth * 64.0f + 0.5f);
    key.phaseX = phaseX;
    key.phaseY = phaseY;

    auto it = cache.find(key);
    if (it == cache.end()) {
        glyphware::RenderParams p = params;
        p.transform.dx = phaseX * 0.25f;
        p.transform.dy = phaseY * 0.25f;
        glyphware::GlyphMask mask;
        if (!face.renderGlyphMask(gid, p, mask)) return;

        CachedMask cm;
        cm.left = mask.left;
        cm.top = mask.top;
        cm.width = mask.width;
        cm.rows = mask.rows;
        cm.coverage.resize(static_cast<size_t>(mask.width) * mask.rows);
        for (int r = 0; r < mask.rows; ++r) {
            std::memcpy(cm.coverage.data() + static_cast<size_t>(r) * mask.width,
                        mask.buffer + static_cast<ptrdiff_t>(mask.pitch) * r,
                        static_cast<size_t>(mask.width));
        }
        cacheBytes += cm.coverage.size();
        it = cache.emplace(key, std::move(cm)).first;
        evict();
        it = cache.find(key);
        if (it == cache.end()) return;
    }

    const CachedMask& cm = it->second;
    if (cm.width <= 0 || cm.rows <= 0) return;
    const int ox = static_cast<int>(baseX) + cm.left;
    const int oy = -(static_cast<int>(baseY) + cm.top);
    blendMask(t, cm.coverage.data(), cm.width, cm.rows, cm.width, ox, oy, color, alpha);
}

namespace {

/// ガウスぼかしを 3 回のボックスぼかしで近似するときの各箱の半径
std::array<int, 3> boxRadiiForGauss(float sigma) {
    const int n = 3;
    const float wIdeal = std::sqrt(12.0f * sigma * sigma / n + 1.0f);
    int wl = static_cast<int>(std::floor(wIdeal));
    if (wl % 2 == 0) --wl;
    const int wu = wl + 2;
    const float mIdeal = (12.0f * sigma * sigma - n * wl * wl - 4.0f * n * wl - 3.0f * n) / (-4.0f * wl - 4.0f);
    const int m = static_cast<int>(std::lround(mIdeal));
    std::array<int, 3> r{};
    for (int i = 0; i < n; ++i) r[static_cast<size_t>(i)] = std::max(0, ((i < m ? wl : wu) - 1) / 2);
    return r;
}

/// 1 軸のボックスぼかし（範囲外は 0）。src → dst、長さ n、要素間隔 stride
void boxBlurLine(const uint8_t* src, uint8_t* dst, int n, int stride, int r) {
    if (r <= 0) {
        for (int i = 0; i < n; ++i) dst[i * stride] = src[i * stride];
        return;
    }
    const int win = 2 * r + 1;
    int sum = 0;
    for (int i = 0; i < std::min(n, r); ++i) sum += src[i * stride];
    for (int i = 0; i < n; ++i) {
        const int add = i + r;
        const int sub = i - r - 1;
        if (add < n) sum += src[add * stride];
        if (sub >= 0) sum -= src[sub * stride];
        dst[i * stride] = static_cast<uint8_t>((sum + win / 2) / win);
    }
}

void gaussianBlur(std::vector<uint8_t>& a, int w, int h, float sigma) {
    if (sigma <= 0.0f || w <= 0 || h <= 0) return;
    std::vector<uint8_t> tmp(a.size());
    for (int r : boxRadiiForGauss(sigma)) {
        for (int y = 0; y < h; ++y) boxBlurLine(a.data() + static_cast<size_t>(y) * w, tmp.data() + static_cast<size_t>(y) * w, w, 1, r);
        for (int x = 0; x < w; ++x) boxBlurLine(tmp.data() + x, a.data() + x, h, w, r);
    }
}

} // namespace

void RasterRenderer::Impl::drawBlurredGlyphRun(Target& t, const dl::GlyphRun& run, const Matrix& ctm,
                                               float opacity) {
    glyphware::Face& face = *run.face;
    const float upem = font::unitsPerEm(face);
    const float s = run.size / upem;
    Matrix base;
    base.xx = s; base.yy = -s;
    const float devScale = std::sqrt(std::fabs(ctm.determinant()));
    const float sigma = run.blur * devScale * 0.5f;
    const int margin = static_cast<int>(std::ceil(sigma * 3.0f)) + 1;

    struct Mask { int left, top, w, h; std::vector<uint8_t> a; };
    std::vector<Mask> masks;
    int x0 = std::numeric_limits<int>::max(), y0 = x0, x1 = std::numeric_limits<int>::min(), y1 = x1;
    auto addMask = [&](uint32_t gid, const Matrix& m, float strokeWidth, StrokeJoin join, StrokeCap cap) {
        glyphware::RenderParams params;
        params.transform.xx = m.xx;
        params.transform.xy = m.xy;
        params.transform.dx = m.dx;
        params.transform.yx = -m.yx;
        params.transform.yy = -m.yy;
        params.transform.dy = -m.dy;
        params.strokeWidth = strokeWidth;
        params.join = toGw(join);
        params.cap = toGw(cap);
        glyphware::GlyphMask gm;
        if (!face.renderGlyphMask(gid, params, gm) || gm.width <= 0 || gm.rows <= 0) return;
        Mask mk{gm.left, -gm.top, gm.width, gm.rows, {}};
        mk.a.resize(static_cast<size_t>(gm.width) * gm.rows);
        for (int r = 0; r < gm.rows; ++r) {
            std::memcpy(mk.a.data() + static_cast<size_t>(r) * gm.width,
                        gm.buffer + static_cast<ptrdiff_t>(gm.pitch) * r, static_cast<size_t>(gm.width));
        }
        x0 = std::min(x0, mk.left);
        y0 = std::min(y0, mk.top);
        x1 = std::max(x1, mk.left + mk.w);
        y1 = std::max(y1, mk.top + mk.h);
        masks.push_back(std::move(mk));
    };

    // 塗りと縁取りを同じカバレッジへ集める（色は塗り優先。影は普通どちらか一方）
    const Color color = run.fill ? *run.fill : run.stroke->color;
    for (const dl::Glyph& g : run.glyphs) {
        Matrix m = multiply(Matrix::fromMat2(g.xform), base);
        m = multiply(Matrix::translation(g.pos.x, g.pos.y), m);
        m = multiply(ctm, m);
        if (run.fill) {
            addMask(g.gid, m, run.embolden > 0.0f ? run.embolden * devScale : 0.0f, StrokeJoin::Round, StrokeCap::Round);
        }
        if (run.stroke && run.stroke->width > 0.0f) {
            addMask(g.gid, m, (run.stroke->width + run.embolden) * devScale, run.stroke->join, run.stroke->cap);
        }
    }
    if (masks.empty()) return;

    // クリップの外はぼかしの届く範囲だけ残す
    x0 = std::max(x0 - margin, t.cx0 - margin);
    y0 = std::max(y0 - margin, t.cy0 - margin);
    x1 = std::min(x1 + margin, t.cx1 + margin);
    y1 = std::min(y1 + margin, t.cy1 + margin);
    const int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    std::vector<uint8_t> cov(static_cast<size_t>(w) * h, 0);
    for (const Mask& mk : masks) {
        for (int r = 0; r < mk.h; ++r) {
            const int y = mk.top + r - y0;
            if (y < 0 || y >= h) continue;
            for (int c = 0; c < mk.w; ++c) {
                const int x = mk.left + c - x0;
                if (x < 0 || x >= w) continue;
                uint8_t& d = cov[static_cast<size_t>(y) * w + x];
                d = std::max(d, mk.a[static_cast<size_t>(r) * mk.w + c]);
            }
        }
    }
    gaussianBlur(cov, w, h, sigma);
    blendMask(t, cov.data(), w, h, w, x0, y0, color, effectiveAlpha(color, opacity));
}

void RasterRenderer::Impl::drawGlyphRun(Target& t, const dl::GlyphRun& run, const Matrix& ctm,
                                        float opacity, bool useCache) {
    if (!run.face || run.glyphs.empty()) return;
    if (run.blur > 0.0f && (run.fill || run.stroke)) {
        drawBlurredGlyphRun(t, run, ctm, opacity);
        return;
    }
    glyphware::Face& face = *run.face;
    const float upem = font::unitsPerEm(face);
    const float s = run.size / upem;
    // フォントユニット(y-up) → pt(y-down)
    Matrix base;
    base.xx = s; base.yy = -s;

    // デバイスのスケール（線幅用）
    const float devScale = std::sqrt(std::fabs(ctm.determinant()));

    for (const dl::Glyph& g : run.glyphs) {
        Matrix m = multiply(Matrix::fromMat2(g.xform), base);
        m = multiply(Matrix::translation(g.pos.x, g.pos.y), m);
        m = multiply(ctm, m);

        if (run.fill) {
            const uint32_t a = effectiveAlpha(*run.fill, opacity);
            blendGlyph(t, face, g.gid, m, 0.0f, StrokeJoin::Round, StrokeCap::Round,
                       *run.fill, a, useCache);
            if (run.embolden > 0.0f) {
                blendGlyph(t, face, g.gid, m, run.embolden * devScale,
                           StrokeJoin::Round, StrokeCap::Round, *run.fill, a, useCache);
            }
        }
        if (run.stroke && run.stroke->width > 0.0f) {
            const uint32_t a = effectiveAlpha(run.stroke->color, opacity);
            blendGlyph(t, face, g.gid, m, (run.stroke->width + run.embolden) * devScale,
                       run.stroke->join, run.stroke->cap, run.stroke->color, a, useCache);
        }
    }
}

void RasterRenderer::Impl::drawPath(Target& t, const dl::PathItem& item, const Matrix& ctm,
                                    float opacity) {
    if (item.path.empty()) return;
    const Path dev = item.path.transformed(ctm);
    std::vector<bool> closed;
    const auto polys = flattenPath(dev, 0.2f, &closed);
    if (polys.empty()) return;

    detail::Coverage cov;
    if (item.fill) {
        if (detail::rasterizePolygons(polys, item.evenOdd, t.width, t.height, cov)) {
            blendMask(t, cov.a.data(), cov.width, cov.height, cov.width, cov.x0, cov.y0,
                      *item.fill, effectiveAlpha(*item.fill, opacity));
        }
    }
    if (item.stroke && item.stroke->width > 0.0f) {
        const float devScale = std::sqrt(std::fabs(ctm.determinant()));
        const auto strokePolys = detail::strokeToPolygons(
            polys, closed, item.stroke->width * devScale, item.stroke->join, item.stroke->cap);
        if (detail::rasterizePolygons(strokePolys, false, t.width, t.height, cov)) {
            blendMask(t, cov.a.data(), cov.width, cov.height, cov.width, cov.x0, cov.y0,
                      item.stroke->color, effectiveAlpha(item.stroke->color, opacity));
        }
    }
}

void RasterRenderer::Impl::drawRect(Target& t, const dl::RectItem& item, const Matrix& ctm,
                                    float opacity) {
    if (ctm.xy == 0.0f && ctm.yx == 0.0f) {
        const Point a = ctm.apply({item.rect.x, item.rect.y});
        const Point b = ctm.apply({item.rect.right(), item.rect.bottom()});
        const float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
        fillRectF(t, x0, y0, std::fabs(b.x - a.x), std::fabs(b.y - a.y), item.fill,
                  effectiveAlpha(item.fill, opacity));
        return;
    }
    dl::PathItem p;
    p.path.addRect(item.rect);
    p.fill = item.fill;
    drawPath(t, p, ctm, opacity);
}

void RasterRenderer::Impl::drawImage(Target& t, const dl::ImageItem& item, const Matrix& ctm,
                                     float opacity) {
    if (!item.image || item.image->rgba.empty()) return;
    const Matrix m = multiply(ctm, item.xform);
    blendImage(t, item.image->rgba.data(), item.image->width, item.image->height, m,
               static_cast<uint32_t>(std::clamp(255.0f * item.opacity * opacity + 0.5f, 0.0f, 255.0f)));
}

void RasterRenderer::Impl::drawItems(Target& t, const std::vector<dl::Item>& items,
                                     const Matrix& ctm, float opacity, bool useCache) {
    for (const dl::Item& item : items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            drawGlyphRun(t, *run, ctm, opacity, useCache);
        } else if (const auto* path = std::get_if<dl::PathItem>(&item)) {
            drawPath(t, *path, ctm, opacity);
        } else if (const auto* rect = std::get_if<dl::RectItem>(&item)) {
            drawRect(t, *rect, ctm, opacity);
        } else if (const auto* img = std::get_if<dl::ImageItem>(&item)) {
            drawImage(t, *img, ctm, opacity);
        } else if (const auto* group = std::get_if<dl::Group>(&item)) {
            Target sub = t;
            if (group->clip) {
                // 親座標系のクリップ矩形をデバイスへ（回転があれば外接矩形）
                const Rect& c = *group->clip;
                const Point p[4] = {ctm.apply({c.x, c.y}), ctm.apply({c.right(), c.y}),
                                    ctm.apply({c.right(), c.bottom()}), ctm.apply({c.x, c.bottom()})};
                float x0 = p[0].x, y0 = p[0].y, x1 = x0, y1 = y0;
                for (const Point& q : p) {
                    x0 = std::min(x0, q.x); y0 = std::min(y0, q.y);
                    x1 = std::max(x1, q.x); y1 = std::max(y1, q.y);
                }
                sub.cx0 = std::max(t.cx0, static_cast<int>(std::floor(x0)));
                sub.cy0 = std::max(t.cy0, static_cast<int>(std::floor(y0)));
                sub.cx1 = std::min(t.cx1, static_cast<int>(std::ceil(x1)));
                sub.cy1 = std::min(t.cy1, static_cast<int>(std::ceil(y1)));
                if (sub.cx0 >= sub.cx1 || sub.cy0 >= sub.cy1) continue;
            }
            drawItems(sub, group->children, multiply(ctm, group->xform),
                      opacity * group->opacity, useCache);
        }
    }
}

//------------------------------------------------------------------------------

RasterRenderer::RasterRenderer() : impl_(std::make_unique<Impl>()) {}
RasterRenderer::~RasterRenderer() = default;

void RasterRenderer::clearCache() {
    impl_->cache.clear();
    impl_->cacheBytes = 0;
}

void RasterRenderer::setCacheMaxBytes(size_t bytes) { impl_->cacheMax = bytes; }

void RasterRenderer::render(const dl::DisplayList& list,
                            uint32_t* pixels, int width, int height, int stridePixels,
                            const Matrix& toDevice, bool useCache) {
    if (!pixels || width <= 0 || height <= 0) return;
    Target t;
    t.pixels = pixels;
    t.width = width;
    t.height = height;
    t.stride = stridePixels;
    t.cx0 = 0; t.cy0 = 0; t.cx1 = width; t.cy1 = height;
    impl_->drawItems(t, list.items, toDevice, 1.0f, useCache);
}

Bitmap RasterRenderer::render(const dl::DisplayList& list, const RasterOptions& opts) {
    Bitmap bmp;
    const float scale = opts.dpi / 72.0f;
    bmp.width = std::max(1, static_cast<int>(std::ceil(list.page.w * scale)));
    bmp.height = std::max(1, static_cast<int>(std::ceil(list.page.h * scale)));
    bmp.argb.assign(static_cast<size_t>(bmp.width) * bmp.height, opts.background.toArgb());
    render(list, bmp.argb.data(), bmp.width, bmp.height, bmp.width,
           Matrix::scaling(scale, scale), opts.useCache);
    return bmp;
}

//------------------------------------------------------------------------------
// PNG
//------------------------------------------------------------------------------

bool savePng(const Bitmap& bmp, const std::string& path) {
    if (bmp.width <= 0 || bmp.height <= 0) return false;
    // ARGB → RGBA
    std::vector<uint8_t> rgba(static_cast<size_t>(bmp.width) * bmp.height * 4);
    for (int y = 0; y < bmp.height; ++y) {
        const uint32_t* row = bmp.row(y);
        uint8_t* dst = rgba.data() + static_cast<size_t>(y) * bmp.width * 4;
        for (int x = 0; x < bmp.width; ++x) {
            const uint32_t p = row[x];
            dst[x * 4 + 0] = static_cast<uint8_t>((p >> 16) & 0xFF);
            dst[x * 4 + 1] = static_cast<uint8_t>((p >> 8) & 0xFF);
            dst[x * 4 + 2] = static_cast<uint8_t>(p & 0xFF);
            dst[x * 4 + 3] = static_cast<uint8_t>((p >> 24) & 0xFF);
        }
    }
    const std::string png = image::encodePng(rgba.data(), bmp.width, bmp.height);
    if (png.empty()) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(png.data(), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(f);
}

} // namespace typeset::backend
