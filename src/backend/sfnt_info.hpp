#ifndef TYPESET_BACKEND_SFNT_INFO_HPP
#define TYPESET_BACKEND_SFNT_INFO_HPP

#include <cstddef>
#include <cstdint>

/**
 * sfnt_info — フォントファイル（sfnt）から PDF の FontDescriptor に必要な値を拾う
 *
 * アウトラインが CFF か glyf かで CIDFontType0 / CIDFontType2 の別と
 * FontFile3 / FontFile2 の別が決まる。FreeType を経由せずテーブルディレクトリを
 * 直接読むのは、PDF 側で必要な値が head / hhea / post / OS/2 の生の値そのもの
 * だからで、変換を挟むと丸めが入る。（richtext SfntInfo の移植）
 */
namespace typeset::backend {

struct SfntInfo {
    bool valid = false;
    bool isCFF = false;         ///< true なら CFF（OpenType/CFF）、false なら glyf
    bool isCollection = false;  ///< TTC。サブセット化して単体フォントにしてから埋め込む（丸ごとは埋め込めない）
    uint32_t dirOffset = 0;     ///< 表ディレクトリの位置（TTC では face ごとに異なる）

    uint16_t unitsPerEm = 1000;
    int16_t xMin = 0, yMin = 0, xMax = 0, yMax = 0;   ///< head の FontBBox
    int16_t ascender = 0, descender = 0;              ///< hhea
    int16_t capHeight = 0;                            ///< OS/2（無ければ 0）
    float italicAngle = 0.0f;                         ///< post
    bool isFixedPitch = false;
    bool isSerif = false;                             ///< OS/2 の PANOSE から推定
    uint16_t fsType = 0;                              ///< OS/2 の埋め込み許可ビット（無ければ 0 = Installable）

    /// 埋め込み許可の判定（OpenType 仕様 OS/2 fsType）
    bool embeddingRestricted() const { return (fsType & 0x000F) == 0x0002; }   ///< Restricted License: 埋め込み不可
    bool noSubsetting() const { return (fsType & 0x0100) != 0; }                ///< サブセット化不可（丸ごと埋め込む）
    bool bitmapOnly() const { return (fsType & 0x0200) != 0; }                  ///< アウトラインの埋め込み不可
};

/// @param faceIndex TTC のときに読む face 番号
bool parseSfnt(const uint8_t* data, size_t size, SfntInfo& out, int faceIndex = 0);

} // namespace typeset::backend

#endif // TYPESET_BACKEND_SFNT_INFO_HPP
