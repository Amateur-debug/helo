#include "tex/tex.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace helo::func {
namespace {
// 将整数纹素坐标按各向边界规则映射到纹理内部。
std::uint32_t texel_index(std::int64_t index, std::uint32_t size, TextureWrap wrap) {
    if (wrap == TextureWrap::REPEAT) {
        return std::uint32_t((index % size + size) % size);
    }
    return std::uint32_t(std::clamp<std::int64_t>(index, 0, std::int64_t(size) - 1));
}

// 在放大为纹素坐标之前规约归一化坐标，避免整数溢出。
double normalize(float value, TextureWrap wrap) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("nonfinite texture coordinate");
    }
    if (wrap == TextureWrap::REPEAT) {
        return double(value) - std::floor(double(value));
    }
    return std::clamp(double(value), 0.0, 1.0);
}
}

// 将采样器连接至统一内存。
Tex::Tex(Mem& memory) : m_memory(memory) {
}

// 检查纹理尺寸、状态枚举与整个内存范围。
void Tex::validate(const TextureDesc& texture) const {
    if (texture.m_width == 0 || texture.m_height == 0) {
        throw std::invalid_argument("empty texture");
    }
    if (texture.m_filter != TextureFilter::NEAREST && texture.m_filter != TextureFilter::BILINEAR) {
        throw std::invalid_argument("invalid texture filter");
    }
    if ((texture.m_wrap_u != TextureWrap::CLAMP && texture.m_wrap_u != TextureWrap::REPEAT) ||
        (texture.m_wrap_v != TextureWrap::CLAMP && texture.m_wrap_v != TextureWrap::REPEAT)) {
        throw std::invalid_argument("invalid texture wrap");
    }
    const std::uint64_t pixels = std::uint64_t(texture.m_width) * texture.m_height;
    if (pixels > (std::uint64_t(1) << 32) / 4) {
        throw std::out_of_range("texture address overflow");
    }
    const std::uint64_t end = std::uint64_t(texture.m_address) + pixels * 4;
    if (end > (std::uint64_t(1) << 32)) {
        throw std::out_of_range("texture address overflow");
    }
    m_memory.validate(texture.m_address, 4);
    m_memory.validate(std::uint32_t(end - 1), 1);
}

// 用纹素中心约定完成最近点或双线性采样。
Color Tex::sample(const TextureDesc& texture, float u, float v) const {
    return prepare(texture).sample(u, v);
}

// 验证完整布局并生成不可伪造的采样句柄。
Tex::Sampler Tex::prepare(const TextureDesc& texture) const {
    validate(texture);
    return Sampler(*this, texture);
}

// 绑定纹理单元并复制本次操作的资源状态。
Tex::Sampler::Sampler(const Tex& owner, const TextureDesc& texture)
    : m_owner(owner), m_texture(texture) {
}

// 复用描述符检查结果，同时保留逐次坐标校验和数据可见性。
Color Tex::Sampler::sample(float u, float v) const {
    return m_owner.sample_validated(m_texture, u, v);
}

// 对已验证布局执行采样，纹理字节从不缓存。
Color Tex::sample_validated(const TextureDesc& texture, float u, float v) const {
    const double s = normalize(u, texture.m_wrap_u) * texture.m_width;
    const double t = normalize(v, texture.m_wrap_v) * texture.m_height;
    // 读取一个经过边界映射的RGBA8纹素并转换成浮点颜色。
    const auto read_texel = [&](std::int64_t x, std::int64_t y) {
        const auto ix = texel_index(x, texture.m_width, texture.m_wrap_u);
        const auto iy = texel_index(y, texture.m_height, texture.m_wrap_v);
        const auto address = std::uint32_t(
            std::uint64_t(texture.m_address) + (std::uint64_t(iy) * texture.m_width + ix) * 4);
        const auto word = m_memory.read(address, 4);
        Color color{};
        for (unsigned c = 0; c < 4; ++c) {
            color[c] = float((word >> (c * 8)) & 255) / 255.0f;
        }
        return color;
    };
    if (texture.m_filter == TextureFilter::NEAREST) {
        return read_texel(std::int64_t(std::floor(s)), std::int64_t(std::floor(t)));
    }
    const auto x = std::int64_t(std::floor(s - 0.5));
    const auto y = std::int64_t(std::floor(t - 0.5));
    const float fx = float(s - 0.5 - double(x));
    const float fy = float(t - 0.5 - double(y));
    const Color a = read_texel(x, y), b = read_texel(x + 1, y), c = read_texel(x, y + 1),
                d = read_texel(x + 1, y + 1);
    Color result{};
    for (unsigned channel = 0; channel < 4; ++channel) {
        result[channel] = (a[channel] * (1 - fx) + b[channel] * fx) * (1 - fy) +
                          (c[channel] * (1 - fx) + d[channel] * fx) * fy;
    }
    return result;
}
}
