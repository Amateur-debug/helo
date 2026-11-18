#pragma once
#include "common/types.h"

namespace helo::func {
// 负责RGBA8颜色量化和源透明度混合。
class Crop {
public:
    std::uint32_t pack(const Color& color) const;
    Color unpack(std::uint32_t word) const;
    Color blend(const Color& source, const Color& destination) const;
};
}
