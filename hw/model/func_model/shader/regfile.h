#pragma once
#include "warp.h"
#include <vector>

namespace helo::func {
// 按任务寄存器配置保存各线程的通用寄存器与独立浮点状态。
class Regfile {
public:
    static constexpr std::size_t GPR_BYTES_PER_THREAD =
        (REGISTER_COUNT - 1) *
        sizeof(std::uint32_t); // 表示每线程使用最大寄存器配置时的 GPR 字节数，不计 r0。
    static constexpr std::size_t GPR_BYTES_PER_WARP =
        WARP_SIZE * GPR_BYTES_PER_THREAD; // 表示完整 warp 使用最大寄存器配置时的 GPR 字节数。
    explicit Regfile(std::size_t max_warps, std::size_t capacity_bytes = 261120);
    void configure(std::size_t warp_count, unsigned register_count);
    unsigned register_count() const;
    void validate_register(unsigned reg) const;
    std::size_t capacity_bytes() const;
    std::size_t warp_capacity() const;
    void reset();
    std::uint32_t read_gpr(std::size_t warp, std::size_t thread, unsigned reg) const;
    void write_scalar(std::size_t warp, std::size_t thread, unsigned reg, std::uint32_t value);
    std::uint32_t read_fcsr(std::size_t warp, std::size_t thread) const;
    void write_fcsr(std::size_t warp, std::size_t thread, std::uint32_t value);

private:
    std::size_t m_capacity_bytes; // 保存硬件 GPR 字节预算，不包含浮点控制状态。
    std::size_t m_warp_capacity; // 保存硬件允许分配的 warp 上限。
    unsigned m_register_count = REGISTER_COUNT; // 保存当前任务声明的逻辑寄存器数，包含 r0。
    std::vector<std::uint32_t> m_scalar; // 按 warp、线程及非零寄存器编号紧凑保存 GPR 数据。
    std::vector<std::array<std::uint32_t, WARP_SIZE>>
        m_fcsr; // 按 warp 和线程保存独立浮点控制状态。
};
}
