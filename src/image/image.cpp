/**
 * image.cpp — 画像の読み込み（stb_image）と PNG エンコード（zlib）
 */

#include "typeset/image/image.hpp"

#include <cstring>
#include <fstream>
#include <iterator>

#include <zlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include <stb_image.h>

namespace typeset::image {

namespace {

dl::Image::Encoding sniff(const uint8_t* d, size_t n) {
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return dl::Image::Encoding::Png;
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return dl::Image::Encoding::Jpeg;
    return dl::Image::Encoding::None;
}

void putU32(std::string& out, uint32_t v) {
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>(v & 0xFF));
}

void putChunk(std::string& out, const char* type, const std::string& data) {
    putU32(out, static_cast<uint32_t>(data.size()));
    std::string body(type, 4);
    body += data;
    out += body;
    const uint32_t crc = static_cast<uint32_t>(
        crc32(0, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size())));
    putU32(out, crc);
}

} // namespace

std::shared_ptr<dl::Image> loadMemory(const void* data, size_t size) {
    if (!data || size == 0) return nullptr;
    int w = 0, h = 0, ch = 0;
    stbi_uc* px = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size),
                                        &w, &h, &ch, 4);
    if (!px) return nullptr;
    auto img = std::make_shared<dl::Image>();
    img->width = w;
    img->height = h;
    img->rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    img->encoding = sniff(bytes, size);
    if (img->encoding != dl::Image::Encoding::None) img->encoded.assign(bytes, bytes + size);
    return img;
}

std::shared_ptr<dl::Image> loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return loadMemory(bytes.data(), bytes.size());
}

std::shared_ptr<dl::Image> fromRgba(int width, int height, const uint8_t* rgba) {
    if (width <= 0 || height <= 0 || !rgba) return nullptr;
    auto img = std::make_shared<dl::Image>();
    img->width = width;
    img->height = height;
    img->rgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
    return img;
}

std::string encodePng(const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return std::string();
    std::string raw;
    raw.reserve(static_cast<size_t>(width) * height * 4 + height);
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);   // filter: none
        raw.append(reinterpret_cast<const char*>(rgba + static_cast<size_t>(y) * width * 4),
                   static_cast<size_t>(width) * 4);
    }
    uLongf destLen = compressBound(static_cast<uLong>(raw.size()));
    std::string zdata(destLen, '\0');
    if (compress2(reinterpret_cast<Bytef*>(zdata.data()), &destLen,
                  reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), 6) != Z_OK) {
        return std::string();
    }
    zdata.resize(destLen);

    std::string out("\x89PNG\r\n\x1a\n", 8);
    std::string ihdr;
    putU32(ihdr, static_cast<uint32_t>(width));
    putU32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);
    ihdr.push_back(6);   // RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    putChunk(out, "IHDR", ihdr);
    putChunk(out, "IDAT", zdata);
    putChunk(out, "IEND", std::string());
    return out;
}

std::string toPng(const dl::Image& img) {
    if (img.encoding == dl::Image::Encoding::Png && !img.encoded.empty()) {
        return std::string(img.encoded.begin(), img.encoded.end());
    }
    return encodePng(img.rgba.data(), img.width, img.height);
}

std::string base64(const std::string& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 2 < in.size()) {
        const uint32_t v = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8) |
                           static_cast<uint8_t>(in[i + 2]);
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);
        out.push_back(tbl[v & 63]);
        i += 3;
    }
    if (i + 1 == in.size()) {
        const uint32_t v = static_cast<uint8_t>(in[i]) << 16;
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out += "==";
    } else if (i + 2 == in.size()) {
        const uint32_t v = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8);
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

bool hasAlpha(const dl::Image& img) {
    for (size_t i = 3; i < img.rgba.size(); i += 4) {
        if (img.rgba[i] != 255) return true;
    }
    return false;
}

} // namespace typeset::image
