#ifndef TYPESET_INL_LINE_BREAKER_HPP
#define TYPESET_INL_LINE_BREAKER_HPP

#include <cstdint>
#include <vector>

#include "typeset/inl/line_item.hpp"
#include "typeset/style.hpp"

/**
 * inl/line_breaker — TeX 型の行分割（Greedy / Knuth–Plass）
 *
 * 行長は 1 つの値ではなく LineShapeProvider から**行ごとに**取る（TeX の
 * `\parshape`）。回り込み・ラベル付き段落・将来の吹き出しはこれで表現する。
 * 行が確定すると調整比 ratio が決まり、追い込み・追い出し・両端揃えが
 * 「グルーの伸縮でどう行長に合わせるか」の 1 つの問題に統一される。
 */
namespace typeset::inl {

/// 1 行の形（行長と行頭の下げ）
struct LineShape {
    Pt length = 0.0f;
    Pt indent = 0.0f;
};

class LineShapeProvider {
public:
    virtual ~LineShapeProvider() = default;
    /// lineIndex 行目の形（段落の先頭を 0 とは限らない。呼び出し側が firstLineIndex を渡す）
    virtual LineShape at(int lineIndex) const = 0;
    /**
     * 行送りが広がった行の後ろでの形。extraBlock は「行番号 × 行送り」に対して実際の行位置が
     * ずれている量（前の行までの追加の送り）。排除領域を見る実装だけが使う。既定は at(lineIndex)
     */
    virtual LineShape at(int lineIndex, Pt /*extraBlock*/) const { return at(lineIndex); }
};

/// 行ごとの実際の位置のずれを base に渡す（段落レイアウタが行送りの広がりを反映するのに使う）
class OffsetLineShape : public LineShapeProvider {
public:
    OffsetLineShape(const LineShapeProvider& base, const std::vector<Pt>& offsets, int firstLineIndex)
        : base_(base), offsets_(offsets), first_(firstLineIndex) {}
    LineShape at(int lineIndex) const override { return at(lineIndex, 0.0f); }
    LineShape at(int lineIndex, Pt extraBlock) const override {
        const int i = lineIndex - first_;
        const Pt extra = (i >= 0 && static_cast<size_t>(i) < offsets_.size()) ? offsets_[static_cast<size_t>(i)]
                       : (offsets_.empty() ? 0.0f : offsets_.back());
        return base_.at(lineIndex, extraBlock + extra);
    }
private:
    const LineShapeProvider& base_;
    const std::vector<Pt>& offsets_;
    int first_;
};

/// 全行同じ長さ
class ConstantLineShape : public LineShapeProvider {
public:
    explicit ConstantLineShape(Pt length, Pt indent = 0.0f) : length_(length), indent_(indent) {}
    LineShape at(int) const override { return LineShape{length_, indent_}; }
private:
    Pt length_;
    Pt indent_;
};

/// 指定した行だけ行頭を下げる（一字下げ用）
class IndentedLineShape : public LineShapeProvider {
public:
    IndentedLineShape(const LineShapeProvider& base, int indentLine, Pt indent)
        : base_(base), indentLine_(indentLine), indent_(indent) {}
    LineShape at(int lineIndex) const override { return at(lineIndex, 0.0f); }
    LineShape at(int lineIndex, Pt extraBlock) const override {
        LineShape s = base_.at(lineIndex, extraBlock);
        if (lineIndex == indentLine_) {
            s.length -= indent_;
            s.indent += indent_;
        }
        return s;
    }
private:
    const LineShapeProvider& base_;
    int indentLine_;
    Pt indent_;
};

/// 確定した 1 行
struct BreakLine {
    uint32_t itemStart = 0;     ///< 行を構成するアイテムの開始
    uint32_t itemEnd = 0;       ///< 同・終端（ブレーク位置。この位置は含まない）
    float ratio = 0.0f;         ///< グルーの調整比。>0 で伸ばす、<0 で縮める
    Pt naturalWidth = 0.0f;     ///< 調整前の行長
    Pt width = 0.0f;            ///< 調整後の行長
    int lineIndex = 0;          ///< LineShapeProvider に渡した行番号
    LineShape shape;            ///< その行の形
};

/**
 * 行分割を実行する
 * @param items 組版アイテム列（末尾に段落終端が付いていること）
 * @param shape 行ごとの行長
 * @param firstLineIndex 最初の行の行番号
 */
std::vector<BreakLine> breakLines(const std::vector<LineItem>& items,
                                  const LineShapeProvider& shape,
                                  const BreakOptions& opts,
                                  int firstLineIndex = 0);

} // namespace typeset::inl

#endif // TYPESET_INL_LINE_BREAKER_HPP
