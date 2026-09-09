#ifndef TYPESET_INL_PARAGRAPH_HPP
#define TYPESET_INL_PARAGRAPH_HPP

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "typeset/dl/display_list.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/inl/annotation.hpp"
#include "typeset/inl/line_breaker.hpp"
#include "typeset/inl/shaped.hpp"
#include "typeset/style.hpp"
#include "typeset/writing_mode.hpp"

/**
 * inl/paragraph — 段落の組版（入口）
 *
 *   Paragraph（InlineRun 列＋注記）
 *     → shapeText（Itemizer ＋ HarfBuzz）
 *     → buildLineItems（JLReq のアキ・禁則・注記）
 *     → breakLines（Greedy / Knuth–Plass、行ごとの行長）
 *     → LineBox（論理座標のグリフ列）
 *     → emitParagraph（書字方向で物理化して表示リストへ）
 */
namespace typeset::inl {

struct Paragraph;

/// 外部ハンドラで生成するオブジェクトの指定（組版時に obj::ObjectRegistry で解決する）
struct ObjectRef {
    std::string handler;
    std::u16string source;
    std::map<std::string, std::string> params;
};

/// スタイルの付いたテキスト片。image / object / objectRef が付いていれば行内オブジェクト（text は U+FFFC 1 文字）
struct InlineRun {
    std::u16string text;
    TextStyle style;
    std::shared_ptr<const dl::Image> image;
    Size imageSize;         ///< pt。0 なら画素数を 72dpi として使い、片方 0 なら縦横比を保つ
    std::shared_ptr<const obj::ObjectResult> object;    ///< 解決済みのオブジェクト
    std::optional<ObjectRef> objectRef;                 ///< 未解決（FlowLayouter が解決する）
    /// 脚注。この run が本文中の記号（text は "{fn}"。FlowLayouter が番号に置き換え、段末に注を置く）
    std::shared_ptr<const Paragraph> footnote;
    /// true なら `{name}` の置換や `{index:}` の収集をしない（コードなど、波括弧をそのまま出す run）
    bool literal = false;
    /// 行内プレースホルダ（大きさ imageSize の空箱。描かない。ホストがウィジェット等を重ねる）
    bool placeholder = false;
    std::string placeholderId;
};

struct Paragraph {
    std::vector<InlineRun> runs;
    std::vector<Annotation> annotations;    ///< 範囲は段落全体の UTF-16 位置
    ParagraphStyle style;

    std::u16string text() const;
    /// 本文スタイル（先頭の run。無ければ既定）
    const TextStyle& baseStyle() const;

    static Paragraph plain(std::u16string text, TextStyle style, ParagraphStyle pstyle = {}) {
        Paragraph p;
        p.runs.push_back(InlineRun{std::move(text), std::move(style)});
        p.style = std::move(pstyle);
        return p;
    }

    /// 行内画像を足す（本文中の位置は U+FFFC 1 文字ぶん）
    void addImage(std::shared_ptr<const dl::Image> image, Size size, TextStyle style) {
        InlineRun r;
        r.text = u"\uFFFC";
        r.style = std::move(style);
        r.image = std::move(image);
        r.imageSize = size;
        runs.push_back(std::move(r));
    }

    /**
     * 行内プレースホルダ（大きさだけの空箱）を足す。本文中の位置は U+FFFC 1 文字ぶん。描かれないので、
     * 組んだあと placeholderRects() / charBoxes() で位置を取り、ホストがウィジェットや画像を重ねる。
     * 中心が行の中心線に載る（行内画像と同じ）。行送りの箱から出れば行送りが広がる
     */
    void addPlaceholder(Size size, TextStyle style, std::string id = std::string()) {
        InlineRun r;
        r.text = u"\uFFFC";
        r.style = std::move(style);
        r.placeholder = true;
        r.placeholderId = std::move(id);
        r.imageSize = size;
        runs.push_back(std::move(r));
    }

