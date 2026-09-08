#ifndef TYPESET_BACKEND_PDF_WRITER_HPP
#define TYPESET_BACKEND_PDF_WRITER_HPP

#include <memory>
#include <string>
#include <vector>

#include "typeset/dl/display_list.hpp"

/**
 * backend/pdf_writer — 表示リストを PDF へ
 *
 * グリフ ID を直接書く（Identity-H + CIDFontType0 / CIDFontType2）ので、
 * ビューア側で再シェイピングされない。richtext PdfWriter の移植。
 *
 *  - 1 グリフずつ Tm で置く。縦組みでも Identity-V は使わない（縦メトリクスは
 *    組版層で計算済み）
 *  - Tm の 2x2 はグリフ固有の変形だけ。フォントサイズは Tf が掛ける
 *  - 画面（y-down）→ PDF（y-up）は Y 反転で共役を取る
 *  - フォントは既定で hb-subset により使用グリフだけに切り出す（グリフ ID は保持するので
 *    Identity-H の対応は変わらない）。コンテンツストリームは Flate で圧縮する
 */
namespace typeset::backend {

class PdfWriter {
public:
    PdfWriter();
    ~PdfWriter();

    PdfWriter(const PdfWriter&) = delete;
    PdfWriter& operator=(const PdfWriter&) = delete;

    void setTitle(std::string title);
    void setAuthor(std::string author);
    void setCreator(std::string creator);

    /// ToUnicode CMap を埋め込む（既定 true）。GlyphRun::text が無いランでは埋まらない
    void setEmbedToUnicode(bool embed);

    /// フォントを使用グリフだけにサブセット化する（既定 true。失敗したフォントは full embed）
    void setSubsetFonts(bool subset);

    /// ストリームを Flate で圧縮する（既定 true。デバッグ時に false にすると中身が読める）
    void setCompressStreams(bool compress);

    /**
     * ページを追加する（表示リスト 1 本 = 1 ページ）
     */
    void addPage(const dl::DisplayList& list);

    size_t pageCount() const;

    /// PDF のバイト列を組み立てる
    std::string build();

    /// ファイルへ書き出す
    bool save(const std::string& path);

    /// 埋め込めなかったフォント等
    const std::vector<std::string>& warnings() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace typeset::backend

#endif // TYPESET_BACKEND_PDF_WRITER_HPP
