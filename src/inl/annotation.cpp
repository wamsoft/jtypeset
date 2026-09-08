#include "typeset/inl/annotation.hpp"

namespace typeset::inl {

char32_t emphasisMarkCodePoint(EmphasisMark mark) {
    switch (mark) {
    case EmphasisMark::Sesame:       return 0xFE45;   // ﹅ 縦書き用ゴマ点
    case EmphasisMark::OpenSesame:   return 0xFE46;   // ﹆ 白ゴマ点
    case EmphasisMark::Dot:          return 0x30FB;   // ・
    case EmphasisMark::FilledCircle: return 0x25CF;   // ●
    case EmphasisMark::OpenCircle:   return 0x25CB;   // ○
    }
    return 0xFE45;
}

} // namespace typeset::inl
