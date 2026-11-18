#include "tex/tex.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

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
        const double fraction = double(value) - std::floor(double(value));
        // 极小负数的差在double中可能舍入为1，最近点应归属接缝左侧。
        return fraction == 1.0 ? std::nextafter(1.0, 0.0) : fraction;
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

// 按从粗化前到粗化后的顺序验证整条紧密排列的mip链。
Tex::Sampler Tex::prepare(const TextureDesc& texture, unsigned level_count) const {
    if (level_count == 0 || level_count > 32) {
        throw std::invalid_argument("invalid texture mip level count");
    }
    std::vector<TextureDesc> levels;
    levels.reserve(level_count);
    auto level = texture;
    for (unsigned i = 0; i < level_count; ++i) {
        validate(level);
        levels.push_back(level);
        if (i + 1 == level_count) {
            break;
        }
        if (level.m_width == 1 && level.m_height == 1) {
            throw std::invalid_argument("texture mip chain extends beyond 1x1");
        }
        const auto next = level.m_address + std::uint64_t(level.m_width) * level.m_height * 4;
        if (next > std::numeric_limits<std::uint32_t>::max()) {
            throw std::out_of_range("texture mip address overflow");
        }
        level.m_address = static_cast<std::uint32_t>(next);
        level.m_width = std::max(1U, level.m_width / 2);
        level.m_height = std::max(1U, level.m_height / 2);
    }
    return Sampler(*this, std::move(levels));
}

// 读取每槽七字的内存描述符，所有资源验证完成后才交给Shader。
Tex::Bindings Tex::prepare_bindings(std::uint32_t address, unsigned count) const {
    constexpr unsigned DESCRIPTOR_BYTES = 28;
    if (count > 16 || (!count && address) ||
        std::uint64_t(address) + std::uint64_t(count) * DESCRIPTOR_BYTES > (1ULL << 32)) {
        throw std::invalid_argument("invalid shader texture binding table");
    }
    Bindings bindings;
    bindings.reserve(count);
    for (unsigned slot = 0; slot < count; ++slot) {
        const auto base = address + slot * DESCRIPTOR_BYTES;
        TextureDesc texture;
        texture.m_address = m_memory.read(base, 4);
        texture.m_width = m_memory.read(base + 4, 4);
        texture.m_height = m_memory.read(base + 8, 4);
        const auto filter = m_memory.read(base + 12, 4);
        const auto wrap_u = m_memory.read(base + 16, 4);
        const auto wrap_v = m_memory.read(base + 20, 4);
        if (filter > 1 || wrap_u > 1 || wrap_v > 1) {
            throw std::invalid_argument("invalid shader texture sampling state");
        }
        texture.m_filter = static_cast<TextureFilter>(filter);
        texture.m_wrap_u = static_cast<TextureWrap>(wrap_u);
        texture.m_wrap_v = static_cast<TextureWrap>(wrap_v);
        bindings.push_back(prepare(texture, m_memory.read(base + 24, 4)));
    }
    return bindings;
}

// 保存各级的布局快照，后续纹素读取仍直接访问统一内存。
Tex::Sampler::Sampler(const Tex& owner, std::vector<TextureDesc> levels)
    : m_owner(owner), m_levels(std::move(levels)) {
}

// 将显式LOD夹到有效范围，再按小数权重混合相邻两级过滤结果。
Color Tex::Sampler::sample(float u, float v, float lod) const {
    if (m_levels.empty()) {
        throw std::logic_error("empty texture sampler");
    }
    if (!std::isfinite(lod)) {
        throw std::invalid_argument("nonfinite texture LOD");
    }
    const float bounded = std::clamp(lod, 0.0f, static_cast<float>(m_levels.size() - 1));
    const auto base = static_cast<unsigned>(bounded);
    const float weight = bounded - static_cast<float>(base);
    auto color = m_owner.sample_validated(m_levels[base], u, v);
    if (weight > 0) {
        const auto upper = m_owner.sample_validated(m_levels[base + 1], u, v);
        for (unsigned channel = 0; channel < 4; ++channel) {
            color[channel] += weight * (upper[channel] - color[channel]);
        }
    }
    return color;
}

// 先在整数域处理包裹或钳位，保留大坐标选取纹素时用到的低16位。
Color Tex::Sampler::sample_q16(std::int32_t u, std::int32_t v) const {
    const auto& level = m_levels.at(0);
    const auto coordinate = [](std::int32_t value, TextureWrap wrap) {
        const auto fraction = wrap == TextureWrap::REPEAT
                                  ? (static_cast<std::uint32_t>(value) & 65535U)
                                  : static_cast<unsigned>(std::clamp(value, 0, 65536));
        return float(fraction) / 65536;
    };
    return m_owner.sample_validated(
        level, coordinate(u, level.m_wrap_u), coordinate(v, level.m_wrap_v));
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
