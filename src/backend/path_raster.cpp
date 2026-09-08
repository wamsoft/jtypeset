/**
 * path_raster.cpp — 多角形のスキャンライン塗りとストロークの多角形化
 *
 * 縦方向は kSubSamples 本のサブスキャンラインで、横方向はスパン端の
 * 被覆率を厳密に取る。グリフを通さないパス（罫線・図形）用なので
 * 速度より単純さを優先している。
 */

#include "path_raster.hpp"

#include <algorithm>
#include <cmath>

namespace typeset::backend::detail {

namespace {

constexpr int kSubSamples = 4;

struct Edge {
    float x0, y0, x1, y1;   // y0 < y1
    int dir;                // 元の向き（+1: 下向き, -1: 上向き）
};

struct Crossing {
    float x;
    int dir;
    bool operator<(const Crossing& o) const { return x < o.x; }
};

/// 1 サブスキャンライン分のスパン [xa, xb) を被覆率の行へ加算する
void addSpan(std::vector<float>& row, int x0, float xa, float xb, float weight) {
    xa = std::max(xa, static_cast<float>(x0));
    xb = std::min(xb, static_cast<float>(x0 + static_cast<int>(row.size())));
    if (xb <= xa) return;
    int ia = static_cast<int>(std::floor(xa));
    int ib = static_cast<int>(std::ceil(xb)) - 1;
    if (ia == ib) {
        row[ia - x0] += (xb - xa) * weight;
        return;
    }
    row[ia - x0] += (static_cast<float>(ia + 1) - xa) * weight;
    for (int x = ia + 1; x < ib; ++x) row[x - x0] += weight;
    row[ib - x0] += (xb - static_cast<float>(ib)) * weight;
}

float signedArea(const std::vector<Point>& poly) {
    float a = 0.0f;
    for (size_t i = 0, n = poly.size(); i < n; ++i) {
        const Point& p = poly[i];
        const Point& q = poly[(i + 1) % n];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5f;
}

/// 向きを揃える（正の面積 = y-down で時計回り）
void orient(std::vector<Point>& poly) {
    if (signedArea(poly) < 0.0f) std::reverse(poly.begin(), poly.end());
}

void addCircle(std::vector<std::vector<Point>>& out, Point c, float r) {
    const int n = std::clamp(static_cast<int>(r * 2.0f) + 8, 8, 48);
    std::vector<Point> poly;
    poly.reserve(n);
    for (int i = 0; i < n; ++i) {
        const float t = 6.2831853f * static_cast<float>(i) / n;
        poly.push_back(Point{c.x + r * std::cos(t), c.y + r * std::sin(t)});
    }
    orient(poly);
    out.push_back(std::move(poly));
}

} // namespace

//------------------------------------------------------------------------------

bool rasterizePolygons(const std::vector<std::vector<Point>>& polys, bool evenOdd,
                       int clipW, int clipH, Coverage& out) {
    out = Coverage{};

    std::vector<Edge> edges;
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (const auto& poly : polys) {
        const size_t n = poly.size();
        if (n < 2) continue;
        for (size_t i = 0; i < n; ++i) {
            const Point& p = poly[i];
            const Point& q = poly[(i + 1) % n];
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            if (p.y == q.y) continue;
            Edge e;
            if (p.y < q.y) { e = {p.x, p.y, q.x, q.y, +1}; }
            else           { e = {q.x, q.y, p.x, p.y, -1}; }
            edges.push_back(e);
        }
    }
    if (edges.empty()) return false;

    const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
    const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
    const int x1 = std::min(clipW, static_cast<int>(std::ceil(maxX)) + 1);
    const int y1 = std::min(clipH, static_cast<int>(std::ceil(maxY)) + 1);
    if (x0 >= x1 || y0 >= y1) return false;

    out.x0 = x0;
    out.y0 = y0;
    out.width = x1 - x0;
    out.height = y1 - y0;
    out.a.assign(static_cast<size_t>(out.width) * out.height, 0);

    // y でソートして走査を短くする
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) { return a.y0 < b.y0; });

    std::vector<float> row(static_cast<size_t>(out.width));
    std::vector<Crossing> xs;
    const float weight = 1.0f / kSubSamples;
    bool any = false;
    size_t firstEdge = 0;

