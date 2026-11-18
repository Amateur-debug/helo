#include "rop/crop.h"
#include <algorithm>
#include <cmath>

namespace helo::func {
namespace {
// 把颜色分量限制到UNORM范围，NaN确定映射为零。
float saturate(float value) {
    return std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 1.0f);
}
}

// 按R最低字节的顺序将浮点颜色四舍五入为RGBA8。
std::uint32_t Crop::pack(const Color& color) const {
    std::uint32_t word = 0;
    for (unsigned c = 0; c < 4; ++c) {
        word |= std::uint32_t(std::floor(saturate(color[c]) * 255.0f + 0.5f)) << (8 * c);
    }
    return word;
}

// 将RGBA8四个字节转换为归一化浮点分量。
Color Crop::unpack(std::uint32_t word) const {
    Color color{};
    for (unsigned c = 0; c < 4; ++c) {
        color[c] = float((word >> (8 * c)) & 255) / 255.0f;
    }
    return color;
}

// 以非预乘源颜色执行source-over混合，透明度单独累积。
Color Crop::blend(const Color& source, const Color& destination) const {
    Color color{};
    const float alpha = saturate(source[3]);
    for (unsigned c = 0; c < 3; ++c) {
        color[c] = saturate(source[c]) * alpha + saturate(destination[c]) * (1 - alpha);
    }
    color[3] = alpha + saturate(destination[3]) * (1 - alpha);
    return color;
}
}
