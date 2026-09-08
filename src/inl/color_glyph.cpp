/**
 * color_glyph.cpp — COLR レイヤ／ビットマップ絵文字 → 表示リスト
 */

#include "color_glyph.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "typeset/font/font_set.hpp"

namespace typeset::inl {

namespace {

/// アウトライン → Path（フォントユニット、y-up のまま。変換は後で行列でまとめて掛ける）
class PathSink : public glyphware::OutlineSink {
public:
    Path path;
    void moveTo(float x, float y) override { path.moveTo(x, y); }
    void lineTo(float x, float y) override { path.lineTo(x, y); }
    void quadTo(float cx, float cy, float x, float y) override { path.quadTo(cx, cy, x, y); }
    void cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override {
        path.cubicTo(c1x, c1y, c2x, c2y, x, y);
    }
    void close() override { path.close(); }
};

Color paintColor(const glyphware::ColorPaint& p) {
    if (p.kind == glyphware::PaintKind::Solid || p.stops.empty()) return Color{p.r, p.g, p.b, p.a};
    // グラデーション: 色止めの平均で近似する
    float r = 0, g = 0, b = 0, a = 0;
    for (const glyphware::ColorStop& s : p.stops) { r += s.r; g += s.g; b += s.b; a += s.a; }
    const float n = static_cast<float>(p.stops.size());
    return Color{static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n), static_cast<uint8_t>(b / n),
                 static_cast<uint8_t>(a / n)};
}

/// ペン原点の y-up ローカル座標（単位: 1 = 1pt になるよう scale 済み）→ 物理
Matrix penMatrix(Point pos, const Mat2& xform, float scale) {
    // 物理 = pos + xform * (scale * (x, −y))
    Matrix flip;
    flip.xx = scale; flip.yy = -scale;
    return multiply(Matrix::translation(pos.x, pos.y), multiply(Matrix::fromMat2(xform), flip));
}

bool emitLayers(dl::DisplayList& out, glyphware::Face& face, uint32_t gid, Pt size, Point pos,
                const Mat2& xform) {
    const float upem = font::unitsPerEm(face);
    if (upem <= 0.0f) return false;
    // ピクセルサイズを upem にすると、レイヤの transform（ピクセル空間）がそのままフォントユニットになる
    face.setPixelSize(static_cast<int>(upem));
    std::vector<glyphware::ColorLayer> layers;
    if (!face.colorLayers(gid, layers) || layers.empty()) return false;
    // 走査に失敗して同じレイヤの繰り返しになっているときは使わない（通常のグリフとして出す）
    if (layers.size() > 1) {
        bool allSame = true;
        for (const glyphware::ColorLayer& l : layers) if (l.gid != layers.front().gid) { allSame = false; break; }
        if (allSame) return false;
    }

    const Matrix pen = penMatrix(pos, xform, size / upem);
    for (const glyphware::ColorLayer& layer : layers) {
        PathSink sink;
        if (!face.glyphOutline(layer.gid, sink) || sink.path.empty()) continue;
        Matrix lt;
        lt.xx = layer.transform[0]; lt.xy = layer.transform[1]; lt.dx = layer.transform[2];
        lt.yx = layer.transform[3]; lt.yy = layer.transform[4]; lt.dy = layer.transform[5];
        const Color c = paintColor(layer.paint);
        if (c.a == 0) continue;
        dl::PathItem item;
        item.path = sink.path.transformed(multiply(pen, lt));
        item.fill = c;
        out.add(std::move(item));
    }
    return true;
}

bool emitBitmap(dl::DisplayList& out, glyphware::Face& face, uint32_t gid, Pt size, Point pos,
                const Mat2& xform) {
    // ビットマップは大きめの strike を選んで縮めて置く（PDF / SVG でも粗くならないように）
    const int px = std::clamp(static_cast<int>(std::lround(size * 4.0f)), 32, 512);
    face.setPixelSize(px);
    glyphware::GlyphBitmap bm;
    if (!face.glyphBitmap(gid, true, bm) || bm.format != glyphware::BitmapFormat::BGRA ||
        bm.width <= 0 || bm.rows <= 0 || !bm.buffer) {
        return false;
    }
    auto img = std::make_shared<dl::Image>();
    img->width = bm.width;
    img->height = bm.rows;
    img->rgba.resize(static_cast<size_t>(bm.width) * bm.rows * 4);
    for (int y = 0; y < bm.rows; ++y) {
        const uint8_t* row = bm.buffer + (bm.pitch >= 0 ? y * bm.pitch : (bm.rows - 1 - y) * -bm.pitch);
        for (int x = 0; x < bm.width; ++x) {
            const uint8_t b = row[x * 4 + 0], g = row[x * 4 + 1], r = row[x * 4 + 2], a = row[x * 4 + 3];
            uint8_t* o = &img->rgba[(static_cast<size_t>(y) * bm.width + x) * 4];
            // FreeType の BGRA は前乗算。非前乗算に戻す
            if (a > 0 && a < 255) {
                o[0] = static_cast<uint8_t>(std::min(255, r * 255 / a));
                o[1] = static_cast<uint8_t>(std::min(255, g * 255 / a));
                o[2] = static_cast<uint8_t>(std::min(255, b * 255 / a));
            } else {
                o[0] = r; o[1] = g; o[2] = b;
            }
            o[3] = a;
        }
    }
    // glyphware は固定 strike のビットマップを要求ピクセルサイズに拡縮して返すので、px で割る
    const float s = size / static_cast<float>(px);   // 1px あたりの pt
    // ピクセル (i, j) → ペン原点の y-down ローカル: (left + i, −top + j) × s
    Matrix local = multiply(Matrix::translation(bm.left * s, -bm.top * s), Matrix::scaling(s, s));
    dl::ImageItem item;
    item.image = img;
    item.xform = multiply(Matrix::translation(pos.x, pos.y), multiply(Matrix::fromMat2(xform), local));
    out.add(std::move(item));
    return true;
}

} // namespace

bool emitColorGlyph(dl::DisplayList& out, glyphware::Face& face, uint32_t gid, Pt size, Point pos,
                    const Mat2& xform) {
    if (!face.descriptor().color || gid == 0) return false;
    if (emitLayers(out, face, gid, size, pos, xform)) return true;
    return emitBitmap(out, face, gid, size, pos, xform);
}

} // namespace typeset::inl
