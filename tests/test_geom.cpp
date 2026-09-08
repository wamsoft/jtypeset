#include <doctest/doctest.h>

#include <cmath>

#include "typeset/dl/glyph_transform.hpp"
#include "typeset/geom.hpp"
#include "typeset/writing_mode.hpp"

using namespace typeset;

TEST_CASE("Matrix multiply applies b then a") {
    const Matrix t = Matrix::translation(10, 20);
    const Matrix s = Matrix::scaling(2, 3);
    // multiply(t, s): scale first, then translate
    const Point p = multiply(t, s).apply({1, 1});
    CHECK(p.x == doctest::Approx(12));
    CHECK(p.y == doctest::Approx(23));
    // multiply(s, t): translate first, then scale
    const Point q = multiply(s, t).apply({1, 1});
    CHECK(q.x == doctest::Approx(22));
    CHECK(q.y == doctest::Approx(63));
}

TEST_CASE("Matrix invert") {
    Matrix m = multiply(Matrix::translation(5, -3), Matrix::scaling(2, 0.5f));
    bool ok = false;
    const Matrix inv = invert(m, &ok);
    REQUIRE(ok);
    const Point p{7, 11};
    const Point back = inv.apply(m.apply(p));
    CHECK(back.x == doctest::Approx(p.x));
    CHECK(back.y == doctest::Approx(p.y));
}

TEST_CASE("glyphMatrix rotation -90deg turns glyph up toward +x (y-down)") {
    // 縦組みの横倒し: グリフの「上」（y-up で +y）が画面右（+x）を向く
    const Mat2 m = dl::glyphMatrix(kSidewaysRotation);
    // グリフ座標は y-down で渡される。グリフの上端は (0, -1)
    const float ux = m.xx * 0 + m.xy * -1;
    const float uy = m.yx * 0 + m.yy * -1;
    CHECK(ux == doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(uy == doctest::Approx(0.0f).epsilon(1e-5));
    // 送り方向（グリフの +x）は画面の下（+y）
    CHECK(m.xx == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(m.yx == doctest::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("glyphMatrix skew and scale compose in the documented order") {
    const Mat2 m = dl::glyphMatrix(0.0f, 2.0f, 1.0f, dl::kFakeItalicSkew);
    // x' = 2 * (x + skew*y)
    CHECK(m.xx == doctest::Approx(2.0f));
    CHECK(m.xy == doctest::Approx(2.0f * dl::kFakeItalicSkew));
    CHECK(m.yy == doctest::Approx(1.0f));
}

TEST_CASE("flattenPath approximates a circle within tolerance") {
    Path p;
    p.addEllipse(Rect{0, 0, 100, 100});
    std::vector<bool> closed;
    const auto polys = flattenPath(p, 0.1f, &closed);
    REQUIRE(polys.size() == 1);
    CHECK(closed[0]);
    for (const Point& q : polys[0]) {
        const float r = std::sqrt((q.x - 50) * (q.x - 50) + (q.y - 50) * (q.y - 50));
        CHECK(r == doctest::Approx(50.0f).epsilon(0.005));
    }
}

TEST_CASE("toPhysical maps logical axes per writing mode") {
    const Point h = toPhysical(WritingMode::HorizontalTb, LogicalPoint{10, 2}, Point{100, 200});
    CHECK(h.x == doctest::Approx(110));
    CHECK(h.y == doctest::Approx(202));
    const Point v = toPhysical(WritingMode::VerticalRl, LogicalPoint{10, 2}, Point{100, 200});
    CHECK(v.x == doctest::Approx(102));
    CHECK(v.y == doctest::Approx(210));
}
