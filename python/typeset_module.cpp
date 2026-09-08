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

} // namespace

PYBIND11_MODULE(typeset, m) {
    m.doc() = "typeset — 縦書き・横書きの日本語組版ライブラリ";

    m.attr("MM") = kMm;
    m.attr("INCH") = kInch;
    m.attr("CM") = kCm;

    // ---- 幾何・色 ----
    py::class_<Point>(m, "Point")
        .def(py::init<>())
        .def(py::init<Pt, Pt>(), py::arg("x"), py::arg("y"))
        .def_readwrite("x", &Point::x)
        .def_readwrite("y", &Point::y);
    py::class_<Size>(m, "Size")
        .def(py::init<>())
        .def(py::init<Pt, Pt>(), py::arg("w"), py::arg("h"))
        .def_readwrite("w", &Size::w)
        .def_readwrite("h", &Size::h);
    py::class_<Rect>(m, "Rect")
        .def(py::init<>())
        .def(py::init<Pt, Pt, Pt, Pt>(), py::arg("x"), py::arg("y"), py::arg("w"), py::arg("h"))
        .def_readwrite("x", &Rect::x)
        .def_readwrite("y", &Rect::y)
        .def_readwrite("w", &Rect::w)
        .def_readwrite("h", &Rect::h);
    py::class_<Color>(m, "Color")
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
    py::class_<Stroke>(m, "Stroke")
        .def(py::init<>())
        .def(py::init([](Color c, Pt w) { Stroke s; s.color = c; s.width = w; return s; }),
             py::arg("color"), py::arg("width") = 1.0f)
        .def_readwrite("color", &Stroke::color)
        .def_readwrite("width", &Stroke::width);

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
    py::class_<font::FontSet>(m, "FontSet")
        .def(py::init<>())
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
    py::class_<FontSpec>(m, "FontSpec")
        .def(py::init<>())
        .def(py::init([](std::vector<std::string> family, int weight, bool italic) {
                 FontSpec f; f.family = std::move(family); f.weight = weight; f.italic = italic; return f;
             }),
             py::arg("family"), py::arg("weight") = 400, py::arg("italic") = false)
        .def_readwrite("family", &FontSpec::family)
        .def_readwrite("weight", &FontSpec::weight)
        .def_readwrite("italic", &FontSpec::italic);

    py::class_<TextStyle>(m, "TextStyle")
        .def(py::init<>())
        .def(py::init([](std::vector<std::string> family, Pt size, Color fill) {
                 TextStyle s; s.font.family = std::move(family); s.size = size; s.fill = fill; return s;
             }),
             py::arg("family"), py::arg("size") = 10.0f, py::arg("fill") = Color{0, 0, 0, 255})
        .def_readwrite("font", &TextStyle::font)
        .def_readwrite("size", &TextStyle::size)
        .def_readwrite("fill", &TextStyle::fill)
        .def_readwrite("stroke", &TextStyle::stroke)
        .def_readwrite("letter_spacing", &TextStyle::letterSpacing)
        .def_readwrite("orientation", &TextStyle::orientation)
        .def_readwrite("scale_x", &TextStyle::scaleX)
        .def_readwrite("scale_y", &TextStyle::scaleY)
        .def_readwrite("fake_bold", &TextStyle::fakeBold)
        .def_readwrite("fake_italic", &TextStyle::fakeItalic)
        .def_readwrite("language", &TextStyle::language)
        .def("copy", [](const TextStyle& s) { return TextStyle(s); });

    py::class_<SpacingOptions>(m, "SpacingOptions")
        .def(py::init<>())
        .def_readwrite("punctuation_spacing", &SpacingOptions::punctuationSpacing)
        .def_readwrite("hanging_punctuation", &SpacingOptions::hangingPunctuation)
        .def_readwrite("latin_gap", &SpacingOptions::latinGap)
        .def_readwrite("kanji_skip_stretch", &SpacingOptions::kanjiSkipStretch)
        .def_readwrite("kanji_skip_shrink", &SpacingOptions::kanjiSkipShrink);
    py::class_<BreakOptions>(m, "BreakOptions")
        .def(py::init<>())
        .def_readwrite("strategy", &BreakOptions::strategy)
        .def_readwrite("justify", &BreakOptions::justify)
        .def_readwrite("tolerance", &BreakOptions::tolerance)
        .def_readwrite("line_penalty", &BreakOptions::linePenalty);
    py::class_<ParagraphStyle>(m, "ParagraphStyle")
        .def(py::init<>())
        .def_readwrite("align", &ParagraphStyle::align)
        .def_readwrite("first_line_indent", &ParagraphStyle::firstLineIndent)
        .def_readwrite("line_pitch", &ParagraphStyle::linePitch)
        .def_readwrite("line_height", &ParagraphStyle::lineHeight)
        .def_readwrite("orientation", &ParagraphStyle::orientation)
        .def_readwrite("spacing", &ParagraphStyle::spacing)
        .def_readwrite("line_break", &ParagraphStyle::lineBreak)
        .def_readwrite("preserve_spaces", &ParagraphStyle::preserveSpaces)
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
    py::class_<inl::Annotation>(m, "Annotation")
        .def_readwrite("start", &inl::Annotation::start)
        .def_readwrite("end", &inl::Annotation::end)
        .def_readwrite("text", &inl::Annotation::text)
        .def_readwrite("scale", &inl::Annotation::scale)
        .def_static("ruby", &inl::Annotation::ruby, py::arg("start"), py::arg("end"), py::arg("text"),
                    py::arg("mode") = inl::RubyMode::Group, py::arg("scale") = 0.5f)
        .def_static("tate_chu_yoko", &inl::Annotation::tateChuYoko, py::arg("start"), py::arg("end"))
        .def_static("emphasis", &inl::Annotation::emphasis, py::arg("start"), py::arg("end"),
                    py::arg("mark") = inl::EmphasisMark::Sesame, py::arg("scale") = 0.5f)
        .def_static("warichu", &inl::Annotation::warichu, py::arg("start"), py::arg("end"),
                    py::arg("text") = std::u16string(), py::arg("scale") = 0.5f)
        .def_static("jidori", &inl::Annotation::jidori, py::arg("start"), py::arg("end"), py::arg("em"));

    py::class_<inl::InlineRun>(m, "InlineRun")
        .def(py::init([](std::u16string text, TextStyle style) {
                 return inl::InlineRun{std::move(text), std::move(style)};
             }),
             py::arg("text"), py::arg("style"))
        .def_readwrite("text", &inl::InlineRun::text)
        .def_readwrite("style", &inl::InlineRun::style);

    py::class_<inl::Paragraph>(m, "Paragraph")
        .def(py::init<>())
        .def(py::init([](std::u16string text, TextStyle style, std::optional<ParagraphStyle> pstyle) {
                 return inl::Paragraph::plain(std::move(text), std::move(style),
                                              pstyle ? *pstyle : ParagraphStyle{});
             }),
             py::arg("text"), py::arg("style"), py::arg("paragraph_style") = std::nullopt)
        .def_readwrite("runs", &inl::Paragraph::runs)
        .def_readwrite("annotations", &inl::Paragraph::annotations)
        .def_readwrite("style", &inl::Paragraph::style)
        .def("add_run", [](inl::Paragraph& p, std::u16string text, TextStyle st) {
                 p.runs.push_back(inl::InlineRun{std::move(text), std::move(st)});
             }, py::arg("text"), py::arg("style"))
        .def("annotate", [](inl::Paragraph& p, inl::Annotation a) { p.annotations.push_back(std::move(a)); })
        .def("add_image",
             [](inl::Paragraph& p, std::shared_ptr<dl::Image> img, Size size, TextStyle st) {
                 p.addImage(img, size, std::move(st));
             },
             py::arg("image"), py::arg("size"), py::arg("style"), "行内画像を足す（本文中の位置は 1 文字ぶん）")
        .def("add_object",
             [](inl::Paragraph& p, std::string handler, std::u16string source, TextStyle st,
                std::map<std::string, std::string> params) {
                 p.addObject(std::move(handler), std::move(source), std::move(params), std::move(st));
             },
             py::arg("handler"), py::arg("source"), py::arg("style"),
             py::arg("params") = std::map<std::string, std::string>{},
             "外部ハンドラのオブジェクト（数式など）を行内に足す。layout(objects=...) の ObjectRegistry で解決される")
        .def_property_readonly("text", &inl::Paragraph::text);

    // ---- ブロック ----
    py::enum_<block::BreakKind>(m, "BreakKind")
        .value("AUTO", block::BreakKind::Auto)
        .value("COLUMN", block::BreakKind::Column)
        .value("PAGE", block::BreakKind::Page);
    py::class_<block::BlockStyle>(m, "BlockStyle")
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
    py::class_<block::ImageBlock>(m, "ImageBlock")
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
    py::class_<block::TableColumn>(m, "TableColumn")
        .def(py::init<>())
        .def(py::init([](Pt width, Align align) { return block::TableColumn{width, align}; }),
             py::arg("width") = 0.0f, py::arg("align") = Align::Start)
        .def_readwrite("width", &block::TableColumn::width)
        .def_readwrite("align", &block::TableColumn::align);
    py::class_<block::TableCell>(m, "TableCell")
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
    py::class_<block::TableRow>(m, "TableRow")
        .def(py::init<>())
        .def(py::init([](std::vector<block::TableCell> cells, bool header) {
                 block::TableRow r; r.cells = std::move(cells); r.header = header; return r;
             }),
             py::arg("cells"), py::arg("header") = false)
        .def_readwrite("cells", &block::TableRow::cells)
        .def_readwrite("header", &block::TableRow::header);
    py::class_<block::TableBorders>(m, "TableBorders")
        .def(py::init<>())
        .def_readwrite("outer", &block::TableBorders::outer)
        .def_readwrite("inner", &block::TableBorders::inner)
        .def_readwrite("header_rule", &block::TableBorders::headerRule)
        .def_readwrite("vertical", &block::TableBorders::vertical)
        .def_readwrite("horizontal", &block::TableBorders::horizontal)
        .def_readwrite("color", &block::TableBorders::color);
    py::class_<block::TableBlock>(m, "TableBlock")
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
    py::class_<block::ListBlock>(m, "ListBlock")
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
    py::class_<block::TocBlock>(m, "TocBlock")
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

    py::class_<block::Flow>(m, "Flow")
        .def(py::init<>())
        .def("add_paragraph", &block::Flow::addParagraph, py::arg("paragraph"),
             py::arg("style") = block::BlockStyle{})
        .def("add_heading", &block::Flow::addHeading, py::arg("paragraph"), py::arg("level") = 1,
             py::arg("style") = std::nullopt, py::arg("numbered") = false)
        .def("add_list", &block::Flow::addList, py::arg("list"))
        .def("add_toc", &block::Flow::addToc, py::arg("toc"))
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

    py::class_<page::Margins>(m, "Margins")
        .def(py::init<>())
        .def(py::init([](Pt top, Pt bottom, Pt inner, Pt outer) {
                 return page::Margins{top, bottom, inner, outer};
             }),
             py::arg("top"), py::arg("bottom"), py::arg("inner"), py::arg("outer"))
        .def_readwrite("top", &page::Margins::top)
        .def_readwrite("bottom", &page::Margins::bottom)
        .def_readwrite("inner", &page::Margins::inner)
        .def_readwrite("outer", &page::Margins::outer);
    py::class_<page::RunningText>(m, "RunningText")
        .def(py::init<>())
        .def(py::init([](inl::Paragraph p, Pt offset) { return page::RunningText{std::move(p), offset}; }),
             py::arg("paragraph"), py::arg("offset") = 0.0f)
        .def_readwrite("paragraph", &page::RunningText::para)
        .def_readwrite("offset", &page::RunningText::offset);
    py::class_<page::PageMaster>(m, "PageMaster")
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
    py::class_<page::PageSequence>(m, "PageSequence")
        .def(py::init<>())
        .def_readwrite("master", &page::PageSequence::master)
        .def_readwrite("first_page_number", &page::PageSequence::firstPageNumber);

    py::class_<page::Page>(m, "Page")
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
        .def("has", &obj::ObjectRegistry::has, py::arg("name"))
        .def("clear_cache", &obj::ObjectRegistry::clearCache)
        .def_property_readonly("cache_size", &obj::ObjectRegistry::cacheSize)
        .def_property_readonly("errors", &obj::ObjectRegistry::errors);

    py::class_<page::FlowLayouter>(m, "FlowLayouter")
        .def(py::init<font::FontSet&>(), py::arg("fonts"), py::keep_alive<1, 2>())
        .def("layout",
             [](page::FlowLayouter& l, const block::Flow& flow, const page::PageSequence& seq,
                std::map<std::u16string, std::u16string> fields, bool drawGuides, bool balanceLastPage,
                std::u16string figureFormat, std::u16string tableFormat, std::u16string equationFormat,
                obj::ObjectRegistry* objects) {
                 page::FlowLayoutOptions o;
                 o.fields = std::move(fields);
                 o.drawGuides = drawGuides;
                 o.balanceLastPage = balanceLastPage;
                 o.figureFormat = std::move(figureFormat);
                 o.tableFormat = std::move(tableFormat);
                 o.equationFormat = std::move(equationFormat);
                 o.objects = objects;
                 return l.layout(flow, seq, o);
             },
             py::arg("flow"), py::arg("sequence"),
             py::arg("fields") = std::map<std::u16string, std::u16string>{},
             py::arg("draw_guides") = false, py::arg("balance_last_page") = true,
             py::arg("figure_format") = std::u16string(u"図 {n}"),
             py::arg("table_format") = std::u16string(u"表 {n}"),
             py::arg("equation_format") = std::u16string(u"({n})"),
             py::arg("objects") = nullptr,
             "Flow をページ列へ流し込む。fields は柱・ノンブル・本文の {name} 置換");

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
    py::class_<LineInfo>(m, "LineInfo")
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
              const inl::ParagraphFragment frag = layouter.layout(para, wm, shape);
              std::vector<LineInfo> out;
              for (const inl::LineBox& l : frag.lines) {
                  out.push_back(LineInfo{l.charStart, l.charEnd, l.length, l.naturalLength, l.indent,
                                         l.paragraphEnd, l.hanging, l.lineIndex});
              }
              return out;
          },
          py::arg("fonts"), py::arg("paragraph"), py::arg("writing_mode"),
          py::arg("line_lengths") = std::vector<Pt>{}, py::arg("default_length") = 200.0f,
          "段落を組んで行ごとの文字範囲と長さを返す（行長は行ごとに指定できる = \\parshape）");
}
