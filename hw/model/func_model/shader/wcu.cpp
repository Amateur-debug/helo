#include "wcu.h"
#include <stdexcept>
#include <string>

namespace helo::func {
// 在提交任何副作用前检查固定参与集合的屏障协议。
void Wcu::validate(const Warp& warp, const IssuedInstr& instruction) const {
    const auto operation = instruction.m_decoded.m_instr.m_operation;
    if (operation == Operation::EXIT && warp.m_barrier_mask) {
        throw std::runtime_error("barrier protocol: EXIT during pending barrier");
    }
    if (operation == Operation::BARW) {
        if (warp.m_live_mask != warp.m_launch_mask) {
            throw std::runtime_error("barrier protocol: participant already exited");
        }
        if (warp.m_barrier_mask && warp.m_barrier_pc != instruction.m_decoded.m_path.m_pc) {
            throw std::runtime_error("barrier protocol: mismatched BARW PC");
        }
    }
}

// 根据各线程旧操作数计算分支、跳转与退出，跳转目标必须八字节对齐。
ExecutionResult Wcu::execute(const IssuedInstr& instruction) const {
    ExecutionResult result;
    result.m_decoded = instruction.m_decoded;
    const auto& decoded = instruction.m_decoded.m_instr;
    const auto& path = instruction.m_decoded.m_path;
    for (std::size_t t = 0; t < WARP_SIZE; ++t) {
        if (!(path.m_mask & (1u << t))) {
            continue;
        }
        const auto a = instruction.m_src1[t];
        const auto b = instruction.m_src2[t];
        auto next = path.m_pc + 8;
        bool taken = false;
        switch (decoded.m_operation) {
        case Operation::BEQ:
            taken = a == b;
            break;
        case Operation::BNE:
            taken = a != b;
            break;
        case Operation::BLT:
            taken = (a ^ 0x80000000u) < (b ^ 0x80000000u);
            break;
        case Operation::BGE:
            taken = (a ^ 0x80000000u) >= (b ^ 0x80000000u);
            break;
        case Operation::BLTU:
            taken = a < b;
            break;
        case Operation::BGEU:
            taken = a >= b;
            break;
        case Operation::JAL:
            taken = true;
            result.m_values[t] = next;
            break;
        case Operation::JALR:
            next = a + decoded.m_immediate;
            result.m_values[t] = path.m_pc + 8;
            break;
        case Operation::TRAP:
            throw std::runtime_error("explicit trap " + std::to_string(decoded.m_immediate));
        case Operation::BARW:
            next = path.m_pc;
            break;
        case Operation::EXIT:
            result.m_exit_mask |= 1u << t;
            break;
        default:
            throw std::logic_error("instruction routed to wrong control unit");
        }
        if (taken) {
            next = path.m_pc + decoded.m_immediate;
        }
        if (next % 8) {
            throw std::runtime_error("misaligned instruction target");
        }
        result.m_next_pc[t] = next;
    }
    return result;
}

// 提交本次路径的线程PC与退出掩码，其他发散路径保持不变。
void Wcu::complete(Warp& warp, const ExecutionResult& result) const {
    if (result.m_decoded.m_instr.m_operation == Operation::BARW) {
        warp.m_barrier_pc = result.m_decoded.m_path.m_pc;
        warp.m_barrier_mask |= result.m_decoded.m_path.m_mask;
        if (warp.m_barrier_mask == warp.m_launch_mask) {
            for (std::size_t t = 0; t < WARP_SIZE; ++t) {
                if (warp.m_launch_mask & (1u << t)) {
                    warp.m_pc[t] = warp.m_barrier_pc + 8;
                }
            }
            warp.m_barrier_mask = 0;
            warp.m_barrier_pc = 0;
        }
        return;
    }
    for (std::size_t t = 0; t < WARP_SIZE; ++t) {
        if (result.m_decoded.m_path.m_mask & (1u << t)) {
            warp.m_pc[t] = result.m_next_pc[t];
        }
    }
    warp.m_live_mask &= ~result.m_exit_mask;
}
}
