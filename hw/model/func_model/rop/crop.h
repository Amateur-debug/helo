#pragma once
#include "common/types.h"

namespace helo::func {
// 负责RGBA8颜色量化和源透明度混合。
class Crop {
public:
    std::uint32_t pack(const Color& color, bool srgb = false) const;
    Color unpack(std::uint32_t word, bool srgb = false) const;
    Color blend(
        const Color& source, const Color& destination, const BlendFactors& factors = {}) const;
};
}