    for (int py = y0; py < y1; ++py) {
        std::fill(row.begin(), row.end(), 0.0f);
        bool rowAny = false;
        for (int s = 0; s < kSubSamples; ++s) {
            const float sy = static_cast<float>(py) + (static_cast<float>(s) + 0.5f) * weight;
            // このスキャンラインより上で終わる辺は以後見なくてよい
            while (firstEdge < edges.size() && edges[firstEdge].y1 <= sy &&
                   edges[firstEdge].y0 <= sy) {
                // 先頭が終了済みでも後続は未終了かもしれないので、単純に飛ばすのは
                // y0 順ソートでは安全でない。ここでは先頭が「開始も終了も sy 以下」の
                // 場合だけ進める。
                ++firstEdge;
            }
            xs.clear();
            for (size_t i = firstEdge; i < edges.size(); ++i) {
                const Edge& e = edges[i];
                if (e.y0 > sy) break;          // ソート済みなので以降は開始していない
                if (e.y1 <= sy) continue;
                const float t = (sy - e.y0) / (e.y1 - e.y0);
                xs.push_back(Crossing{e.x0 + t * (e.x1 - e.x0), e.dir});
            }
            if (xs.size() < 2) continue;
            std::sort(xs.begin(), xs.end());

            int wind = 0;
            for (size_t i = 0; i + 1 < xs.size(); ++i) {
                wind += evenOdd ? 1 : xs[i].dir;
                const bool inside = evenOdd ? (wind & 1) != 0 : wind != 0;
                if (!inside) continue;
                addSpan(row, x0, xs[i].x, xs[i + 1].x, weight);
                rowAny = true;
            }
        }
        if (!rowAny) continue;
        uint8_t* dst = out.a.data() + static_cast<size_t>(py - y0) * out.width;
        for (int x = 0; x < out.width; ++x) {
            const float c = std::clamp(row[x], 0.0f, 1.0f);
            dst[x] = static_cast<uint8_t>(c * 255.0f + 0.5f);
            any = any || dst[x] != 0;
        }
    }
    return any;
}

//------------------------------------------------------------------------------

std::vector<std::vector<Point>> strokeToPolygons(const std::vector<std::vector<Point>>& polylines,
                                                 const std::vector<bool>& closed,
                                                 float width, StrokeJoin join, StrokeCap cap) {
    std::vector<std::vector<Point>> out;
    const float hw = width * 0.5f;
    if (hw <= 0.0f) return out;

    for (size_t li = 0; li < polylines.size(); ++li) {
        const auto& line = polylines[li];
        const bool isClosed = li < closed.size() && closed[li];
        if (line.size() < 2) {
            // 点 1 つ: 丸キャップなら点を打つ
            if (line.size() == 1 && cap == StrokeCap::Round) addCircle(out, line[0], hw);
            continue;
        }
        const size_t n = line.size();
        const size_t segCount = isClosed ? n : n - 1;

        for (size_t i = 0; i < segCount; ++i) {
            Point a = line[i];
            Point b = line[(i + 1) % n];
            float dx = b.x - a.x, dy = b.y - a.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-6f) continue;
            dx /= len; dy /= len;
            // 端点のキャップ（開いた線の最初と最後だけ）
            if (!isClosed && cap == StrokeCap::Square) {
                if (i == 0) a = Point{a.x - dx * hw, a.y - dy * hw};
                if (i == segCount - 1) b = Point{b.x + dx * hw, b.y + dy * hw};
            }
            const float nx = -dy * hw, ny = dx * hw;
            std::vector<Point> quad = {Point{a.x + nx, a.y + ny}, Point{b.x + nx, b.y + ny},
                                       Point{b.x - nx, b.y - ny}, Point{a.x - nx, a.y - ny}};
            orient(quad);
            out.push_back(std::move(quad));
        }

        // 結合部。Miter/Bevel は Phase 0 では丸で近似する（罫線は直線が主なので
        // 見た目に効かない）。曲線は平坦化で頂点が密になるので丸で十分
        const size_t firstJoin = isClosed ? 0 : 1;
        const size_t lastJoin = isClosed ? n : n - 1;
        for (size_t i = firstJoin; i < lastJoin; ++i) {
            (void)join;
            addCircle(out, line[i], hw);
        }
        if (!isClosed && cap == StrokeCap::Round) {
            addCircle(out, line.front(), hw);
            addCircle(out, line.back(), hw);
        }
    }
    return out;
}

} // namespace typeset::backend::detail