    /// 外部ハンドラのオブジェクト（数式など）を行内に足す。style は周囲の本文（サイズの基準・ベースライン）
    void addObject(std::string handler, std::u16string source, std::map<std::string, std::string> params,
                   TextStyle style) {
        InlineRun r;
        r.text = u"\uFFFC";
        r.style = std::move(style);
        r.objectRef = ObjectRef{std::move(handler), std::move(source), std::move(params)};
        runs.push_back(std::move(r));
    }
    /// 解決済みのオブジェクトを行内に足す
    void addObject(std::shared_ptr<const obj::ObjectResult> object, TextStyle style) {
        InlineRun r;
        r.text = u"\uFFFC";
        r.style = std::move(style);
        r.object = std::move(object);
        runs.push_back(std::move(r));
    }

    /**
     * 脚注を足す。本文のこの位置に記号（番号。markerStyle は上付きにしておく: superscriptStyle）が入り、
     * 注の本文 note は FlowLayouter がその行の載る段の末尾に置く（番号は文書を通して連番）
     */
    void addFootnote(Paragraph note, TextStyle markerStyle);
};

/// 上付き（脚注記号・指数用）: 0.6 倍にして注記側へ 0.6em ずらす
inline TextStyle superscriptStyle(TextStyle s) {
    s.size *= 0.6f;
    s.baselineShift = 0.6f;
    return s;
}

inline void Paragraph::addFootnote(Paragraph note, TextStyle markerStyle) {
    InlineRun r;
    r.text = u"{fn}";
    r.style = std::move(markerStyle);
    r.footnote = std::make_shared<Paragraph>(std::move(note));
    runs.push_back(std::move(r));
}

/// オブジェクトの描画命令を物理矩形 box に置く Group を作る（sideways: 時計回り 90° で横倒し）
dl::Group objectGroup(const obj::ObjectResult& ob, const Rect& box, bool sideways);

/**
 * 確定した 1 行（論理座標）
 */
struct LineBox {
    std::vector<PlacedGlyph> glyphs;    ///< inline_ は行頭から、block は中心線から
    Pt length = 0.0f;                   ///< 調整後の行長
    Pt naturalLength = 0.0f;            ///< 調整前の行長
    Pt blockMin = 0.0f;                 ///< 中心線からの張り出し（負側。ルビが横組みで上へ出る等）
    Pt blockMax = 0.0f;                 ///< 同（正側）
    size_t charStart = 0;               ///< 元テキストでの範囲（UTF-16）
    size_t charEnd = 0;
    bool hanging = false;               ///< 行末の約物を版面外へ出した（ぶら下げた）行か
    Pt hangWidth = 0.0f;
    int lineIndex = 0;                  ///< LineShapeProvider の行番号
    Pt indent = 0.0f;                   ///< 行頭の下げ（一字下げ＋揃えによるシフト）
    bool paragraphEnd = false;          ///< この行で（改行または本文の終わりで）段落が終わる
    /// 行内オブジェクト／画像が行送りの箱 [-pitch/2, +pitch/2] から出るぶんの追加の送り（TeX の lineskip 相当）。
    /// ルビや横倒しの張り出しでは広げない（行間に収める）
    Pt extraBefore = 0.0f;
    Pt extraAfter = 0.0f;
};

/**
 * スタイルごとの、描くときに要るメトリクス（ParagraphFragment::styleMetrics。styles と同じ添字）
 */
struct StyleMetrics {
    Pt baseline = 0.0f;                 ///< 中心線からベースラインへ（横組み。block 軸で下が正）
    font::DecorationMetrics decoration; ///< 下線・打消し線の位置と太さ
};

struct ParagraphFragment {
    std::vector<LineBox> lines;
    std::vector<TextStyle> styles;      ///< PlacedGlyph::styleIndex → スタイル
    std::vector<StyleMetrics> styleMetrics;   ///< styles と同じ添字（第一候補フォントから）
    std::vector<std::string> placeholderIds;  ///< styles と同じ添字。プレースホルダの run だけ非空（id 未指定なら ""）
    std::vector<bool> placeholders;           ///< styles と同じ添字。プレースホルダの run か
    size_t charStart = 0;               ///< 組んだ範囲（UTF-16）
    size_t charEnd = 0;
    bool complete = false;              ///< 本文を最後まで組めた
    Pt linePitch = 0.0f;                ///< 行送り
    float rotation = 0.0f;              ///< 段落の回転（度。emitParagraph が Group で包む）
    Pt baseSize = 0.0f;
    std::shared_ptr<const std::u16string> text;   ///< 元テキスト（ToUnicode 用）

