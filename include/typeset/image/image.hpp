#ifndef TYPESET_IMAGE_IMAGE_HPP
#define TYPESET_IMAGE_IMAGE_HPP

#include <memory>
#include <string>

#include "typeset/dl/display_list.hpp"

/**
 * image — 画像の読み込みとエンコード
 *
 * 読み込みは stb_image（PNG / JPEG / GIF / BMP …）。JPEG は元のバイト列を保持して
 * PDF へ素通し（DCTDecode）できるようにする。PNG エンコードは SVG の data URI と
 * ラスタ backend の出力で共用する。
 */
namespace typeset::image {

std::shared_ptr<dl::Image> loadFile(const std::string& path);
std::shared_ptr<dl::Image> loadMemory(const void* data, size_t size);

/// RGBA8888 から生成した画像（テスト・サンプル用）
std::shared_ptr<dl::Image> fromRgba(int width, int height, const uint8_t* rgba);

/// RGBA8888 → PNG バイト列（zlib）
std::string encodePng(const uint8_t* rgba, int width, int height);

/// 画像 → PNG（encoded が PNG ならそれをそのまま返す）
std::string toPng(const dl::Image& img);

std::string base64(const std::string& bytes);

/// 画像に不透明でない画素があるか
bool hasAlpha(const dl::Image& img);

} // namespace typeset::image

#endif // TYPESET_IMAGE_IMAGE_HPP
