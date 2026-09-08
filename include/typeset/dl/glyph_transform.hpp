#ifndef TYPESET_DL_GLYPH_TRANSFORM_HPP
#define TYPESET_DL_GLYPH_TRANSFORM_HPP

#include <cmath>

#include "typeset/geom.hpp"

/**
 * glyph_transform — グリフ固有の変形の組み立て
 *
 * ラスタ・PDF・SVG の 3 backend が同じ行列を使うために 1 箇所に置く
 * （richtext の GlyphTransform.hpp と同じ理由。分かれていると斜体や横倒しが
 * 出力先ごとにずれる）。
 */
namespace typeset::dl {

/// フェイクイタリックのシアー係数（Android と同じ -0.25 ≒ 14度）
inline constexpr float kFakeItalicSkew = -0.25f;

/**
 * グリフ固有の変形を 1 つの 2x2 へ畳む（ペン原点・y-down）
 *
 * 順序は「フェイク幅／斜体 → スケール → 回転」。
 *
 * @param rotation   ラジアン。数学慣習（y-up）の反時計回りが正。横倒しは -90°
 * @param scaleX,Y   平体・長体・縦中横の圧縮
 * @param skewX      シアー。フェイクイタリックなら kFakeItalicSkew
 * @param fakeScaleX フォント幅のうち wdth 軸で吸収できない分の水平スケール
 */
inline Mat2 glyphMatrix(float rotation, float scaleX = 1.0f, float scaleY = 1.0f,
                        float skewX = 0.0f, float fakeScaleX = 1.0f) {
    Mat2 m;
    m.xx = fakeScaleX;
    m.xy = skewX;      // y-down 座標では pt.x += skewX * pt.y
    m.yx = 0.0f;
    m.yy = 1.0f;

    if (scaleX != 1.0f || scaleY != 1.0f) {
        Mat2 s;
        s.xx = scaleX;
        s.yy = scaleY;
        m = multiply(s, m);
    }
    if (rotation != 0.0f) {
        // 角度は y-up の反時計回りが正。描画先が y-down なので Y 反転で
        // 共役を取った形になり、シアー成分の符号が入れ替わる。
        const float c = std::cos(rotation);
        const float s = std::sin(rotation);
        Mat2 r;
        r.xx = c;  r.xy = s;
        r.yx = -s; r.yy = c;
        m = multiply(r, m);
    }
    return m;
}

/// フェイクボールドのストローク幅（フォントサイズの 1/24）
inline Pt fakeBoldWidth(Pt fontSize) { return fontSize / 24.0f; }

} // namespace typeset::dl

#endif // TYPESET_DL_GLYPH_TRANSFORM_HPP
