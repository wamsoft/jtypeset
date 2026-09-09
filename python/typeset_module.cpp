/**
 * typeset_module.cpp — Python バインディング（pybind11）
 *
 * C++ の構造体をほぼそのまま出す。文字列は Python の str ↔ std::u16string。
 * Block の variant は Flow のメソッド経由で足す（variant 自体は出さない）。
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "typeset/backend/pdf_writer.hpp"
#include "typeset/backend/raster.hpp"
#include "typeset/backend/svg_writer.hpp"
#include "typeset/block/block.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/image/image.hpp"
#include "typeset/inl/paragraph.hpp"
#include "typeset/obj/object.hpp"
#include "typeset/obj/svg_import.hpp"
#ifdef TYPESET_HAS_MICROTEX
#include "microtex_handler.hpp"
#endif
#include "typeset/text/utf.hpp"
#include "typeset/page/flow_layouter.hpp"
#include "typeset/page/page.hpp"

namespace py = pybind11;
using namespace typeset;

namespace {

/// 行の情報（低レベル API の戻り値）
struct LineInfo {
    size_t charStart, charEnd;
    float length, naturalLength, indent;
    bool paragraphEnd, hanging;
    int lineIndex;
};

LineInfo lineInfoOf(const inl::LineBox& l) {
    return LineInfo{l.charStart, l.charEnd, l.length, l.naturalLength, l.indent, l.paragraphEnd, l.hanging, l.lineIndex};
}

/// layout_paragraph の結果（fragment と書字方向を持ち、取り出し口を提供する）
struct ParagraphLayout {
    inl::ParagraphFragment frag;
    WritingMode wm = WritingMode::HorizontalTb;
};

/// 文字 1 つの情報（CharBox ＋ 物理矩形 ＋ face のキー）
struct CharInfo {
    size_t lineIndex;
    uint32_t charIndex;
    uint32_t styleIndex;
    uint32_t gid;
    float size;
    std::string faceKey;
    bool image, object, placeholder;
    float inlineStart, inlineEnd, blockMin, blockMax;
    Rect rect;
};

} // namespace

PYBIND11_MODULE(_jtypeset, m) {
    m.doc() = R"doc(jtypeset — 縦書き・横書きの日本語組版ライブラリ typeset の Python バインディング

流れ: FontSet でフォントを開く → TextStyle / Paragraph / Flow で内容を組み立てる → PageSequence で判型・段・柱を決める
→ FlowLayouter.layout() でページ列にする → Page.save_png / save_svg、save_pdf(pages, path) で出力する。
単位は pt（MM / CM / INCH の定数で換算）。座標はページ左上原点・y 下向き。
本文中の {page} {pages} {title}（柱・ノンブル）、{ref:label} {page:label}（相互参照）、{fig} {table} {eq}（番号）、
{index:よみ|用語}（索引）は FlowLayouter が置換・収集する。
Markdown → PDF は jtypeset.md（jtypeset-md コマンド）。)doc";

    m.attr("MM") = kMm;
    m.attr("INCH") = kInch;
    m.attr("CM") = kCm;

    // ---- 幾何・色 ----
    py::class_<Point>(m, "Point", "点（pt、ページ左上原点・y 下向き）")
        .def(py::init<>())
        .def(py::init<Pt, Pt>(), py::arg("x"), py::arg("y"))
        .def_readwrite("x", &Point::x)
        .def_readwrite("y", &Point::y);
    py::class_<Size>(m, "Size", "大きさ（pt）")
        .def(py::init<>())
        .def(py::init<Pt, Pt>(), py::arg("w"), py::arg("h"))
        .def_readwrite("w", &Size::w)
        .def_readwrite("h", &Size::h);
    py::class_<Rect>(m, "Rect", "矩形（pt）。x, y, w, h")
        .def(py::init<>())
        .def(py::init<Pt, Pt, Pt, Pt>(), py::arg("x"), py::arg("y"), py::arg("w"), py::arg("h"))
        .def_readwrite("x", &Rect::x)
        .def_readwrite("y", &Rect::y)
        .def_readwrite("w", &Rect::w)
        .def_readwrite("h", &Rect::h);
    py::class_<Color>(m, "Color", "色（RGBA、0–255）")
        .def(py::init<>())
        .def(py::init([](int r, int g, int b, int a) {
                 return Color{static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                              static_cast<uint8_t>(b), static_cast<uint8_t>(a)};
             }),
             py::arg("r"), py::arg("g"), py::arg("b"), py::arg("a") = 255)
        .def_readwrite("r", &Color::r)
        .def_readwrite("g", &Color::g)
        .def_readwrite("b", &Color::b)
        .def_readwrite("a", &Color::a);
    py::class_<Stroke>(m, "Stroke", "線の描き方（色・幅・端・角）")
        .def(py::init<>())
        .def(py::init([](Color c, Pt w) { Stroke s; s.color = c; s.width = w; return s; }),
             py::arg("color"), py::arg("width") = 1.0f)
        .def_readwrite("color", &Stroke::color)
        .def_readwrite("width", &Stroke::width);
    py::class_<TextLayer>(m, "TextLayer",
                          "文字の外観の 1 層（塗り・縁取り・ずらし・ぼかし）。TextStyle.layers に下から上の順で並べる")
        .def(py::init<>())
        .def(py::init([](std::optional<Color> fill, std::optional<Stroke> stroke, Point offset, Pt blur) {
                 TextLayer l; l.fill = fill; l.stroke = stroke; l.offset = offset; l.blur = blur; return l;
             }),
             py::arg("fill") = std::nullopt, py::arg("stroke") = std::nullopt,
             py::arg("offset") = Point{0.0f, 0.0f}, py::arg("blur") = 0.0f)
        .def_readwrite("fill", &TextLayer::fill)
        .def_readwrite("stroke", &TextLayer::stroke)
        .def_readwrite("offset", &TextLayer::offset, "ずらし（pt、右・下が正）")
        .def_readwrite("blur", &TextLayer::blur, "ぼかし半径（pt）。ラスタと SVG のみ");
    py::class_<TextShadow>(m, "TextShadow", "影（色・ずらし・ぼかし半径）。層の一番下に置かれる")
        .def(py::init<>())
        .def(py::init([](Color color, Point offset, Pt blur) {
                 TextShadow s; s.color = color; s.offset = offset; s.blur = blur; return s;
             }),
             py::arg("color") = Color{0, 0, 0, 128}, py::arg("offset") = Point{1.0f, 1.0f}, py::arg("blur") = 0.0f)
        .def_readwrite("color", &TextShadow::color)
        .def_readwrite("offset", &TextShadow::offset)
        .def_readwrite("blur", &TextShadow::blur);
    py::class_<TextDecoration>(m, "TextDecoration",
                               "下線・打消し線。色（無ければ fill）・太さ（0 でフォントのメトリクス）・位置の補正（em、文字から離れる向きが正）")
        .def(py::init<>())
        .def(py::init([](std::optional<Color> color, Pt thickness, float offset) {
                 TextDecoration d; d.color = color; d.thickness = thickness; d.offset = offset; return d;
             }),
             py::arg("color") = std::nullopt, py::arg("thickness") = 0.0f, py::arg("offset") = 0.0f)
        .def_readwrite("color", &TextDecoration::color)
        .def_readwrite("thickness", &TextDecoration::thickness)
        .def_readwrite("offset", &TextDecoration::offset);

    // ---- 列挙 ----
    py::enum_<WritingMode>(m, "WritingMode")
        .value("HORIZONTAL_TB", WritingMode::HorizontalTb)
        .value("VERTICAL_RL", WritingMode::VerticalRl)
        .value("VERTICAL_LR", WritingMode::VerticalLr);
    py::enum_<TextOrientation>(m, "TextOrientation")
        .value("MIXED", TextOrientation::Mixed)
        .value("UPRIGHT", TextOrientation::Upright)
        .value("SIDEWAYS", TextOrientation::Sideways);
    py::enum_<Align>(m, "Align")
        .value("START", Align::Start)
        .value("END", Align::End)
        .value("CENTER", Align::Center)
        .value("JUSTIFY", Align::Justify);
    py::enum_<LineBreakStrategy>(m, "LineBreakStrategy")
        .value("GREEDY", LineBreakStrategy::Greedy)
        .value("KNUTH_PLASS", LineBreakStrategy::KnuthPlass);

    // ---- フォント ----
    py::class_<glyphware::CodepointRange>(m, "CodepointRange", "コードポイントの範囲（両端含む）")
        .def(py::init([](uint32_t lo, uint32_t hi) { return glyphware::CodepointRange{lo, hi}; }),
             py::arg("lo"), py::arg("hi"))
        .def_readwrite("lo", &glyphware::CodepointRange::lo)
        .def_readwrite("hi", &glyphware::CodepointRange::hi);
    py::class_<font::FontDeclaration>(m, "FontDeclaration",
                                      "開かずに登録するフォントのメタデータ（キー・ファイル・family 別名・weight・italic・languages・ranges）")
        .def(py::init<>())
        .def_readwrite("key", &font::FontDeclaration::key)
        .def_readwrite("path", &font::FontDeclaration::path)
        .def_readwrite("face_index", &font::FontDeclaration::faceIndex)
        .def_readwrite("family", &font::FontDeclaration::family)
        .def_readwrite("weight", &font::FontDeclaration::weight, "100〜900。0 でフォントから")
        .def_readwrite("italic", &font::FontDeclaration::italic, "None でフォントから")
        .def_readwrite("languages", &font::FontDeclaration::languages, "BCP47。この言語のテキストで先に試される")
        .def_readwrite("ranges", &font::FontDeclaration::ranges, "カバレッジ（空なら開いて cmap を見る）");
    py::class_<font::FontSet>(m, "FontSet",
                              "フォントの集合。load_file / load_bytes で開くか declare で宣言（初回使用時に開く）し、"
                              "TextStyle の family（キーまたは family 名）で引く。同じ family の複数 face から weight / italic の"
                              "最近傍を選び、文字が無ければ次の family へフォールバックする。set_language_fonts で言語ごとに先に試す family を指定できる")
        .def(py::init<>())
        .def("declare",
             [](font::FontSet& fs, const std::string& path, const std::string& key, std::vector<std::string> family,
                int weight, std::optional<bool> italic, std::vector<std::string> languages,
                std::vector<glyphware::CodepointRange> ranges, int index) {
                 font::FontDeclaration d;
                 d.key = key.empty() ? path : key;
                 d.path = path;
                 d.faceIndex = index;
                 d.family = std::move(family);
                 d.weight = weight;
                 d.italic = italic;
                 d.languages = std::move(languages);
                 d.ranges = std::move(ranges);
                 return fs.declare(std::move(d));
             },
             py::arg("path"), py::arg("key") = std::string(), py::arg("family") = std::vector<std::string>{},
             py::arg("weight") = 0, py::arg("italic") = std::nullopt,
             py::arg("languages") = std::vector<std::string>{},
             py::arg("ranges") = std::vector<glyphware::CodepointRange>{}, py::arg("face_index") = 0,
             "開かずに宣言する（初回使用時に開く）。key を省略するとパスがキー。weight=0 / italic=None はフォントから取る")
        .def("declare", [](font::FontSet& fs, font::FontDeclaration d) { return fs.declare(std::move(d)); },
             py::arg("declaration"))
        .def("has", &font::FontSet::has, py::arg("key"), "登録済みか（開いていなくてもよい）")
        .def("is_loaded", &font::FontSet::isLoaded, py::arg("key"), "開いているか")
        .def("keys", &font::FontSet::keys, "登録したキー（登録順）")
        .def("set_language_fonts", &font::FontSet::setLanguageFonts, py::arg("language"), py::arg("families"),
             "この言語のテキストで先に試す family 列（空で削除）。\"zh-Hans\" の完全一致が無ければ \"zh\" を使う")
        .def("language_fonts", &font::FontSet::languageFonts, py::arg("language"))
        .def("select",
             [](font::FontSet& fs, const std::string& name, int weight, bool italic) {
                 auto face = fs.select(name, weight, italic);
                 return face ? std::optional<std::string>(face->descriptor().key) : std::nullopt;
             },
             py::arg("name"), py::arg("weight") = 400, py::arg("italic") = false,
             "キー／family 名と weight / italic に最も近い face のキー（無ければ None）。必要なら開く")
        .def("load_file",
             [](font::FontSet& fs, const std::string& path, const std::string& key, int index) {
                 return static_cast<bool>(fs.loadFile(path, key, index));
             },
             py::arg("path"), py::arg("key") = std::string(), py::arg("face_index") = 0,
             "フォントファイルを開く。key を省略するとパスがキーになる")
        .def("load_bytes",
             [](font::FontSet& fs, const std::string& key, py::bytes data, int index) {
                 const std::string s = data;
                 return static_cast<bool>(fs.loadMemory(key, s.data(), s.size(), index));
             },
             py::arg("key"), py::arg("data"), py::arg("face_index") = 0)
        .def_property_readonly("size", &font::FontSet::size);

    // ---- スタイル ----
    py::class_<FontSpec>(m, "FontSpec", "フォント指定（family の列＋ウェイト＋斜体）")
        .def(py::init<>())
        .def(py::init([](std::vector<std::string> family, int weight, bool italic) {
                 FontSpec f; f.family = std::move(family); f.weight = weight; f.italic = italic; return f;
             }),
             py::arg("family"), py::arg("weight") = 400, py::arg("italic") = false)
        .def_readwrite("family", &FontSpec::family)
        .def_readwrite("weight", &FontSpec::weight)
        .def_readwrite("italic", &FontSpec::italic)
        .def_readwrite("variations", &FontSpec::variations,
                       "バリアブルフォントの軸の値（{'wght': 700, 'wdth': 75}）。wght が無ければ weight が入る。辞書はコピーを返すので作って代入する");

    py::class_<TextStyle>(m, "TextStyle", "文字スタイル: フォント・サイズ・色・縁取り・影・層・下線・打消し線・字間・向き・平体長体・合成ボールド／斜体・ベースラインのずらし")
        .def(py::init<>())
        .def(py::init([](std::vector<std::string> family, Pt size, Color fill) {
                 TextStyle s; s.font.family = std::move(family); s.size = size; s.fill = fill; return s;
             }),
             py::arg("family"), py::arg("size") = 10.0f, py::arg("fill") = Color{0, 0, 0, 255})
        .def_readwrite("font", &TextStyle::font)
        .def_readwrite("size", &TextStyle::size)
        .def_readwrite("fill", &TextStyle::fill)
        .def_readwrite("stroke", &TextStyle::stroke)
        .def_readwrite("shadow", &TextStyle::shadow, "影（TextShadow）。層の一番下に足す")
        .def_readwrite("layers", &TextStyle::layers,
                       "外観の層（TextLayer のリスト、下から上）。空なら fill / stroke の 1 層。"
                       "属性はコピーを返すので、リストを作って代入する")
        .def_readwrite("underline", &TextStyle::underline, "下線（TextDecoration）。縦組みでは右側の傍線")
        .def_readwrite("strikethrough", &TextStyle::strikethrough, "打消し線（TextDecoration）")
        .def_readwrite("letter_spacing", &TextStyle::letterSpacing)
        .def_readwrite("orientation", &TextStyle::orientation)
        .def_readwrite("scale_x", &TextStyle::scaleX)
        .def_readwrite("scale_y", &TextStyle::scaleY)
        .def_readwrite("baseline_shift", &TextStyle::baselineShift)
        .def_readwrite("fake_bold", &TextStyle::fakeBold)
        .def_readwrite("fake_italic", &TextStyle::fakeItalic)
        .def_readwrite("language", &TextStyle::language)
        .def_readwrite("features", &TextStyle::features,
                       "OpenType feature（['palt', '-liga', 'ss01'] など HarfBuzz の書式）。palt 等の字幅を変える feature を付けた文字は JLReq の約物の詰めを使わない")
        .def("copy", [](const TextStyle& s) { return TextStyle(s); });

    py::class_<SpacingOptions>(m, "SpacingOptions", "約物の詰め・ぶら下げ・和欧間・和字間の伸縮（JLReq のアキ量表）")
        .def(py::init<>())
        .def_readwrite("punctuation_spacing", &SpacingOptions::punctuationSpacing)
        .def_readwrite("hanging_punctuation", &SpacingOptions::hangingPunctuation)
        .def_readwrite("latin_gap", &SpacingOptions::latinGap)
        .def_readwrite("kanji_skip_stretch", &SpacingOptions::kanjiSkipStretch)
        .def_readwrite("kanji_skip_shrink", &SpacingOptions::kanjiSkipShrink);
    py::class_<BreakOptions>(m, "BreakOptions", "行分割の方法（Greedy / Knuth–Plass）と両端揃え")
        .def(py::init<>())
        .def_readwrite("strategy", &BreakOptions::strategy)
        .def_readwrite("justify", &BreakOptions::justify)
        .def_readwrite("tolerance", &BreakOptions::tolerance)
        .def_readwrite("line_penalty", &BreakOptions::linePenalty);
    py::class_<ParagraphStyle>(m, "ParagraphStyle", "段落スタイル: 揃え・行送り・一字下げ・向き・空白保持（コード）・タブ幅・アキ量・行分割")
        .def(py::init<>())
        .def_readwrite("align", &ParagraphStyle::align)
        .def_readwrite("first_line_indent", &ParagraphStyle::firstLineIndent)
        .def_readwrite("line_pitch", &ParagraphStyle::linePitch)
        .def_readwrite("line_height", &ParagraphStyle::lineHeight)
        .def_readwrite("orientation", &ParagraphStyle::orientation)
        .def_readwrite("spacing", &ParagraphStyle::spacing)
        .def_readwrite("line_break", &ParagraphStyle::lineBreak)
        .def_readwrite("preserve_spaces", &ParagraphStyle::preserveSpaces)
        .def_readwrite("tab_width", &ParagraphStyle::tabWidth)
        .def("copy", [](const ParagraphStyle& s) { return ParagraphStyle(s); });

    // ---- 注記・段落 ----
    py::enum_<inl::RubyMode>(m, "RubyMode")
        .value("GROUP", inl::RubyMode::Group)
        .value("MONO", inl::RubyMode::Mono)
        .value("JUKUGO", inl::RubyMode::Jukugo);
    py::enum_<inl::EmphasisMark>(m, "EmphasisMark")
        .value("SESAME", inl::EmphasisMark::Sesame)
        .value("OPEN_SESAME", inl::EmphasisMark::OpenSesame)
        .value("DOT", inl::EmphasisMark::Dot)
        .value("FILLED_CIRCLE", inl::EmphasisMark::FilledCircle)
        .value("OPEN_CIRCLE", inl::EmphasisMark::OpenCircle);
    py::class_<inl::Annotation>(m, "Annotation", "行内注記。ruby / tate_chu_yoko / emphasis / warichu / jidori の静的メソッドで作り、範囲は段落テキストの UTF-16 位置")
        .def_readwrite("start", &inl::Annotation::start)
        .def_readwrite("end", &inl::Annotation::end)
        .def_readwrite("text", &inl::Annotation::text)
        .def_readwrite("scale", &inl::Annotation::scale)
        .def_static("ruby", &inl::Annotation::ruby, py::arg("start"), py::arg("end"), py::arg("text"),
                    py::arg("mode") = inl::RubyMode::Group, py::arg("scale") = 0.5f)
        .def_static("tate_chu_yoko", &inl::Annotation::tateChuYoko, py::arg("start"), py::arg("end"))
        .def_static("emphasis", &inl::Annotation::emphasis, py::arg("start"), py::arg("end"),
                    py::arg("mark") = inl::EmphasisMark::Sesame, py::arg("scale") = 0.5f,
                    py::arg("opposite_side") = false)
        .def_static("warichu", &inl::Annotation::warichu, py::arg("start"), py::arg("end"),
                    py::arg("text") = std::u16string(), py::arg("scale") = 0.5f)
        .def_static("jidori", &inl::Annotation::jidori, py::arg("start"), py::arg("end"), py::arg("em"));

    py::class_<inl::InlineRun>(m, "InlineRun", "スタイルの付いたテキスト片（段落の構成要素）。literal なら {name} の置換をしない")
        .def(py::init([](std::u16string text, TextStyle style) {
                 return inl::InlineRun{std::move(text), std::move(style)};
             }),
             py::arg("text"), py::arg("style"))
        .def_readwrite("text", &inl::InlineRun::text)
        .def_readwrite("style", &inl::InlineRun::style)
        .def_readwrite("literal", &inl::InlineRun::literal);

    py::class_<inl::Paragraph>(m, "Paragraph", "段落: run の列＋注記＋段落スタイル。add_run / add_image / add_object / add_footnote / annotate で組み立てる")
        .def(py::init<>())
        .def(py::init([](std::u16string text, TextStyle style, std::optional<ParagraphStyle> pstyle) {
                 return inl::Paragraph::plain(std::move(text), std::move(style),
                                              pstyle ? *pstyle : ParagraphStyle{});
             }),
             py::arg("text"), py::arg("style"), py::arg("paragraph_style") = std::nullopt)
        .def_readwrite("runs", &inl::Paragraph::runs)
        .def_readwrite("annotations", &inl::Paragraph::annotations)
        .def_readwrite("style", &inl::Paragraph::style)
        .def("add_run", [](inl::Paragraph& p, std::u16string text, TextStyle st, bool literal) {
                 inl::InlineRun r;
                 r.text = std::move(text);
                 r.style = std::move(st);
                 r.literal = literal;
                 p.runs.push_back(std::move(r));
             }, py::arg("text"), py::arg("style"), py::arg("literal") = false,
             "run を足す。literal なら {name} の置換や {index:} の収集をしない（コード用）")
        .def("annotate", [](inl::Paragraph& p, inl::Annotation a) { p.annotations.push_back(std::move(a)); })
        .def("add_image",
             [](inl::Paragraph& p, std::shared_ptr<dl::Image> img, Size size, TextStyle st) {
                 p.addImage(img, size, std::move(st));
             },
             py::arg("image"), py::arg("size"), py::arg("style"), "行内画像を足す（本文中の位置は 1 文字ぶん）")
        .def("add_placeholder",
             [](inl::Paragraph& p, Size size, TextStyle st, std::string id) {
                 p.addPlaceholder(size, std::move(st), std::move(id));
             },
             py::arg("size"), py::arg("style"), py::arg("id") = std::string(),
             "行内プレースホルダ（描かない空箱、本文中の位置は 1 文字ぶん）を足す。位置は ParagraphLayout.placeholder_rects で取る")
        .def("add_object",
             [](inl::Paragraph& p, std::string handler, std::u16string source, TextStyle st,
                std::map<std::string, std::string> params) {
                 p.addObject(std::move(handler), std::move(source), std::move(params), std::move(st));
             },
             py::arg("handler"), py::arg("source"), py::arg("style"),
             py::arg("params") = std::map<std::string, std::string>{},
             "外部ハンドラのオブジェクト（数式など）を行内に足す。layout(objects=...) の ObjectRegistry で解決される")
        .def("add_footnote",
             [](inl::Paragraph& p, inl::Paragraph note, TextStyle markerStyle) {
                 p.addFootnote(std::move(note), std::move(markerStyle));
             },
             py::arg("note"), py::arg("marker_style"),
             "脚注を足す。本文のこの位置に番号（marker_style は superscript_style() で上付きに）が入り、注は段末に置かれる")
        .def_property_readonly("text", &inl::Paragraph::text);

    // ---- ブロック ----
    py::enum_<block::BreakKind>(m, "BreakKind")
        .value("AUTO", block::BreakKind::Auto)
        .value("COLUMN", block::BreakKind::Column)
        .value("PAGE", block::BreakKind::Page);
    py::class_<block::BlockStyle>(m, "BlockStyle", "ブロックの前後アキ・改ページ制御（orphans / widows / keep_with_next / keep_together / break_before / break_after）・段抜き・ラベル・背景・余白")
        .def(py::init<>())
        .def_readwrite("space_before", &block::BlockStyle::spaceBefore)
        .def_readwrite("space_after", &block::BlockStyle::spaceAfter)
        .def_readwrite("orphans", &block::BlockStyle::orphans)
        .def_readwrite("widows", &block::BlockStyle::widows)
        .def_readwrite("keep_with_next", &block::BlockStyle::keepWithNext)
        .def_readwrite("keep_together", &block::BlockStyle::keepTogether)
        .def_readwrite("break_before", &block::BlockStyle::breakBefore)
        .def_readwrite("break_after", &block::BlockStyle::breakAfter)
        .def_readwrite("span_columns", &block::BlockStyle::spanColumns)
        .def_readwrite("label", &block::BlockStyle::label)
        .def_readwrite("background", &block::BlockStyle::background)
        .def_readwrite("padding", &block::BlockStyle::padding);

    py::class_<dl::Image, std::shared_ptr<dl::Image>>(m, "Image")
        .def_readonly("width", &dl::Image::width)
        .def_readonly("height", &dl::Image::height);
    m.def("load_image", &image::loadFile, py::arg("path"), "PNG / JPEG / BMP / GIF を読む");
    m.def("image_from_rgba",
          [](int w, int h, py::bytes data) {
              const std::string s = data;
              if (s.size() < static_cast<size_t>(w) * h * 4) throw std::invalid_argument("rgba data too short");
              return image::fromRgba(w, h, reinterpret_cast<const uint8_t*>(s.data()));
          },
          py::arg("width"), py::arg("height"), py::arg("rgba"));

    py::enum_<block::ImagePlacement>(m, "ImagePlacement")
        .value("BLOCK", block::ImagePlacement::Block)
        .value("FLOAT_START", block::ImagePlacement::FloatStart)
        .value("FLOAT_END", block::ImagePlacement::FloatEnd);
    py::class_<block::ImageBlock>(m, "ImageBlock", "画像ブロック。placement で流れに置くか回り込み（FLOAT_START / FLOAT_END）、caption の {fig} は図番号")
        .def(py::init<>())
        .def_property("image",
                      [](const block::ImageBlock& b) { return std::const_pointer_cast<dl::Image>(b.image); },
                      [](block::ImageBlock& b, std::shared_ptr<dl::Image> img) { b.image = img; })
        .def_readwrite("size", &block::ImageBlock::size)
        .def_readwrite("placement", &block::ImageBlock::placement)
        .def_readwrite("align", &block::ImageBlock::align)
        .def_readwrite("gap", &block::ImageBlock::gap)
        .def_readwrite("caption", &block::ImageBlock::caption)
        .def_readwrite("caption_gap", &block::ImageBlock::captionGap)
        .def_readwrite("block", &block::ImageBlock::block);

    py::class_<block::ObjectBlock>(m, "ObjectBlock",
                                   "外部ハンドラで生成するオブジェクトのブロック（別行立ての数式・グラフなど）")
        .def(py::init<>())
        .def(py::init([](std::string handler, std::u16string source, TextStyle st,
                         std::map<std::string, std::string> params, bool numbered) {
                 block::ObjectBlock o;
                 o.handler = std::move(handler);
                 o.source = std::move(source);
                 o.textStyle = std::move(st);
                 o.params = std::move(params);
                 o.numbered = numbered;
                 return o;
             }),
             py::arg("handler"), py::arg("source"), py::arg("style"),
             py::arg("params") = std::map<std::string, std::string>{}, py::arg("numbered") = false)
        .def_readwrite("handler", &block::ObjectBlock::handler)
        .def_readwrite("source", &block::ObjectBlock::source)
        .def_readwrite("params", &block::ObjectBlock::params)
        .def_readwrite("align", &block::ObjectBlock::align)
        .def_readwrite("numbered", &block::ObjectBlock::numbered)
        .def_readwrite("text_style", &block::ObjectBlock::textStyle)
        .def_readwrite("caption", &block::ObjectBlock::caption)
        .def_readwrite("caption_gap", &block::ObjectBlock::captionGap)
        .def_readwrite("block", &block::ObjectBlock::block);

    py::enum_<block::VAlign>(m, "VAlign")
        .value("TOP", block::VAlign::Top)
        .value("MIDDLE", block::VAlign::Middle)
        .value("BOTTOM", block::VAlign::Bottom);
    py::class_<block::TableColumn>(m, "TableColumn", "表の列（幅 0 なら自動、揃え）")
        .def(py::init<>())
        .def(py::init([](Pt width, Align align) { return block::TableColumn{width, align}; }),
             py::arg("width") = 0.0f, py::arg("align") = Align::Start)
        .def_readwrite("width", &block::TableColumn::width)
        .def_readwrite("align", &block::TableColumn::align);
    py::class_<block::TableCell>(m, "TableCell", "表のセル（段落の列、colspan / rowspan、縦位置）")
        .def(py::init<>())
        .def(py::init([](inl::Paragraph p, int colspan, int rowspan, block::VAlign valign) {
                 block::TableCell c; c.paras.push_back(std::move(p)); c.colspan = colspan; c.rowspan = rowspan;
                 c.valign = valign; return c;
             }),
             py::arg("paragraph"), py::arg("colspan") = 1, py::arg("rowspan") = 1,
             py::arg("valign") = block::VAlign::Top)
        .def_readwrite("paras", &block::TableCell::paras)
        .def_readwrite("colspan", &block::TableCell::colspan)
        .def_readwrite("rowspan", &block::TableCell::rowspan)
        .def_readwrite("valign", &block::TableCell::valign);
    py::class_<block::TableRow>(m, "TableRow", "表の行（セルの列、header ならページをまたいで繰り返す）")
        .def(py::init<>())
        .def(py::init([](std::vector<block::TableCell> cells, bool header) {
                 block::TableRow r; r.cells = std::move(cells); r.header = header; return r;
             }),
             py::arg("cells"), py::arg("header") = false)
        .def_readwrite("cells", &block::TableRow::cells)
        .def_readwrite("header", &block::TableRow::header);
    py::class_<block::TableBorders>(m, "TableBorders", "表の罫線（外枠・内側・ヘッダ下の太さ、縦横の有無、色）")
        .def(py::init<>())
        .def_readwrite("outer", &block::TableBorders::outer)
        .def_readwrite("inner", &block::TableBorders::inner)
        .def_readwrite("header_rule", &block::TableBorders::headerRule)
        .def_readwrite("vertical", &block::TableBorders::vertical)
        .def_readwrite("horizontal", &block::TableBorders::horizontal)
        .def_readwrite("color", &block::TableBorders::color);
    py::class_<block::TableBlock>(m, "TableBlock", "表。列幅は固定／自動、colspan / rowspan、ヘッダの繰り返し、段より高い行の分割、caption の {table} は表番号")
        .def(py::init<>())
        .def_readwrite("columns", &block::TableBlock::columns)
        .def_readwrite("rows", &block::TableBlock::rows)
        .def_readwrite("borders", &block::TableBlock::borders)
        .def_readwrite("cell_padding", &block::TableBlock::cellPadding)
        .def_readwrite("full_width", &block::TableBlock::fullWidth)
        .def_readwrite("align", &block::TableBlock::align)
        .def_readwrite("repeat_header", &block::TableBlock::repeatHeader)
        .def_readwrite("caption", &block::TableBlock::caption)
        .def_readwrite("caption_gap", &block::TableBlock::captionGap)
        .def_readwrite("block", &block::TableBlock::block);

    py::enum_<block::ListBlock::Marker>(m, "ListMarker")
        .value("BULLET", block::ListBlock::Marker::Bullet)
        .value("NUMBERED", block::ListBlock::Marker::Numbered);
    py::class_<block::ListBlock>(m, "ListBlock", "箇条書き（記号／番号）")
        .def(py::init<>())
        .def(py::init([](std::vector<inl::Paragraph> items, block::ListBlock::Marker marker) {
                 block::ListBlock l; l.items = std::move(items); l.marker = marker; return l;
             }),
             py::arg("items"), py::arg("marker") = block::ListBlock::Marker::Bullet)
        .def_readwrite("items", &block::ListBlock::items)
        .def_readwrite("marker", &block::ListBlock::marker)
        .def_readwrite("bullet", &block::ListBlock::bullet)
        .def_readwrite("number_suffix", &block::ListBlock::numberSuffix)
        .def_readwrite("label_width", &block::ListBlock::labelWidth)
        .def_readwrite("gap", &block::ListBlock::gap)
        .def_readwrite("item_gap", &block::ListBlock::itemGap)
        .def_readwrite("block", &block::ListBlock::block);
    py::class_<block::TocBlock>(m, "TocBlock", "目次。見出し（採番済み）とページ番号を前のパスから集めて並べる")
        .def(py::init<>())
        .def(py::init([](TextStyle style, int maxLevel) {
                 block::TocBlock t; t.style = std::move(style); t.maxLevel = maxLevel; return t;
             }),
             py::arg("style"), py::arg("max_level") = 2)
        .def_readwrite("max_level", &block::TocBlock::maxLevel)
        .def_readwrite("style", &block::TocBlock::style)
        .def_readwrite("sub_style", &block::TocBlock::subStyle)
        .def_readwrite("indent_per_level", &block::TocBlock::indentPerLevel)
        .def_readwrite("line_height", &block::TocBlock::lineHeight)
        .def_readwrite("leader", &block::TocBlock::leader)
        .def_readwrite("block", &block::TocBlock::block);
    py::class_<block::IndexBlock>(m, "IndexBlock",
                                  "索引。本文の {index:用語} / {index:よみ|用語} を集めて読みの順に並べる")
        .def(py::init<>())
        .def(py::init([](TextStyle style) { block::IndexBlock b; b.style = std::move(style); return b; }),
             py::arg("style"))
        .def_readwrite("style", &block::IndexBlock::style)
        .def_readwrite("group_style", &block::IndexBlock::groupStyle)
        .def_readwrite("grouped", &block::IndexBlock::grouped)
        .def_readwrite("leader", &block::IndexBlock::leader)
        .def_readwrite("line_height", &block::IndexBlock::lineHeight)
        .def_readwrite("page_separator", &block::IndexBlock::pageSeparator)
        .def_readwrite("block", &block::IndexBlock::block);

    py::class_<block::Flow>(m, "Flow", "ブロックの列。add_paragraph / add_heading / add_list / add_table / add_image / add_object / add_toc / add_index / add_rule / add_page_break …")
        .def(py::init<>())
        .def("add_paragraph", &block::Flow::addParagraph, py::arg("paragraph"),
             py::arg("style") = block::BlockStyle{})
        .def("add_heading", &block::Flow::addHeading, py::arg("paragraph"), py::arg("level") = 1,
             py::arg("style") = std::nullopt, py::arg("numbered") = false)
        .def("add_list", &block::Flow::addList, py::arg("list"))
        .def("add_toc", &block::Flow::addToc, py::arg("toc"))
        .def("add_index", &block::Flow::addIndex, py::arg("index"))
        .def("add_labeled", &block::Flow::addLabeled, py::arg("label"), py::arg("body"),
             py::arg("label_width"), py::arg("gap") = 0.0f, py::arg("style") = block::BlockStyle{})
        .def("add_rule", &block::Flow::addRule, py::arg("thickness") = 0.5f,
             py::arg("color") = Color{0, 0, 0, 255}, py::arg("style") = block::BlockStyle{})
        .def("add_spacer", &block::Flow::addSpacer, py::arg("size"))
        .def("add_page_break", &block::Flow::addPageBreak)
        .def("add_column_break", &block::Flow::addColumnBreak)
        .def("add_image", &block::Flow::addImage, py::arg("image_block"))
        .def("add_object", [](block::Flow& f, block::ObjectBlock o) { f.addObject(std::move(o)); },
             py::arg("object_block"))
        .def("add_table", &block::Flow::addTable, py::arg("table"))
        .def("add_section", [](block::Flow& f, std::optional<int> columns, std::optional<Pt> gap) {
                 f.add(block::SectionBlock{columns, gap});
             }, py::arg("columns") = std::nullopt, py::arg("column_gap") = std::nullopt)
        .def_property_readonly("size", [](const block::Flow& f) { return f.blocks.size(); });

    // ---- ページ ----
    py::module_ paper = m.def_submodule("paper", "判型");
    paper.attr("A4") = page::paper::A4;
    paper.attr("A5") = page::paper::A5;
    paper.attr("B5") = page::paper::B5;
    paper.attr("B6") = page::paper::B6;
    paper.attr("BUNKO") = page::paper::Bunko;
    paper.attr("SHINSHO") = page::paper::Shinsho;
    paper.def("landscape", &page::paper::landscape);

    py::class_<page::Margins>(m, "Margins", "余白（top / bottom / inner / outer。pt）。duplex では inner がノド側")
        .def(py::init<>())
        .def(py::init([](Pt top, Pt bottom, Pt inner, Pt outer) {
                 return page::Margins{top, bottom, inner, outer};
             }),
             py::arg("top"), py::arg("bottom"), py::arg("inner"), py::arg("outer"))
        .def_readwrite("top", &page::Margins::top)
        .def_readwrite("bottom", &page::Margins::bottom)
        .def_readwrite("inner", &page::Margins::inner)
        .def_readwrite("outer", &page::Margins::outer);
    py::class_<page::RunningText>(m, "RunningText", "柱・ノンブル。{page} {pages} {title} を置換。mirror_on_even で見開きの偶数ページを左右反転")
        .def(py::init<>())
        .def(py::init([](inl::Paragraph p, Pt offset) { return page::RunningText{std::move(p), offset}; }),
             py::arg("paragraph"), py::arg("offset") = 0.0f)
        .def_readwrite("paragraph", &page::RunningText::para)
        .def_readwrite("offset", &page::RunningText::offset)
        .def_readwrite("mirror_on_even", &page::RunningText::mirrorOnEven);
    py::class_<page::PageMaster>(m, "PageMaster", "ページマスタ: 判型・余白・段数・書字方向・柱・ノンブル・duplex")
        .def(py::init<>())
        .def_readwrite("size", &page::PageMaster::size)
        .def_readwrite("margin", &page::PageMaster::margin)
        .def_readwrite("writing_mode", &page::PageMaster::writingMode)
        .def_readwrite("columns", &page::PageMaster::columns)
        .def_readwrite("column_gap", &page::PageMaster::columnGap)
        .def_readwrite("header", &page::PageMaster::header)
        .def_readwrite("footer", &page::PageMaster::footer)
        .def_readwrite("duplex", &page::PageMaster::duplex)
        .def("body_rect", &page::PageMaster::bodyRect, py::arg("page_number"));
    py::class_<page::PageSequence>(m, "PageSequence", "ページ列の設定（マスタ＋開始ページ番号）")
        .def(py::init<>())
        .def_readwrite("master", &page::PageSequence::master)
        .def_readwrite("first_page_number", &page::PageSequence::firstPageNumber);

    py::class_<page::Page>(m, "Page", "組版結果の 1 ページ（番号＋表示リスト）。save_png / save_svg / to_svg で出力、PDF は save_pdf(pages, path)")
        .def_readonly("number", &page::Page::number)
        .def("render",
             [](const page::Page& pg, float dpi, Color background) {
                 backend::RasterRenderer r;
                 backend::RasterOptions o;
                 o.dpi = dpi;
                 o.background = background;
                 backend::Bitmap bmp = r.render(pg.dl, o);
                 return py::make_tuple(bmp.width, bmp.height,
                                       py::bytes(reinterpret_cast<const char*>(bmp.argb.data()),
                                                 bmp.argb.size() * 4));
             },
             py::arg("dpi") = 144.0f, py::arg("background") = Color{255, 255, 255, 255},
             "ラスタライズして (width, height, ARGB8888 bytes) を返す")
        .def("save_png",
             [](const page::Page& pg, const std::string& path, float dpi, Color background) {
                 backend::RasterRenderer r;
                 backend::RasterOptions o;
                 o.dpi = dpi;
                 o.background = background;
                 return backend::savePng(r.render(pg.dl, o), path);
             },
             py::arg("path"), py::arg("dpi") = 144.0f, py::arg("background") = Color{255, 255, 255, 255})
        .def("save_svg",
             [](const page::Page& pg, const std::string& path) { return backend::saveSvg(pg.dl, path); },
             py::arg("path"))
        .def("to_svg", [](const page::Page& pg) { return backend::writeSvg(pg.dl); });

    // ---- 外部オブジェクト ----
    py::class_<obj::ObjectResult, std::shared_ptr<obj::ObjectResult>>(m, "ObjectResult")
        .def_readonly("size", &obj::ObjectResult::size)
        .def_readonly("baseline", &obj::ObjectResult::baseline)
        .def_readonly("error", &obj::ObjectResult::error)
        .def("ok", &obj::ObjectResult::ok);

    py::class_<obj::ObjectRegistry>(m, "ObjectRegistry",
        "オブジェクトハンドラの登録。add(name, fn) の fn は request(dict) を受けて SVG 文字列、"
        "または {'svg': str, 'baseline': pt} を返す。add_command(name, cmd) は `cmd <request.json>` の標準出力（SVG）を読む")
        .def(py::init<>())
        .def("add",
             [](obj::ObjectRegistry& reg, const std::string& name, py::function fn) {
                 reg.add(name, [fn](const obj::ObjectRequest& req) -> obj::ObjectResult {
                     py::gil_scoped_acquire gil;
                     py::dict d;
                     d["handler"] = req.handler;
                     d["source"] = req.source;
                     d["params"] = req.params;
                     d["max_inline"] = req.maxInline;
                     d["max_block"] = req.maxBlock;
                     d["font_size"] = req.fontSize;
                     d["inline"] = req.inlineContext;
                     d["vertical"] = isVertical(req.writingMode);
                     obj::ObjectResult res;
                     py::object out = fn(d);
                     std::string svg;
                     std::optional<float> baseline;
                     if (out.is_none()) {
                         res.error = "handler returned None";
                         return res;
                     } else if (py::isinstance<py::str>(out)) {
                         svg = out.cast<std::string>();
                     } else if (py::isinstance<py::dict>(out)) {
                         py::dict od = out.cast<py::dict>();
                         if (od.contains("error")) { res.error = od["error"].cast<std::string>(); return res; }
                         if (od.contains("svg")) svg = od["svg"].cast<std::string>();
                         if (od.contains("baseline")) baseline = od["baseline"].cast<float>();
                     } else {
                         res.error = "handler must return str (svg) or dict";
                         return res;
                     }
                     obj::SvgImportOptions so;
                     so.fontSize = req.fontSize;
                     obj::importSvg(svg, so, res);
                     if (baseline) { res.baseline = *baseline; res.hasBaseline = true; }
                     return res;
                 });
             },
             py::arg("name"), py::arg("fn"))
        .def("add_command", &obj::ObjectRegistry::addCommand, py::arg("name"), py::arg("command"),
             py::arg("workdir") = std::string())
#ifdef TYPESET_HAS_MICROTEX
        .def("add_microtex",
             [](obj::ObjectRegistry& reg, const std::string& name, font::FontSet& fonts, std::string textFamily,
                std::string sansFamily, std::string resDir) {
                 handlers::MicroTexOptions mo;
                 mo.textFamily = std::move(textFamily);
                 mo.sansFamily = std::move(sansFamily);
                 mo.resDir = std::move(resDir);
                 reg.add(name, handlers::makeMicroTexHandler(fonts, mo));
             },
             py::arg("name"), py::arg("fonts"), py::arg("text_family") = std::string("serif"),
             py::arg("sans_family") = std::string("sans"), py::arg("res_dir") = std::string(),
             py::keep_alive<1, 3>(),
             "MicroTeX（LaTeX 数式）をハンドラとして登録する。res_dir は数式フォントの場所（省略時はパッケージ同梱の "
             "microtex_res。jtypeset.microtex_res_dir()）。\\text{} などは text_family / sans_family の FontSet キーで組む")
#endif
        .def("has", &obj::ObjectRegistry::has, py::arg("name"))
        .def("clear_cache", &obj::ObjectRegistry::clearCache)
        .def_property_readonly("cache_size", &obj::ObjectRegistry::cacheSize)
        .def_property_readonly("errors", &obj::ObjectRegistry::errors);

    py::class_<page::FlowLayouter>(m, "FlowLayouter", "Flow をページ列へ流し込む。layout(flow, sequence, ...) がページの列を返す（採番・参照・目次・索引は多パス）")
        .def(py::init<font::FontSet&>(), py::arg("fonts"), py::keep_alive<1, 2>())
        .def("layout",
             [](page::FlowLayouter& l, const block::Flow& flow, const page::PageSequence& seq,
                std::map<std::u16string, std::u16string> fields, bool drawGuides, bool balanceLastPage,
                std::u16string figureFormat, std::u16string tableFormat, std::u16string equationFormat,
                obj::ObjectRegistry* objects, std::u16string footnoteMarkerFormat,
                std::u16string footnoteLabelFormat, bool footnotePerPage) {
                 page::FlowLayoutOptions o;
                 o.fields = std::move(fields);
                 o.drawGuides = drawGuides;
                 o.balanceLastPage = balanceLastPage;
                 o.figureFormat = std::move(figureFormat);
                 o.tableFormat = std::move(tableFormat);
                 o.equationFormat = std::move(equationFormat);
                 o.objects = objects;
                 o.footnoteMarkerFormat = std::move(footnoteMarkerFormat);
                 o.footnoteLabelFormat = std::move(footnoteLabelFormat);
                 o.footnotePerPage = footnotePerPage;
                 return l.layout(flow, seq, o);
             },
             py::arg("flow"), py::arg("sequence"),
             py::arg("fields") = std::map<std::u16string, std::u16string>{},
             py::arg("draw_guides") = false, py::arg("balance_last_page") = true,
             py::arg("figure_format") = std::u16string(u"図 {n}"),
             py::arg("table_format") = std::u16string(u"表 {n}"),
             py::arg("equation_format") = std::u16string(u"({n})"),
             py::arg("objects") = nullptr,
             py::arg("footnote_marker_format") = std::u16string(u"{n}"),
             py::arg("footnote_label_format") = std::u16string(u"{n} "),
             py::arg("footnote_per_page") = false,
             "Flow をページ列へ流し込む。fields は柱・ノンブル・本文の {name} 置換");

    m.def("superscript_style", &inl::superscriptStyle, py::arg("style"), "上付き（脚注記号・指数用）のスタイルを作る");
#ifdef TYPESET_HAS_MICROTEX
    m.attr("HAS_MICROTEX") = true;
#else
    m.attr("HAS_MICROTEX") = false;
#endif

    m.def("save_pdf",
          [](const std::vector<page::Page>& pages, const std::string& path, const std::string& title,
             const std::string& author, bool subset, bool compress) {
              backend::PdfWriter w;
              if (!title.empty()) w.setTitle(title);
              if (!author.empty()) w.setAuthor(author);
              w.setSubsetFonts(subset);
              w.setCompressStreams(compress);
              for (const page::Page& p : pages) w.addPage(p.dl);
              const bool ok = w.save(path);
              return py::make_tuple(ok, w.warnings());
          },
          py::arg("pages"), py::arg("path"), py::arg("title") = std::string(),
          py::arg("author") = std::string(), py::arg("subset_fonts") = true,
          py::arg("compress") = true,
          "ページ列を 1 つの PDF に書く。(ok, warnings) を返す");

    // ---- 低レベル: 段落 1 つを組む ----
    py::class_<LineInfo>(m, "LineInfo", "layout_paragraph が返す 1 行の情報")
        .def_readonly("char_start", &LineInfo::charStart)
        .def_readonly("char_end", &LineInfo::charEnd)
        .def_readonly("length", &LineInfo::length)
        .def_readonly("natural_length", &LineInfo::naturalLength)
        .def_readonly("indent", &LineInfo::indent)
        .def_readonly("paragraph_end", &LineInfo::paragraphEnd)
        .def_readonly("hanging", &LineInfo::hanging)
        .def_readonly("line_index", &LineInfo::lineIndex)
        .def("__repr__", [](const LineInfo& l) {
            return "LineInfo(chars=" + std::to_string(l.charStart) + ".." + std::to_string(l.charEnd) +
                   ", length=" + std::to_string(l.length) + ")";
        });
    py::class_<CharInfo>(m, "CharInfo", "組んだあとの文字 1 つ（位置・大きさ・スタイル番号・グリフ・物理矩形）")
        .def_readonly("line_index", &CharInfo::lineIndex)
        .def_readonly("char_index", &CharInfo::charIndex, "元テキストでの位置（UTF-16）")
        .def_readonly("style_index", &CharInfo::styleIndex, "Paragraph の run の番号")
        .def_readonly("gid", &CharInfo::gid)
        .def_readonly("size", &CharInfo::size)
        .def_readonly("face_key", &CharInfo::faceKey, "FontSet のキー")
        .def_readonly("image", &CharInfo::image)
        .def_readonly("object", &CharInfo::object)
        .def_readonly("placeholder", &CharInfo::placeholder)
        .def_readonly("inline_start", &CharInfo::inlineStart, "箱の始端（行頭から、pt）")
        .def_readonly("inline_end", &CharInfo::inlineEnd)
        .def_readonly("block_min", &CharInfo::blockMin, "箱の block 範囲（行の中心線から）")
        .def_readonly("block_max", &CharInfo::blockMax)
        .def_readonly("rect", &CharInfo::rect, "物理矩形（origin を渡して組んだとき）")
        .def("__repr__", [](const CharInfo& c) {
            return "CharInfo(line=" + std::to_string(c.lineIndex) + ", char=" + std::to_string(c.charIndex) +
                   ", rect=(" + std::to_string(c.rect.x) + ", " + std::to_string(c.rect.y) + ", " +
                   std::to_string(c.rect.w) + ", " + std::to_string(c.rect.h) + "))";
        });
    py::class_<inl::HitResult>(m, "HitResult", "hit_test の結果")
        .def_readonly("line_index", &inl::HitResult::lineIndex)
        .def_readonly("char_index", &inl::HitResult::charIndex, "当たった文字（行の後ろの余白なら行末）")
        .def_readonly("inside", &inl::HitResult::inside, "文字の箱の中か（False なら行の端に丸めた）")
        .def_readonly("after", &inl::HitResult::after, "箱の後半か（キャレットを次の文字の前に置く判断用）");
    py::class_<inl::PlaceholderRect>(m, "PlaceholderRect", "プレースホルダの位置（id・文字位置・物理矩形）")
        .def_readonly("id", &inl::PlaceholderRect::id)
        .def_readonly("char_index", &inl::PlaceholderRect::charIndex)
        .def_readonly("rect", &inl::PlaceholderRect::rect);
    py::class_<inl::TextMetrics>(m, "TextMetrics", "measure_text の結果")
        .def_readonly("advance", &inl::TextMetrics::advance, "送り方向の長さ（pt）")
        .def_readonly("ascent", &inl::TextMetrics::ascent, "中心線から注記側（横組み: 上）の張り出し")
        .def_readonly("descent", &inl::TextMetrics::descent)
        .def_readonly("cluster_count", &inl::TextMetrics::clusterCount)
        .def_readonly("glyph_count", &inl::TextMetrics::glyphCount);

    py::class_<ParagraphLayout>(m, "ParagraphLayout",
                                "layout_paragraph の結果。行の列（len / 添字 / 反復で LineInfo）と、組んだあとの取り出し口。"
                                "origin は 1 行目の行頭（横組み: 左端 x と 1 行目の中心線 y、縦組み: 1 列目の中心線 x と上端 y）")
        .def_property_readonly("lines", [](const ParagraphLayout& pl) {
            std::vector<LineInfo> out;
            for (const inl::LineBox& l : pl.frag.lines) out.push_back(lineInfoOf(l));
            return out;
        })
        .def("__len__", [](const ParagraphLayout& pl) { return pl.frag.lines.size(); })
        .def("__getitem__", [](const ParagraphLayout& pl, ptrdiff_t i) {
            const ptrdiff_t n = static_cast<ptrdiff_t>(pl.frag.lines.size());
            if (i < 0) i += n;
            if (i < 0 || i >= n) throw py::index_error();
            return lineInfoOf(pl.frag.lines[static_cast<size_t>(i)]);
        })
        .def("__iter__", [](const ParagraphLayout& pl) {
            std::vector<LineInfo> out;
            for (const inl::LineBox& l : pl.frag.lines) out.push_back(lineInfoOf(l));
            return py::iter(py::cast(out));
        })
        .def_property_readonly("writing_mode", [](const ParagraphLayout& pl) { return pl.wm; })
        .def_property_readonly("line_pitch", [](const ParagraphLayout& pl) { return pl.frag.linePitch; })
        .def_property_readonly("block_extent", [](const ParagraphLayout& pl) { return pl.frag.blockExtent(); },
                               "全行が占める行送り方向の量（pt）")
        .def_property_readonly("complete", [](const ParagraphLayout& pl) { return pl.frag.complete; })
        .def_property_readonly("char_end", [](const ParagraphLayout& pl) { return pl.frag.charEnd; })
        .def("line_origin",
             [](const ParagraphLayout& pl, size_t line, Point origin, int lineOffset) {
                 return inl::lineOriginOf(pl.frag, pl.wm, origin, line, lineOffset);
             },
             py::arg("line"), py::arg("origin") = Point{0.0f, 0.0f}, py::arg("line_offset") = 0,
             "行 line の行頭（物理）")
        .def("char_boxes",
             [](const ParagraphLayout& pl, size_t line, Point origin, int lineOffset) {
                 std::vector<CharInfo> out;
                 const Point lo = inl::lineOriginOf(pl.frag, pl.wm, origin, line, lineOffset);
                 for (const inl::CharBox& b : inl::charBoxes(pl.frag, pl.wm, line)) {
                     CharInfo c{b.lineIndex, b.charIndex, b.styleIndex, b.gid, b.size,
                                b.face ? b.face->descriptor().key : std::string(),
                                b.image, b.object, b.placeholder,
                                b.inlineStart, b.inlineEnd, b.blockMin, b.blockMax, b.rect(pl.wm, lo)};
                     out.push_back(std::move(c));
                 }
                 return out;
             },
             py::arg("line"), py::arg("origin") = Point{0.0f, 0.0f}, py::arg("line_offset") = 0,
             "行 line の文字の箱（送り方向の順。ルビ等の注記は含まない）")
        .def("rects_for",
             [](const ParagraphLayout& pl, size_t charStart, size_t charEnd, Point origin, int lineOffset) {
                 return inl::rectsFor(pl.frag, pl.wm, origin, charStart, charEnd, lineOffset);
             },
             py::arg("char_start"), py::arg("char_end"), py::arg("origin") = Point{0.0f, 0.0f},
             py::arg("line_offset") = 0,
             "文字範囲 [char_start, char_end) を覆う矩形（行ごとに 1 つ）。リンク・選択範囲用")
        .def("placeholder_rects",
             [](const ParagraphLayout& pl, Point origin, int lineOffset) {
                 return inl::placeholderRects(pl.frag, pl.wm, origin, lineOffset);
             },
             py::arg("origin") = Point{0.0f, 0.0f}, py::arg("line_offset") = 0)
        .def("hit_test",
             [](const ParagraphLayout& pl, Point p, Point origin, int lineOffset) {
                 return inl::hitTest(pl.frag, pl.wm, origin, p, lineOffset);
             },
             py::arg("point"), py::arg("origin") = Point{0.0f, 0.0f}, py::arg("line_offset") = 0,
             "点 → 文字。行送りの箱の外なら None")
        .def("caret_rect",
             [](const ParagraphLayout& pl, size_t charIndex, Point origin, int lineOffset, Pt thickness) {
                 return inl::caretRect(pl.frag, pl.wm, origin, charIndex, lineOffset, thickness);
             },
             py::arg("char_index"), py::arg("origin") = Point{0.0f, 0.0f}, py::arg("line_offset") = 0,
             py::arg("thickness") = 1.0f, "キャレット矩形（文字の始端。行末なら終端）。範囲外は None")
        .def("render",
             [](const ParagraphLayout& pl, Size size, Point origin, float dpi, Color background,
                int lineOffset, size_t maxChars) {
                 dl::DisplayList list;
                 list.page = size;
                 inl::emitParagraph(list, pl.frag, pl.wm, origin, lineOffset, maxChars);
                 backend::RasterRenderer r;
                 backend::RasterOptions o;
                 o.dpi = dpi;
                 o.background = background;
                 backend::Bitmap bmp = r.render(list, o);
                 return py::make_tuple(bmp.width, bmp.height,
                                       py::bytes(reinterpret_cast<const char*>(bmp.argb.data()),
                                                 bmp.argb.size() * 4));
             },
             py::arg("size"), py::arg("origin"), py::arg("dpi") = 144.0f,
             py::arg("background") = Color{255, 255, 255, 255}, py::arg("line_offset") = 0,
             py::arg("max_chars") = static_cast<size_t>(-1),
             "size（pt）の面に origin から描いて (width, height, ARGB8888 bytes) を返す。max_chars で途中まで（段階表示）")
        .def("save_png",
             [](const ParagraphLayout& pl, const std::string& path, Size size, Point origin, float dpi,
                Color background, int lineOffset, size_t maxChars) {
                 dl::DisplayList list;
                 list.page = size;
                 inl::emitParagraph(list, pl.frag, pl.wm, origin, lineOffset, maxChars);
                 backend::RasterRenderer r;
                 backend::RasterOptions o;
                 o.dpi = dpi;
                 o.background = background;
                 return backend::savePng(r.render(list, o), path);
             },
             py::arg("path"), py::arg("size"), py::arg("origin"), py::arg("dpi") = 144.0f,
             py::arg("background") = Color{255, 255, 255, 255}, py::arg("line_offset") = 0,
             py::arg("max_chars") = static_cast<size_t>(-1))
        .def("__repr__", [](const ParagraphLayout& pl) {
            return "ParagraphLayout(lines=" + std::to_string(pl.frag.lines.size()) + ")";
        });

    m.def("layout_paragraph",
          [](font::FontSet& fonts, const inl::Paragraph& para, WritingMode wm,
             std::vector<Pt> lineLengths, Pt defaultLength) {
              // 行ごとの行長（配列の末尾以降は defaultLength）
              struct Shape : inl::LineShapeProvider {
                  std::vector<Pt> lens;
                  Pt def;
                  inl::LineShape at(int i) const override {
                      const Pt L = (i >= 0 && static_cast<size_t>(i) < lens.size()) ? lens[i] : def;
                      return inl::LineShape{L, 0.0f};
                  }
              } shape;
              shape.lens = std::move(lineLengths);
              shape.def = defaultLength;
              inl::ParagraphLayouter layouter(fonts);
              ParagraphLayout pl;
              pl.frag = layouter.layout(para, wm, shape);
              pl.wm = wm;
              return pl;
          },
          py::arg("fonts"), py::arg("paragraph"), py::arg("writing_mode"),
          py::arg("line_lengths") = std::vector<Pt>{}, py::arg("default_length") = 200.0f,
          "段落を組んで ParagraphLayout（行ごとの文字範囲と長さ＋取り出し口）を返す（行長は行ごとに指定できる = \\parshape）");
    m.def("measure_text",
          [](font::FontSet& fonts, const std::u16string& text, const TextStyle& style, WritingMode wm) {
              return inl::measureText(fonts, text, style, wm);
          },
          py::arg("fonts"), py::arg("text"), py::arg("style"), py::arg("writing_mode") = WritingMode::HorizontalTb,
          "折り返さない 1 行の計測（送り・張り出し・クラスタ数）");
}
