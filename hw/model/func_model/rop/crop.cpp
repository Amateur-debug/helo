#include "rop/crop.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace helo::func {
namespace {
// 把颜色分量限制到UNORM范围，NaN确定映射为零。
float saturate(float value) {
    return std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 1.0f);
}

// 从线性空间或sRGB空间转换RGB分量，两种格式共用线性alpha。
float encode_srgb(float value) {
    return value <= 0.0031308F ? 12.92F * value : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

// 将已归一化的sRGB字节解码到混合与解析所用的线性空间。
float decode_srgb(float value) {
    return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

// 对当前通道取源或目标分量作为加法混合权重。
float factor(BlendFactor mode, unsigned c, const Color& src, const Color& dst) {
    switch (mode) {
    case BlendFactor::ZERO:
        return 0;
    case BlendFactor::ONE:
        return 1;
    case BlendFactor::SRC_ALPHA:
        return saturate(src[3]);
    case BlendFactor::INV_SRC_ALPHA:
        return 1 - saturate(src[3]);
    case BlendFactor::DST_ALPHA:
        return saturate(dst[3]);
    case BlendFactor::INV_DST_ALPHA:
        return 1 - saturate(dst[3]);
    case BlendFactor::SRC_COLOR:
        return saturate(src[c]);
    case BlendFactor::INV_SRC_COLOR:
        return 1 - saturate(src[c]);
    case BlendFactor::DST_COLOR:
        return saturate(dst[c]);
    case BlendFactor::INV_DST_COLOR:
        return 1 - saturate(dst[c]);
    }
    throw std::invalid_argument("invalid blend factor");
}
}

// 按R最低字节的顺序将浮点颜色四舍五入为RGBA8。
std::uint32_t Crop::pack(const Color& color, bool srgb) const {
    std::uint32_t word = 0;
    for (unsigned c = 0; c < 4; ++c) {
        const float linear = saturate(color[c]);
        const float value = srgb && c < 3 ? encode_srgb(linear) : linear;
        word |= std::uint32_t(std::floor(value * 255.0f + 0.5f)) << (8 * c);
    }
    return word;
}

// 将RGBA8四个字节转换为归一化浮点分量。
Color Crop::unpack(std::uint32_t word, bool srgb) const {
    Color color{};
    for (unsigned c = 0; c < 4; ++c) {
        const float value = float((word >> (8 * c)) & 255) / 255.0f;
        color[c] = srgb && c < 3 ? decode_srgb(value) : value;
    }
    return color;
}

// 以非预乘源颜色执行source-over混合，透明度单独累积。
Color Crop::blend(
    const Color& source, const Color& destination, const BlendFactors& factors) const {
    Color color{};
    for (unsigned c = 0; c < 4; ++c) {
        const auto src = c == 3 ? factors.m_src_alpha : factors.m_src_rgb;
        const auto dst = c == 3 ? factors.m_dst_alpha : factors.m_dst_rgb;
        color[c] = saturate(source[c]) * factor(src, c, source, destination) +
                   saturate(destination[c]) * factor(dst, c, source, destination);
    }
    return color;
}
}
