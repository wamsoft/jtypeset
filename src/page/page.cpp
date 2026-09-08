/**
 * page.cpp — ページマスタと段
 */

#include "typeset/page/page.hpp"

#include <algorithm>
#include <utility>

namespace typeset::page {

Rect PageMaster::bodyRect(int pageNumber) const {
    // 綴じ側: 横組み（左開き）は奇数ページで左がノド、縦組み（右開き）は奇数ページで右がノド
    const bool rightBound = isVertical(writingMode);
    const bool odd = (pageNumber % 2) != 0;
    Pt left, right;
    if (!duplex) {
        left = margin.inner;
        right = margin.outer;
        if (rightBound) std::swap(left, right);
    } else {
        const bool innerOnLeft = rightBound ? !odd : odd;
        left = innerOnLeft ? margin.inner : margin.outer;
        right = innerOnLeft ? margin.outer : margin.inner;
    }
    return Rect{left, margin.top, size.w - left - right, size.h - margin.top - margin.bottom};
}

std::vector<Rect> PageMaster::columnRects(const Rect& body, int cols, Pt gap) const {
    std::vector<Rect> out;
    if (cols < 1) cols = 1;
    if (cols == 1) {
        out.push_back(body);
        return out;
    }
    if (isVertical(writingMode)) {
        // 段は上下に並ぶ
        const Pt h = (body.h - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        for (int i = 0; i < cols; ++i) {
            out.push_back(Rect{body.x, body.y + static_cast<float>(i) * (h + gap), body.w, h});
        }
    } else {
        const Pt w = (body.w - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        for (int i = 0; i < cols; ++i) {
            out.push_back(Rect{body.x + static_cast<float>(i) * (w + gap), body.y, w, body.h});
        }
    }
    return out;
}

Point Region::lineOrigin(Pt blockOffset, Pt pitch, Pt inlineOffset) const {
    const Pt u = used + blockOffset + pitch * 0.5f;
    switch (writingMode) {
    case WritingMode::HorizontalTb: return Point{area.x + inlineOffset, area.y + u};
    case WritingMode::VerticalRl:   return Point{area.right() - u, area.y + inlineOffset};
    case WritingMode::VerticalLr:   return Point{area.x + u, area.y + inlineOffset};
    }
    return Point{area.x, area.y};
}

Rect Region::toRect(Pt inline0, Pt inline1, Pt block0, Pt block1) const {
    if (inline1 < inline0) std::swap(inline0, inline1);
    if (block1 < block0) std::swap(block0, block1);
    switch (writingMode) {
    case WritingMode::HorizontalTb:
        return Rect{area.x + inline0, area.y + block0, inline1 - inline0, block1 - block0};
    case WritingMode::VerticalRl:
        return Rect{area.right() - block1, area.y + inline0, block1 - block0, inline1 - inline0};
    case WritingMode::VerticalLr:
        return Rect{area.x + block0, area.y + inline0, block1 - block0, inline1 - inline0};
    }
    return Rect{};
}

void Region::freeInlineRange(Pt block0, Pt block1, Pt& start, Pt& end) const {
    const Pt L = lineLength();
    start = 0.0f;
    end = L;
    if (exclusions.empty()) return;

    const Rect strip = toRect(0.0f, L, block0, block1);
    // 帯にかかる排除領域の inline 区間を集める
    std::vector<std::pair<Pt, Pt>> blocked;
    for (const Rect& e : exclusions) {
        const Rect x = strip.intersect(e);
        if (x.w <= 0.01f || x.h <= 0.01f) continue;
        Pt a, b;
        if (isVertical(writingMode)) { a = x.y - area.y; b = x.bottom() - area.y; }
        else                         { a = x.x - area.x; b = x.right() - area.x; }
        blocked.emplace_back(std::max(0.0f, a), std::min(L, b));
    }
    if (blocked.empty()) return;
    std::sort(blocked.begin(), blocked.end());

    // 空き区間のうち最大のものを選ぶ
    Pt bestStart = 0.0f, bestEnd = 0.0f;
    Pt cursor = 0.0f;
    for (const auto& iv : blocked) {
        if (iv.first > cursor && iv.first - cursor > bestEnd - bestStart) {
            bestStart = cursor;
            bestEnd = iv.first;
        }
        cursor = std::max(cursor, iv.second);
    }
    if (L > cursor && L - cursor > bestEnd - bestStart) {
        bestStart = cursor;
        bestEnd = L;
    }
    start = bestStart;
    end = bestEnd;
}

inl::LineShape RegionLineShape::at(int lineIndex, Pt extraBlock) const {
    const Pt b0 = startBlock_ + pitch_ * static_cast<float>(lineIndex) + extraBlock;
    Pt s = 0.0f, e = 0.0f;
    region_.freeInlineRange(b0, b0 + pitch_, s, e);
    inl::LineShape shape;
    shape.indent = s + baseIndent_;
    shape.length = std::max(1.0f, e - s - baseIndent_);
    return shape;
}

} // namespace typeset::page
