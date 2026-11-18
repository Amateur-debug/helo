#include "iu.h"
#include <stdexcept>

namespace helo::func {
// 对有效线程执行32位整数运算，所有加减按模2^32定义。
ExecutionResult Iu::execute(const IssuedInstr& instruction) const {
    ExecutionResult result;
    result.m_decoded = instruction.m_decoded;
    const auto& decoded = instruction.m_decoded.m_instr;
    const auto& path = instruction.m_decoded.m_path;
    for (std::size_t thread = 0; thread < WARP_SIZE; ++thread) {
        if (!(path.m_mask & (1u << thread))) {
            continue;
        }
        const auto a = instruction.m_src1[thread];
        const auto b =
            decoded.m_immediate_operand ? decoded.m_immediate : instruction.m_src2[thread];
        const auto shift = b & 31;
        auto& value = result.m_values[thread];
        switch (decoded.m_operation) {
        case Operation::LUI:
            value = decoded.m_immediate;
            break;
        case Operation::AUIPC:
            value = path.m_pc + decoded.m_immediate;
            break;
        case Operation::ADD:
            value = a + b;
            break;
        case Operation::SUB:
            value = a - b;
            break;
        case Operation::SLL:
            value = a << shift;
            break;
        case Operation::SLT:
            value = (a ^ 0x80000000u) < (b ^ 0x80000000u);
            break;
        case Operation::SLTU:
            value = a < b;
            break;
        case Operation::XOR:
            value = a ^ b;
            break;
        case Operation::SRL:
            value = a >> shift;
            break;
        case Operation::SRA:
            value = a >> shift;
            if (shift && (a & 0x80000000u)) {
                value |= 0xffffffffu << (32 - shift);
            }
            break;
        case Operation::OR:
            value = a | b;
            break;
        case Operation::AND:
            value = a & b;
            break;
        case Operation::MUL:
            value = static_cast<std::uint32_t>(std::uint64_t(a) * b);
            break;
        case Operation::MULHU:
            value = static_cast<std::uint32_t>((std::uint64_t(a) * b) >> 32);
            break;
        case Operation::MULH:
        case Operation::MULHSU: {
            // 先形成64位补码数值，避免有符号32位溢出及负数右移。
            const auto sa = std::int64_t(a) - ((a & 0x80000000u) ? (std::int64_t(1) << 32) : 0);
            const auto sb =
                std::int64_t(b) - ((decoded.m_operation == Operation::MULH && (b & 0x80000000u))
                                          ? (std::int64_t(1) << 32)
                                          : 0);
            value = static_cast<std::uint32_t>(static_cast<std::uint64_t>(sa * sb) >> 32);
            break;
        }
        case Operation::DIVU:
            value = b == 0 ? 0xffffffffu : a / b;
            break;
        case Operation::REMU:
            value = b == 0 ? a : a % b;
            break;
        case Operation::DIV:
        case Operation::REM: {
            const auto sa = std::int64_t(a) - ((a & 0x80000000u) ? (std::int64_t(1) << 32) : 0);
            const auto sb = std::int64_t(b) - ((b & 0x80000000u) ? (std::int64_t(1) << 32) : 0);
            value = decoded.m_operation == Operation::DIV
                        ? (b == 0 ? 0xffffffffu : static_cast<std::uint32_t>(sa / sb))
                        : (b == 0 ? a : static_cast<std::uint32_t>(sa % sb));
            break;
        }
        case Operation::MOV:
            value = a;
            break;
        case Operation::NOP:
        case Operation::FENCE:
            break;
        default:
            throw std::logic_error("instruction routed to wrong integer unit");
        }
        result.m_next_pc[thread] = path.m_pc + 8;
    }
    return result;
}
}
