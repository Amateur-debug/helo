#include "gpu.h"
#include <stdexcept>

namespace helo::func {
// 创建具有独立统一内存和具名Shader资源配置的GPU功能模型。
Gpu::Gpu(const GpuConfig& config)
    : m_memory(config.m_memory_bytes), m_shader(m_memory, config.m_shader), m_geom(m_memory),
      m_tex(m_memory), m_rop(m_memory),
      m_cp(m_memory, m_geom, m_rast, m_shader, m_tex, m_rop, m_stats) {
}

// 返回可供主机上传程序与资源的统一内存。
Mem& Gpu::memory() {
    return m_memory;
}

// 返回只读统一内存以检查设备执行结果。
const Mem& Gpu::memory() const {
    return m_memory;
}

// 返回复位以来的实际执行统计。
const GpuStats& Gpu::stats() const {
    return m_stats;
}

// 将内存中的command buffer范围交给CP统一解析。
void Gpu::submit(std::uint32_t address, std::uint32_t byte_size) {
    m_cp.enqueue(address, byte_size);
}

// 执行给定命令预算并返回完成状态或锁存错误。
SubmitResult Gpu::run(std::size_t max_commands) {
    return m_cp.run(max_commands);
}

// 清除控制状态和统计，同时保留已上传资源及附件。
void Gpu::reset() {
    m_cp.reset();
    m_shader.reset();
    m_stats = {};
}

// 返回尚未完成的command buffer数量。
std::size_t Gpu::pending() const {
    return m_cp.pending();
}

// 将颜色附件按自上而下的RGBA8紧密字节布局读回主机。
std::vector<std::uint8_t> Gpu::read_color(
    const FramebufferDesc& framebuffer, unsigned attachment) const {
    if (attachment > 1 || (attachment == 1 && !framebuffer.m_has_color1) ||
        (framebuffer.m_samples != 1 && framebuffer.m_samples != 4)) {
        throw std::invalid_argument("invalid readback target or sample count");
    }
    const auto address =
        attachment == 0 ? framebuffer.m_color_address : framebuffer.m_color1_address;
    const std::uint64_t pixels = std::uint64_t(framebuffer.m_width) * framebuffer.m_height;
    if (pixels == 0 || pixels > (0x100000000ULL - address) / 4 / framebuffer.m_samples) {
        throw std::invalid_argument("invalid framebuffer readback range");
    }
    m_memory.validate(address, 4);
    m_memory.validate(
        address + static_cast<std::uint32_t>((pixels * framebuffer.m_samples - 1) * 4), 4);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(pixels * 4));
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        const auto first = address + static_cast<std::uint32_t>(pixel * 4 * framebuffer.m_samples);
        auto value = m_memory.read(first, 4);
        if (framebuffer.m_samples == 4) {
            Crop crop;
            Color sum{};
            for (unsigned sample = 0; sample < 4; ++sample) {
                const auto decoded = crop.unpack(
                    sample ? m_memory.read(first + sample * 4, 4) : value, framebuffer.m_srgb);
                for (unsigned c = 0; c < 4; ++c) {
                    sum[c] += decoded[c] * 0.25F;
                }
            }
            value = crop.pack(sum, framebuffer.m_srgb);
        }
        for (unsigned channel = 0; channel < 4; ++channel) {
            rgba[pixel * 4 + channel] = static_cast<std::uint8_t>(value >> (channel * 8));
        }
    }
    return rgba;
}
}
