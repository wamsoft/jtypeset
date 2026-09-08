#include <doctest/doctest.h>

#include <numeric>

#include "backend/path_raster.hpp"
#include "typeset/backend/raster.hpp"

using namespace typeset;
using namespace typeset::backend;

namespace {

double coverageSum(const detail::Coverage& c) {
    return std::accumulate(c.a.begin(), c.a.end(), 0.0) / 255.0;
}

} // namespace

TEST_CASE("rasterizePolygons: axis-aligned rectangle area") {
    std::vector<std::vector<Point>> polys = {{{2, 2}, {12, 2}, {12, 7}, {2, 7}}};
    detail::Coverage cov;
    REQUIRE(detail::rasterizePolygons(polys, false, 100, 100, cov));
    CHECK(coverageSum(cov) == doctest::Approx(50.0).epsilon(0.01));
}

TEST_CASE("rasterizePolygons: half-pixel offset rectangle keeps area") {
    std::vector<std::vector<Point>> polys = {{{2.5f, 2.25f}, {12.5f, 2.25f}, {12.5f, 7.25f}, {2.5f, 7.25f}}};
    detail::Coverage cov;
    REQUIRE(detail::rasterizePolygons(polys, false, 100, 100, cov));
    CHECK(coverageSum(cov) == doctest::Approx(50.0).epsilon(0.02));
}

TEST_CASE("rasterizePolygons: nonzero vs evenodd on nested squares") {
    // 同じ向きの入れ子: nonzero は全部塗る、evenodd は穴が開く
    std::vector<std::vector<Point>> polys = {
        {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
        {{2, 2}, {8, 2}, {8, 8}, {2, 8}},
    };
    detail::Coverage nz, eo;
    REQUIRE(detail::rasterizePolygons(polys, false, 100, 100, nz));
    REQUIRE(detail::rasterizePolygons(polys, true, 100, 100, eo));
    CHECK(coverageSum(nz) == doctest::Approx(100.0).epsilon(0.01));
    CHECK(coverageSum(eo) == doctest::Approx(64.0).epsilon(0.01));
}

TEST_CASE("strokeToPolygons: horizontal rule has width*length area") {
    std::vector<std::vector<Point>> lines = {{{10, 10}, {60, 10}}};
    const auto polys = detail::strokeToPolygons(lines, {false}, 4.0f, StrokeJoin::Miter, StrokeCap::Butt);
    detail::Coverage cov;
    REQUIRE(detail::rasterizePolygons(polys, false, 100, 100, cov));
    CHECK(coverageSum(cov) == doctest::Approx(200.0).epsilon(0.02));
}

TEST_CASE("RasterRenderer fills a rect with the expected color") {
    dl::DisplayList list;
    list.page = Size{10, 10};
    list.addRect(Rect{2, 2, 4, 4}, Color::rgb(255, 0, 0));
    RasterRenderer r;
    RasterOptions o;
    o.dpi = 72;
    o.background = Color::rgb(255, 255, 255);
    const Bitmap bmp = r.render(list, o);
    REQUIRE(bmp.width == 10);
    REQUIRE(bmp.height == 10);
    CHECK(bmp.row(3)[3] == 0xFFFF0000u);
    CHECK(bmp.row(0)[0] == 0xFFFFFFFFu);
    CHECK(bmp.row(6)[6] == 0xFFFFFFFFu);
}
