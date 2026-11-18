#include "lsu.h"
#include <stdexcept>

namespace helo::func {
// 连接数据缓存接口。
Lsu::Lsu(Dcache& cache) : m_cache(cache) {
}

// 在分批存储之前验证完整执行组的地址，保证后续批次错误不会留下部分写入。
void Lsu::validate(const IssuedInstr& instruction) const {
    unsigned width = 0;
    switch (instruction.m_decoded.m_instr.m_operation) {
    case Operation::LB:
    case Operation::LBU:
    case Operation::SB:
        width = 1;
        break;
    case Operation::LH:
    case Operation::LHU:
    case Operation::SH:
        width = 2;
        break;
    case Operation::LW:
    case Operation::SW:
    case Operation::LDS_W:
    case Operation::STS_W:
    case Operation::ATOM_ADD_SHARED:
    case Operation::ATOM_ADD_GLOBAL:
        width = 4;
        break;
    default:
        throw std::logic_error("instruction routed to wrong load/store unit");
    }
    for (std::size_t lane = 0; lane < WARP_SIZE; ++lane) {
        if (instruction.m_decoded.m_path.m_mask & (ThreadMask(1) << lane)) {
            m_cache.validate(
                instruction.m_src1[lane] + instruction.m_decoded.m_instr.m_immediate, width);
        }
    }
}

// 先验证全部有效地址再顺序访存，失败的线程组不会部分写入。
ExecutionResult Lsu::execute(const IssuedInstr& instruction) {
    ExecutionResult result;
    result.m_decoded = instruction.m_decoded;
    const auto& decoded = instruction.m_decoded.m_instr;
    const auto& path = instruction.m_decoded.m_path;
    unsigned width = 0;
    bool store = false;
    bool sign = false;
    switch (decoded.m_operation) {
    case Operation::LB:
        width = 1;
        sign = true;
        break;
    case Operation::LH:
        width = 2;
        sign = true;
        break;
    case Operation::LW:
    case Operation::LDS_W:
    case Operation::ATOM_ADD_SHARED:
    case Operation::ATOM_ADD_GLOBAL:
        width = 4;
        break;
    case Operation::LBU:
        width = 1;
        break;
    case Operation::LHU:
        width = 2;
        break;
    case Operation::SB:
        width = 1;
        store = true;
        break;
    case Operation::SH:
        width = 2;
        store = true;
        break;
    case Operation::SW:
    case Operation::STS_W:
        width = 4;
        store = true;
        break;
    default:
        throw std::logic_error("instruction routed to wrong load/store unit");
    }
    for (std::size_t t = 0; t < WARP_SIZE; ++t) {
        if (path.m_mask & (1u << t)) {
            m_cache.validate(instruction.m_src1[t] + decoded.m_immediate, width);
        }
    }
    for (std::size_t t = 0; t < WARP_SIZE; ++t) {
        if (!(path.m_mask & (1u << t))) {
            continue;
        }
        const auto address = instruction.m_src1[t] + decoded.m_immediate;
        if (store) {
            m_cache.write(address, width, instruction.m_src2[t]);
        } else {
            auto value = m_cache.read(address, width);
            if (decoded.m_operation == Operation::ATOM_ADD_SHARED ||
                decoded.m_operation == Operation::ATOM_ADD_GLOBAL) {
                m_cache.write(address, width, value + instruction.m_src2[t]);
            }
            if (sign) {
                const auto sign_bit = 1u << (width * 8 - 1);
                value = (value ^ sign_bit) - sign_bit;
            }
            result.m_values[t] = value;
        }
        result.m_next_pc[t] = path.m_pc + 8;
    }
    return result;
}
}
