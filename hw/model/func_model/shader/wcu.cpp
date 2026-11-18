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
        if (warp.m_group_barrier) {
            throw std::runtime_error("barrier protocol: BARW during group barrier");
        }
        if (warp.m_live_mask != warp.m_launch_mask) {
            throw std::runtime_error("barrier protocol: participant already exited");
        }
        if (warp.m_barrier_mask && warp.m_barrier_pc != instruction.m_decoded.m_path.m_pc) {
            throw std::runtime_error("barrier protocol: mismatched BARW PC");
        }
    }
}

// 对跨warp屏障固定参与集合检查提前退出、混用屏障及不同会合位置。
void Wcu::validate_group(const std::vector<Warp>& warps, const IssuedInstr& instruction) const {
    const auto op = instruction.m_decoded.m_instr.m_operation;
    if (op != Operation::EXIT && op != Operation::BARG) {
        return;
    }
    const auto& path = instruction.m_decoded.m_path;
    const auto& issuer = warps.at(path.m_warp);
    if (op == Operation::BARG && issuer.m_barrier_mask && !issuer.m_group_barrier) {
        throw std::runtime_error("barrier protocol: BARG during warp barrier");
    }
    for (const auto& warp : warps) {
        if (op == Operation::BARG && warp.m_launch_mask != warp.m_live_mask) {
            throw std::runtime_error("barrier protocol: workgroup participant already exited");
        }
        if (warp.m_group_barrier && op == Operation::EXIT) {
            throw std::runtime_error("barrier protocol: EXIT during pending BARG");
        }
        if (warp.m_group_barrier && warp.m_barrier_pc != path.m_pc) {
            throw std::runtime_error("barrier protocol: mismatched BARG PC");
        }
    }
}

// 收集每个warp实际启动的线程，仅在整个工作组到齐后推进所有参与者的PC。
void Wcu::complete_group(std::vector<Warp>& warps, const ExecutionResult& result) const {
    const auto& path = result.m_decoded.m_path;
    auto& issuer = warps.at(path.m_warp);
    issuer.m_group_barrier = true;
    issuer.m_barrier_mask |= path.m_mask;
    issuer.m_barrier_pc = path.m_pc;
    for (const auto& warp : warps) {
        if (!warp.m_group_barrier || warp.m_barrier_mask != warp.m_launch_mask) {
            return;
        }
    }
    for (auto& warp : warps) {
        for (std::size_t lane = 0; lane < WARP_SIZE; ++lane) {
            if (warp.m_launch_mask & (1u << lane)) {
                warp.m_pc[lane] = path.m_pc + 8;
            }
        }
        warp.m_barrier_mask = 0;
        warp.m_barrier_pc = 0;
        warp.m_group_barrier = false;
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
        case Operation::BARG:
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
