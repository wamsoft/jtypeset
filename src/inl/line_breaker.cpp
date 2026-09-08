/**
 * line_breaker.cpp — TeX 型の行分割
 *
 * Greedy と Knuth–Plass（total-fit）。どちらも「合法ブレーク点」と「グルーの
 * 調整比」の定義は共有していて、違うのはどのブレーク点の組を選ぶかだけ。
 * 行長は行番号ごとに LineShapeProvider から取る。
 */

#include "typeset/inl/line_breaker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace typeset::inl {

namespace {

constexpr float kInfinity = std::numeric_limits<float>::infinity();

bool isLegalBreak(const std::vector<LineItem>& items, size_t i) {
    const LineItem& it = items[i];
    if (it.isPenalty()) return it.penalty < kInfinitePenalty;
    if (it.isGlue()) return i > 0 && items[i - 1].isBox();
    return false;
}

/// ブレーク点 b の次の行の開始位置（捨てられるアイテムを読み飛ばす）
uint32_t nextLineStart(const std::vector<LineItem>& items, size_t b) {
    size_t i = items[b].isPenalty() ? b + 1 : b;
    while (i < items.size() && !items[i].isBox()) ++i;
    return static_cast<uint32_t>(i);
}

struct Prefix {
    std::vector<float> width, stretch, shrink;
    explicit Prefix(const std::vector<LineItem>& items) {
        const size_t n = items.size();
        width.resize(n + 1, 0.0f);
        stretch.resize(n + 1, 0.0f);
        shrink.resize(n + 1, 0.0f);
        for (size_t i = 0; i < n; ++i) {
            const LineItem& it = items[i];
            float w = 0.0f, st = 0.0f, sh = 0.0f;
            if (it.isBox()) {
                w = it.width;
            } else if (it.isGlue()) {
                w = it.natural; st = it.stretch; sh = it.shrink;
            }
            width[i + 1] = width[i] + w;
            stretch[i + 1] = stretch[i] + st;
            shrink[i + 1] = shrink[i] + sh;
        }
    }
};

float lineWidth(const std::vector<LineItem>& items, const Prefix& pre, uint32_t start,
                uint32_t breakAt) {
    float w = pre.width[breakAt] - pre.width[start];
    if (items[breakAt].isPenalty()) w += items[breakAt].width;
    return w;
}

float adjustRatio(float natural, float stretch, float shrink, float target) {
    const float diff = target - natural;
    if (std::fabs(diff) < 1e-4f) return 0.0f;
    if (diff > 0.0f) return (stretch > 0.0f) ? diff / stretch : kInfinity;
    return (shrink > 0.0f) ? diff / shrink : -kInfinity;
}

float badness(float ratio) {
    if (!std::isfinite(ratio)) return 10000.0f;
    const float r = std::fabs(ratio);
    return 100.0f * r * r * r;
}

Pt safeLength(const LineShape& s) { return std::max(s.length, 1.0f); }

BreakLine makeLine(const std::vector<LineItem>& items, const Prefix& pre, uint32_t start,
                   uint32_t breakAt, float ratio, int lineIndex, const LineShape& shape) {
    BreakLine line;
    line.itemStart = start;
    line.itemEnd = breakAt;
    line.naturalWidth = lineWidth(items, pre, start, breakAt);
    line.ratio = ratio;
    const float st = pre.stretch[breakAt] - pre.stretch[start];
    const float sh = pre.shrink[breakAt] - pre.shrink[start];
    line.width = line.naturalWidth + (ratio >= 0.0f ? ratio * st : ratio * sh);
    line.lineIndex = lineIndex;
    line.shape = shape;
    return line;
}

//------------------------------------------------------------------------------
// Greedy
//------------------------------------------------------------------------------

std::vector<BreakLine> breakGreedy(const std::vector<LineItem>& items, const Prefix& pre,
                                   const LineShapeProvider& shapes, const BreakOptions& opts,
                                   int firstLineIndex) {
    std::vector<BreakLine> lines;
    const uint32_t n = static_cast<uint32_t>(items.size());

    uint32_t start = 0;
    while (start < n && !items[start].isBox()) ++start;
    int lineIndex = firstLineIndex;

    while (start < n) {
        const LineShape shape = shapes.at(lineIndex);
        const float lineLength = safeLength(shape);
        uint32_t chosen = n;
        bool forced = false;

        for (uint32_t i = start; i < n; ++i) {
            if (!isLegalBreak(items, i) || i <= start) continue;
            const float w = lineWidth(items, pre, start, i);
            // 揃えないとき（ragged）はグルーを縮めないので、自然幅で収まりを見る
            const float sh = opts.justify ? pre.shrink[i] - pre.shrink[start] : 0.0f;
            if (w - sh <= lineLength) {
                chosen = i;
                if (items[i].isForcedBreak()) { forced = true; break; }
            } else if (chosen != n) {
                break;                      // これ以上は入らない
            } else {
                chosen = i;                 // 1 つも入らない: 溢れを承知でここで切る
                if (items[i].isForcedBreak()) forced = true;
                break;
            }
        }
        if (chosen == n) {
            chosen = n - 1;
            forced = true;
        }

        float ratio = 0.0f;
        if (opts.justify) {
            const float st = pre.stretch[chosen] - pre.stretch[start];
            const float sh = pre.shrink[chosen] - pre.shrink[start];
            float r = adjustRatio(lineWidth(items, pre, start, chosen), st, sh, lineLength);
            if (!std::isfinite(r)) r = 0.0f;
            // Greedy は行が決まった後で揃えるので、伸びは 1 を超えても行末を揃える
            // （JLReq の追い出し＝字間を空けて揃える）。極端な伸びだけ抑える
            ratio = std::clamp(r, -1.0f, 4.0f);
        }
        lines.push_back(makeLine(items, pre, start, chosen, ratio, lineIndex, shape));
        ++lineIndex;

        if (forced && chosen >= n - 1) break;
        const uint32_t next = nextLineStart(items, chosen);
        if (next <= start) break;
        start = next;
    }
    return lines;
}

//------------------------------------------------------------------------------
// Knuth–Plass（total-fit）
//------------------------------------------------------------------------------

struct Node {
    uint32_t position;      ///< このノードから始まる行の開始アイテム
    uint32_t breakAt;       ///< このノードを生んだブレーク点
    int lineNumber;         ///< このノードから始まる行の行番号
    float totalDemerits;
    int previous;
    float ratio;
};

std::vector<BreakLine> breakKnuthPlass(const std::vector<LineItem>& items, const Prefix& pre,
                                       const LineShapeProvider& shapes,
                                       const BreakOptions& opts, int firstLineIndex) {
    const uint32_t n = static_cast<uint32_t>(items.size());
    uint32_t start = 0;
    while (start < n && !items[start].isBox()) ++start;

    std::vector<Node> nodes;
    nodes.push_back(Node{start, start, firstLineIndex, 0.0f, -1, 0.0f});
    std::vector<int> active{0};
    int lastNode = -1;

    for (uint32_t b = start; b < n; ++b) {
        if (!isLegalBreak(items, b)) continue;

        int bestNode = -1;
        float bestDemerits = kInfinity;
        float bestRatio = 0.0f;
        std::vector<int> stillActive;
        stillActive.reserve(active.size());

        for (int ai : active) {
            const Node& a = nodes[ai];
            if (b <= a.position) { stillActive.push_back(ai); continue; }
            const float lineLength = safeLength(shapes.at(a.lineNumber));
            const float w = lineWidth(items, pre, a.position, b);
            const float st = pre.stretch[b] - pre.stretch[a.position];
            const float sh = opts.justify ? pre.shrink[b] - pre.shrink[a.position] : 0.0f;
            const float r = adjustRatio(w, st, sh, lineLength);

            const bool tooLong = (r < -1.0f) || (r == -kInfinity);
            const bool forced = items[b].isForcedBreak();

            if (!tooLong && (forced || (std::isfinite(r) && r <= opts.tolerance))) {
                float d = opts.linePenalty + badness(r);
                d = d * d;
                if (items[b].isPenalty() && !forced) {
                    const float p = items[b].penalty;
                    d += (p >= 0.0f) ? p * p : -(p * p);
                }
                const float total = a.totalDemerits + d;
                if (total < bestDemerits) {
                    bestDemerits = total;
                    bestNode = ai;
                    bestRatio = std::clamp(r, -1.0f, 1.0f);
                }
            }
            if (!tooLong) stillActive.push_back(ai);
        }

        if (bestNode >= 0) {
            const uint32_t next = nextLineStart(items, b);
            nodes.push_back(Node{next, b, nodes[bestNode].lineNumber + 1, bestDemerits,
                                 bestNode, bestRatio});
            const int newIndex = static_cast<int>(nodes.size()) - 1;
            if (items[b].isForcedBreak()) lastNode = newIndex;
            stillActive.push_back(newIndex);
        }

        active.swap(stillActive);
        if (active.empty()) {
            return breakGreedy(items, pre, shapes, opts, firstLineIndex);
        }
    }

    if (lastNode < 0) return breakGreedy(items, pre, shapes, opts, firstLineIndex);

    std::vector<BreakLine> lines;
    for (int i = lastNode; i > 0; i = nodes[i].previous) {
        const Node& node = nodes[i];
        const Node& prev = nodes[node.previous];
        lines.push_back(makeLine(items, pre, prev.position, node.breakAt,
                                 opts.justify ? node.ratio : 0.0f, prev.lineNumber,
                                 shapes.at(prev.lineNumber)));
        if (node.previous <= 0) break;
    }
    std::reverse(lines.begin(), lines.end());
    return lines;
}

} // namespace

std::vector<BreakLine> breakLines(const std::vector<LineItem>& items,
                                  const LineShapeProvider& shape,
                                  const BreakOptions& opts, int firstLineIndex) {
    if (items.empty()) return {};
    const Prefix pre(items);
    if (opts.strategy == LineBreakStrategy::KnuthPlass) {
        return breakKnuthPlass(items, pre, shape, opts, firstLineIndex);
    }
    return breakGreedy(items, pre, shape, opts, firstLineIndex);
}

} // namespace typeset::inl
