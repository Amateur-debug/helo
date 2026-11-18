#include "regfile.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace helo::func {
// 建立可配置的物理GPR预算，浮点控制状态独立于GPR预算。
Regfile::Regfile(std::size_t max_warps, std::size_t capacity_bytes)
    : m_capacity_bytes(capacity_bytes), m_warp_capacity(max_warps) {
    if (!max_warps) {
        throw std::invalid_argument("regfile requires warp capacity");
    }
    if (max_warps > std::numeric_limits<std::size_t>::max() / GPR_BYTES_PER_WARP) {
        throw std::length_error("regfile capacity overflow");
    }
    if (capacity_bytes % sizeof(std::uint32_t)) {
        throw std::invalid_argument("regfile size must be word aligned");
    }
}

// 在替换旧存储前验证任务需求并准备新的紧凑寄存器上下文。
void Regfile::configure(std::size_t warp_count, unsigned register_count) {
    if (!register_count || register_count > REGISTER_COUNT) {
        throw std::invalid_argument("register count must be in 1..256");
    }
    const auto words_per_warp = WARP_SIZE * (register_count - 1);
    if (warp_count > m_warp_capacity ||
        (words_per_warp &&
            warp_count > m_capacity_bytes / sizeof(std::uint32_t) / words_per_warp)) {
        throw std::invalid_argument("regfile capacity exceeded");
    }
    // 相同布局直接清零并复用容量，连续图形批次无需重新分配。
    if (m_fcsr.size() == warp_count && m_register_count == register_count) {
        reset();
        return;
    }
    std::vector<std::uint32_t> scalar(warp_count * words_per_warp);
    std::vector<std::array<std::uint32_t, WARP_SIZE>> fcsr(warp_count);
    m_scalar.swap(scalar);
    m_fcsr.swap(fcsr);
    m_register_count = register_count;
}

// 返回配置的物理GPR字节预算。
std::size_t Regfile::capacity_bytes() const {
    return m_capacity_bytes;
}

// 返回硬件允许的warp上下文上限。
std::size_t Regfile::warp_capacity() const {
    return m_warp_capacity;
}

// 返回当前任务声明的逻辑寄存器数量，包含r0。
unsigned Regfile::register_count() const {
    return m_register_count;
}

// 拒绝超出当前任务声明范围的寄存器引用。
void Regfile::validate_register(unsigned reg) const {
    if (reg >= m_register_count) {
        throw std::out_of_range("register exceeds task allocation");
    }
}

// 清除现有任务寄存器及浮点状态。
void Regfile::reset() {
    std::fill(m_scalar.begin(), m_scalar.end(), 0);
    for (auto& warp : m_fcsr) {
        warp = {};
    }
}

// 读取任务分配范围内的寄存器，r0不占物理存储。
std::uint32_t Regfile::read_gpr(std::size_t warp, std::size_t thread, unsigned reg) const {
    m_fcsr.at(warp).at(thread);
    validate_register(reg);
    return reg ? m_scalar.at((warp * WARP_SIZE + thread) * (m_register_count - 1) + reg - 1) : 0;
}

// 写入紧凑物理槽位，r0写入丢弃。
void Regfile::write_scalar(
    std::size_t warp, std::size_t thread, unsigned reg, std::uint32_t value) {
    m_fcsr.at(warp).at(thread);
    validate_register(reg);
    if (reg) {
        m_scalar.at((warp * WARP_SIZE + thread) * (m_register_count - 1) + reg - 1) = value;
    }
}

// 读取线程独立的浮点控制状态。
std::uint32_t Regfile::read_fcsr(std::size_t warp, std::size_t thread) const {
    return m_fcsr.at(warp).at(thread);
}

// 写入浮点控制状态并清除保留位。
void Regfile::write_fcsr(std::size_t warp, std::size_t thread, std::uint32_t value) {
    m_fcsr.at(warp).at(thread) = value & 0xff;
}
}
