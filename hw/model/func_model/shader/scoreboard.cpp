#include "scoreboard.h"
#include <stdexcept>

namespace helo::func {
namespace {
// 校验通用寄存器编号，避免非法编号访问浮点状态槽位。
unsigned slot(unsigned reg) {
    if (reg >= REGISTER_COUNT) {
        throw std::out_of_range("register index");
    }
    return reg;
}
}

// 为寄存器及线程浮点控制状态分配占用掩码。
Scoreboard::Scoreboard(std::size_t warp_count) : m_pending(warp_count) {
}

// 清除所有未完成的依赖。
void Scoreboard::reset() {
    for (auto& pending : m_pending) {
        pending = {};
    }
}

// 检查三个源、目标及浮点状态的冲突，整数与浮点共享槽位且r0不占用。
bool Scoreboard::ready(const BufferedInstr& instruction) const {
    const auto& pending = m_pending.at(instruction.m_path.m_warp);
    const auto& d = instruction.m_instr;
    const auto mask = instruction.m_path.m_mask;
    // 判断指定寄存器是否有当前线程的未完成写入。
    const auto busy = [&](bool used, unsigned reg) {
        return used && reg != 0 && (pending.at(slot(reg)) & mask);
    };
    return !busy(d.m_uses_rs1, d.m_rs1) && !busy(d.m_uses_rs2, d.m_rs2) &&
           !busy(d.m_uses_rs3, d.m_rs3) && !busy(d.m_writes_rd, d.m_rd) &&
           !(d.m_uses_fcsr && (pending.at(REGISTER_COUNT) & mask));
}

// 登记目标和浮点状态，保守串行化同线程的浮点运算与CSR访问。
void Scoreboard::reserve(const BufferedInstr& instruction) {
    if (!ready(instruction)) {
        throw std::logic_error("scoreboard dependency conflict");
    }
    const auto& d = instruction.m_instr;
    auto& pending = m_pending.at(instruction.m_path.m_warp);
    if (d.m_writes_rd && d.m_rd != 0) {
        pending.at(slot(d.m_rd)) |= instruction.m_path.m_mask;
    }
    if (d.m_uses_fcsr) {
        pending.at(REGISTER_COUNT) |= instruction.m_path.m_mask;
    }
}

// 解除提交或取消指令持有的占用。
void Scoreboard::release(const BufferedInstr& instruction) {
    const auto& d = instruction.m_instr;
    auto& pending = m_pending.at(instruction.m_path.m_warp);
    if (d.m_writes_rd && d.m_rd != 0) {
        pending.at(slot(d.m_rd)) &= ~instruction.m_path.m_mask;
    }
    if (d.m_uses_fcsr) {
        pending.at(REGISTER_COUNT) &= ~instruction.m_path.m_mask;
    }
}
}