    /// 行 li が占める行送り方向の量（行送り＋行内オブジェクトのための追加）
    Pt lineAdvance(size_t li) const {
        return linePitch + lines[li].extraBefore + lines[li].extraAfter;
    }
    /// 行 li の中心線の、「0 行目の行送りの箱の始端＋pitch/2」からの距離（追加が無ければ pitch × li）
    Pt lineCenterOffset(size_t li) const {
        Pt off = 0.0f;
        for (size_t j = 0; j < li && j < lines.size(); ++j) off += lineAdvance(j);
        return off + (li < lines.size() ? lines[li].extraBefore : 0.0f);
    }
    /// 全行が占める行送り方向の量
    Pt blockExtent() const {
        Pt s = 0.0f;
        for (size_t j = 0; j < lines.size(); ++j) s += lineAdvance(j);
        return s;
    }
};

class ParagraphLayouter {
public:
    explicit ParagraphLayouter(font::FontSet& fonts) : fonts_(fonts) {}

    /**
     * 段落を組む
     * @param wm 書字方向
     * @param shape 行ごとの行長
     * @param charStart 組み始める位置（段またぎの続き。0 なら先頭）
     * @param maxLines 行数上限（< 0 で無制限）
     * @param firstLineIndex 最初の行に与える行番号（LineShapeProvider へ渡る）
     */
    ParagraphFragment layout(const Paragraph& para, WritingMode wm,
                             const LineShapeProvider& shape,
                             size_t charStart = 0, int maxLines = -1,
                             int firstLineIndex = 0);

private:
    ParagraphFragment layoutOnce(const Paragraph& para, WritingMode wm,
                                 const LineShapeProvider& shape,
                                 size_t charStart, int maxLines, int firstLineIndex);
    void applyEllipsis(ParagraphFragment& frag, const Paragraph& para, WritingMode wm,
                       const LineShapeProvider& shape);
    font::FontSet& fonts_;
    std::vector<Pt> lineIndents_;   ///< Indent 注記による行ごとの字下げ（layout の反復で埋める。負は無し）
};

/**
 * 行 i の行頭（中心線上の点）の物理位置
 * @param origin 1 行目の行頭。横組みでは (左端, 1 行目の中心線 y)、
 *               縦組みでは (1 列目の中心線 x, 上端)
 */
Point lineOrigin(WritingMode wm, Point origin, int lineOffset, Pt linePitch, Pt indent);
/// 行送り方向に adv 進んだ行頭
Point lineOriginAt(WritingMode wm, Point origin, Pt adv, Pt indent);

/**
 * 段落を表示リストへ出す
 * @param origin 1 行目の行頭（lineOrigin 参照）
 * @param lineOffset fragment の 0 行目に対応する行番号（続きを別の位置から描くとき）
 * @param maxChars 元テキストのこの位置（UTF-16）より前の文字だけ描く（段階表示。注記は親文字に従う）。
 *                 組版はやり直さないので、全文で組んだ行分割のまま途中まで出る
 */
void emitParagraph(dl::DisplayList& out, const ParagraphFragment& frag, WritingMode wm,
                   Point origin, int lineOffset = 0, size_t maxChars = static_cast<size_t>(-1));

//------------------------------------------------------------------------------
// 取り出し口（ホストがリンク・ヒットテスト・キャレット・段階表示を作るための問い合わせ）
//------------------------------------------------------------------------------

/**
 * 組んだあとの 1 文字（クラスタのグリフ 1 つ）の箱。注記のグリフは含めない
 */
struct CharBox {
    size_t lineIndex = 0;           ///< ParagraphFragment::lines の添字
    uint32_t charIndex = 0;         ///< 元テキストでの位置（UTF-16）
    Pt inlineStart = 0.0f;          ///< 箱の始端（行頭から。indent は含まない）
    Pt inlineEnd = 0.0f;            ///< 箱の終端
    Pt blockMin = 0.0f;             ///< 箱の block 範囲（中心線から。em box、画像・オブジェクトはその大きさ）
    Pt blockMax = 0.0f;
    uint32_t styleIndex = 0;
    uint32_t gid = 0;
    Pt size = 0.0f;
    std::shared_ptr<glyphware::Face> face;
    bool image = false;
    bool object = false;
    bool placeholder = false;
    /// 物理座標での矩形
    Rect rect(WritingMode wm, Point lineOrigin) const;
};

/// 行 lineIndex の文字の箱（送り方向の順。注記のグリフは含めない。wm は画像の箱の向きに使う）
std::vector<CharBox> charBoxes(const ParagraphFragment& frag, WritingMode wm, size_t lineIndex);

/// 行 lineIndex の行頭（物理）。emitParagraph と同じ origin / lineOffset を渡す
Point lineOriginOf(const ParagraphFragment& frag, WritingMode wm, Point origin, size_t lineIndex,
                   int lineOffset = 0);

/**
 * 文字範囲 [charStart, charEnd) を覆う矩形（行ごとに 1 つ。物理座標。縦組みは縦長）。
 * リンクの当たり判定・選択範囲の描画用。範囲に文字が無い行は出ない
 */
std::vector<Rect> rectsFor(const ParagraphFragment& frag, WritingMode wm, Point origin,
                           size_t charStart, size_t charEnd, int lineOffset = 0);

/// プレースホルダの位置（id と物理矩形）。id 未指定のものは ""
struct PlaceholderRect {
    std::string id;
    uint32_t charIndex = 0;
    Rect rect;
};
std::vector<PlaceholderRect> placeholderRects(const ParagraphFragment& frag, WritingMode wm, Point origin,
                                              int lineOffset = 0);

/**
 * 点 → 文字
 */
struct HitResult {
    size_t lineIndex = 0;
    uint32_t charIndex = 0;         ///< 当たった文字（行の後ろの余白なら行末 = line.charEnd）
    bool inside = false;            ///< 文字の箱の中に当たったか（false なら行の端に丸めた）
    bool after = false;             ///< 箱の後半に当たった（キャレットを次の文字の前に置く判断用）
};
/// 行送りの箱の範囲に無い点は nullopt。行の前後の余白は最も近い文字（行頭／行末）に丸める
std::optional<HitResult> hitTest(const ParagraphFragment& frag, WritingMode wm, Point origin, Point p,
                                 int lineOffset = 0);

/**
 * キャレット矩形: charIndex の文字の始端（charIndex が行末なら最後の文字の終端）に、行送り方向の
 * em の高さで thickness 幅の矩形。文字が無い（範囲外）なら nullopt。行またぎの位置は前の行の行末側
 */
std::optional<Rect> caretRect(const ParagraphFragment& frag, WritingMode wm, Point origin, size_t charIndex,
                              int lineOffset = 0, Pt thickness = 1.0f);

/**
 * 箱の中での段落の位置: 行送り方向に blockAlign、行の方向に align（Justify は Start 扱い）で揃えた origin
 * （emitParagraph に渡す 1 行目の行頭）を返す。段落は箱の行長で組んであること
 */
Point originInBox(const ParagraphFragment& frag, WritingMode wm, const Rect& box,
                  BlockAlign blockAlign = BlockAlign::Start, Align align = Align::Start);

/**
 * 自動縮小: 行数上限に収まるまで文字サイズ（と linePitch）を段階的に縮めて組み直す
 */
struct FitResult {
    ParagraphFragment fragment;
    float scale = 1.0f;             ///< 採用した倍率（1 なら縮めていない）
    bool fits = true;               ///< minScale まで縮めても収まらなければ false（fragment は minScale の結果）
};
FitResult fitParagraph(ParagraphLayouter& layouter, const Paragraph& para, WritingMode wm,
                       const LineShapeProvider& shape, int maxLines,
                       float minScale = 0.5f, float step = 0.05f);

/**
 * 1 行計測（折り返さない）
 */
struct TextMetrics {
    Pt advance = 0.0f;              ///< 送り方向の長さ（字間・約物の詰めは入らない: ベタ組みの送りの和）
    Pt ascent = 0.0f;               ///< 中心線から注記側の張り出し（横組み: 上）。正
    Pt descent = 0.0f;              ///< 反対側。正
    size_t clusterCount = 0;        ///< クラスタ（文字）数
    size_t glyphCount = 0;
};
TextMetrics measureText(font::FontSet& fonts, const std::u16string& text, const TextStyle& style,
                        WritingMode wm = WritingMode::HorizontalTb,
                        TextOrientation orientation = TextOrientation::Mixed);

} // namespace typeset::inl

#endif // TYPESET_INL_PARAGRAPH_HPP
