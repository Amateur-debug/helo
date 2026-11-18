#include "rop/rop.h"
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace helo::func {
namespace {
// 保留FP32深度的原始位模式供内存写入。
std::uint32_t depth_bits(float depth) {
    std::uint32_t bits;
    std::memcpy(&bits, &depth, sizeof(bits));
    return bits;
}

// 将内存中的FP32位模式恢复为深度值。
float depth_value(std::uint32_t bits) {
    float depth;
    std::memcpy(&depth, &bits, sizeof(depth));
    return depth;
}
}

// 将片元输出单元连接到统一内存。
Rop::Rop(Mem& memory) : m_memory(memory) {
}

// 在产生任何写入前检查所有附件范围及相互重叠。
void Rop::validate(const FramebufferDesc& framebuffer) const {
    if (framebuffer.m_width == 0 || framebuffer.m_height == 0) {
        throw std::invalid_argument("empty framebuffer");
    }
    const std::uint64_t pixels = std::uint64_t(framebuffer.m_width) * framebuffer.m_height;
    if (framebuffer.m_samples != 1 && framebuffer.m_samples != 4) {
        throw std::invalid_argument("framebuffer requires one or four samples");
    }
    if (pixels > (std::uint64_t(1) << 32) / 4 / framebuffer.m_samples) {
        throw std::out_of_range("framebuffer size overflow");
    }
    const auto samples = pixels * framebuffer.m_samples;
    const std::uint64_t addresses[4] = {framebuffer.m_color_address,
        framebuffer.m_depth_address,
        framebuffer.m_stencil_address,
        framebuffer.m_color1_address};
    const std::uint64_t sizes[4] = {samples * 4,
        framebuffer.m_has_depth ? samples * 4 : 0,
        framebuffer.m_has_stencil ? samples : 0,
        framebuffer.m_has_color1 ? samples * 4 : 0};
    for (unsigned i = 0; i < 4; ++i) {
        if (sizes[i] == 0) {
            continue;
        }
        const auto end = addresses[i] + sizes[i];
        if (end > (std::uint64_t(1) << 32)) {
            throw std::out_of_range("framebuffer address overflow");
        }
        m_memory.validate(std::uint32_t(addresses[i]), i == 2 ? 1 : 4);
        m_memory.validate(std::uint32_t(end - 1), 1);
        for (unsigned j = 0; j < i; ++j) {
            if (sizes[j] && addresses[i] < addresses[j] + sizes[j] && addresses[j] < end) {
                throw std::invalid_argument("overlapping framebuffer attachments");
            }
        }
    }
}

// 同时清除已配置的颜色、深度与模板附件。
void Rop::clear(
    const FramebufferDesc& framebuffer, const Color& color, float depth, std::uint8_t stencil) {
    validate(framebuffer);
    if (!std::isfinite(depth) || depth < 0 || depth > 1) {
        throw std::invalid_argument("clear depth outside zero to one");
    }
    const auto packed = m_crop.pack(color, framebuffer.m_srgb);
    const auto bits = depth_bits(depth);
    const std::uint64_t pixels = std::uint64_t(framebuffer.m_width) * framebuffer.m_height;
    for (std::uint64_t i = 0; i < pixels * framebuffer.m_samples; ++i) {
        m_memory.write(std::uint32_t(framebuffer.m_color_address + i * 4), 4, packed);
        if (framebuffer.m_has_color1) {
            m_memory.write(std::uint32_t(framebuffer.m_color1_address + i * 4), 4, packed);
        }
        if (framebuffer.m_has_depth) {
            m_memory.write(std::uint32_t(framebuffer.m_depth_address + i * 4), 4, bits);
        }
        if (framebuffer.m_has_stencil) {
            m_memory.write(std::uint32_t(framebuffer.m_stencil_address + i), 1, stencil);
        }
    }
}

// 对目标内的片元执行测试，并只提交通过条件允许的附件更新。
bool Rop::write(
    const FramebufferDesc& framebuffer, const RopState& state, const Fragment& fragment) {
    return prepare(framebuffer).write(state, fragment);
}

// 检查完整附件布局并返回当前ROP专属的目标句柄。
Rop::Target Rop::prepare(const FramebufferDesc& framebuffer) {
    validate(framebuffer);
    return Target(*this, framebuffer);
}

// 复制已验证的布局以免调用方后续修改描述符绕过检查。
Rop::Target::Target(Rop& owner, const FramebufferDesc& framebuffer)
    : m_owner(owner), m_framebuffer(framebuffer) {
}

// 复用布局验证结果并执行一个片元的原有提交协议。
bool Rop::Target::write(const RopState& state, const Fragment& fragment) const {
    return m_owner.write_validated(m_framebuffer, state, fragment);
}

// 在已验证附件上检查片元和状态，并按模板、深度、颜色顺序写入。
bool Rop::write_validated(
    const FramebufferDesc& framebuffer, const RopState& state, const Fragment& fragment) {
    if (fragment.m_x >= framebuffer.m_width || fragment.m_y >= framebuffer.m_height ||
        fragment.m_sample >= framebuffer.m_samples) {
        return false;
    }
    if (!std::isfinite(fragment.m_depth) || fragment.m_depth < 0 || fragment.m_depth > 1) {
        return false;
    }
    const auto& ds = state.m_depth_stencil;
    if (ds.m_stencil_test && !framebuffer.m_has_stencil) {
        throw std::invalid_argument("stencil test requires attachment");
    }
    if ((ds.m_depth_test || ds.m_depth_write) && !framebuffer.m_has_depth) {
        throw std::invalid_argument("depth state requires attachment");
    }
    const std::uint64_t pixel =
        (std::uint64_t(fragment.m_y) * framebuffer.m_width + fragment.m_x) * framebuffer.m_samples +
        fragment.m_sample;
    const std::array<std::uint32_t, 2> color_address{
        std::uint32_t(framebuffer.m_color_address + pixel * 4),
        std::uint32_t(framebuffer.m_color1_address + pixel * 4)};
    const unsigned targets = framebuffer.m_has_color1 ? 2 : 1;
    const auto depth_address = std::uint32_t(framebuffer.m_depth_address + pixel * 4);
    const auto stencil_address = std::uint32_t(framebuffer.m_stencil_address + pixel);
    const auto old_stencil =
        ds.m_stencil_test ? std::uint8_t(m_memory.read(stencil_address, 1)) : std::uint8_t(0);
    const bool stencil_pass =
        !ds.m_stencil_test || m_zrop.compare(ds.m_stencil_compare,
                                  ds.m_stencil_reference & ds.m_stencil_read_mask,
                                  old_stencil & ds.m_stencil_read_mask);
    const bool depth_pass = !ds.m_depth_test || m_zrop.compare(ds.m_depth_compare,
                                                    fragment.m_depth,
                                                    depth_value(m_memory.read(depth_address, 4)));
    const bool pass = stencil_pass && depth_pass;
    const auto stencil_op =
        !stencil_pass ? ds.m_stencil_fail : (!depth_pass ? ds.m_depth_fail : ds.m_pass);
    const auto new_stencil =
        ds.m_stencil_test
            ? m_zrop.stencil(
                  stencil_op, old_stencil, ds.m_stencil_reference, ds.m_stencil_write_mask)
            : old_stencil;
    std::array<std::uint32_t, 2> colors{};
    if (pass && state.m_color_write) {
        for (unsigned i = 0; i < targets; ++i) {
            const auto& source = i == 0 ? fragment.m_color : fragment.m_color1;
            const auto linear =
                state.m_blend
                    ? m_crop.blend(source,
                          m_crop.unpack(m_memory.read(color_address[i], 4), framebuffer.m_srgb),
                          state.m_factors)
                    : source;
            colors[i] = m_crop.pack(linear, framebuffer.m_srgb);
        }
    }
    if (ds.m_stencil_test) {
        m_memory.write(stencil_address, 1, new_stencil);
    }
    if (!pass) {
        return false;
    }
    if (ds.m_depth_write) {
        m_memory.write(depth_address, 4, depth_bits(fragment.m_depth));
    }
    if (state.m_color_write) {
        for (unsigned i = 0; i < targets; ++i) {
            m_memory.write(color_address[i], 4, colors[i]);
        }
    }
    return true;
}
}
