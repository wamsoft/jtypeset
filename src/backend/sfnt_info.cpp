/**
 * sfnt_info.cpp
 */

#include "sfnt_info.hpp"

namespace typeset::backend {

namespace {

inline uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
inline int16_t readS16(const uint8_t* p) {
    return static_cast<int16_t>(readU16(p));
}
inline uint32_t readU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

constexpr uint32_t tag(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(d));
}

struct Table {
    uint32_t offset = 0;
    uint32_t length = 0;
    bool found = false;
};

Table findTable(const uint8_t* data, size_t size, uint32_t wanted, size_t dir = 0) {
    Table out;
    if (size < dir + 12) return out;
    const uint16_t numTables = readU16(data + dir + 4);
    const size_t dirEnd = dir + 12 + static_cast<size_t>(numTables) * 16;
    if (dirEnd > size) return out;

    for (uint16_t i = 0; i < numTables; ++i) {
        const uint8_t* rec = data + dir + 12 + static_cast<size_t>(i) * 16;
        if (readU32(rec) != wanted) continue;
        const uint32_t off = readU32(rec + 8);
        const uint32_t len = readU32(rec + 12);
        if (off > size) return out;
        out.offset = off;
        out.length = len;
        out.found = true;
        return out;
    }
    return out;
}

} // namespace

bool parseSfnt(const uint8_t* data, size_t size, SfntInfo& out, int faceIndex) {
    out = SfntInfo{};
    if (!data || size < 12) return false;

    size_t dir = 0;
    uint32_t sfntVersion = readU32(data);
    if (sfntVersion == tag('t', 't', 'c', 'f')) {
        // TTC: 表ディレクトリは face ごと。表のオフセットはファイル先頭からなので dir を差し替えるだけでよい
        out.isCollection = true;
        const uint32_t numFonts = readU32(data + 8);
        if (faceIndex < 0 || static_cast<uint32_t>(faceIndex) >= numFonts ||
            12 + static_cast<size_t>(faceIndex) * 4 + 4 > size) {
            return false;
        }
        dir = readU32(data + 12 + static_cast<size_t>(faceIndex) * 4);
        if (dir + 12 > size) return false;
        sfntVersion = readU32(data + dir);
    }
    if (sfntVersion != 0x00010000 && sfntVersion != tag('O', 'T', 'T', 'O') &&
        sfntVersion != tag('t', 'r', 'u', 'e')) {
        return false;
    }
    out.dirOffset = static_cast<uint32_t>(dir);

    out.isCFF = findTable(data, size, tag('C', 'F', 'F', ' '), dir).found;

    const Table head = findTable(data, size, tag('h', 'e', 'a', 'd'), dir);
    if (head.found && head.offset + 54 <= size) {
        const uint8_t* p = data + head.offset;
        out.unitsPerEm = readU16(p + 18);
        out.xMin = readS16(p + 36);
        out.yMin = readS16(p + 38);
        out.xMax = readS16(p + 40);
        out.yMax = readS16(p + 42);
    }
    if (out.unitsPerEm == 0) out.unitsPerEm = 1000;

    const Table hhea = findTable(data, size, tag('h', 'h', 'e', 'a'), dir);
    if (hhea.found && hhea.offset + 36 <= size) {
        const uint8_t* p = data + hhea.offset;
        out.ascender = readS16(p + 4);
        out.descender = readS16(p + 6);
    }

    const Table post = findTable(data, size, tag('p', 'o', 's', 't'), dir);
    if (post.found && post.offset + 32 <= size) {
        const uint8_t* p = data + post.offset;
        const int32_t fixed = static_cast<int32_t>(readU32(p + 4));   // 16.16
        out.italicAngle = static_cast<float>(fixed) / 65536.0f;
        out.isFixedPitch = readU32(p + 12) != 0;
    }

    const Table os2 = findTable(data, size, tag('O', 'S', '/', '2'), dir);
    if (os2.found && os2.offset + 96 <= size) {
        const uint8_t* p = data + os2.offset;
        const uint16_t version = readU16(p);
        out.fsType = readU16(p + 8);
        const uint8_t familyType = p[32];
        const uint8_t serifStyle = p[33];
        out.isSerif = (familyType == 2) && (serifStyle >= 2 && serifStyle <= 10);
        if (version >= 2 && os2.offset + 90 <= size) {
            out.capHeight = readS16(p + 88);
        }
    }

    out.valid = true;
    return true;
}

} // namespace typeset::backend
