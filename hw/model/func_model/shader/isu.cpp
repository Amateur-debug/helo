#include "isu.h"

namespace helo::func {
// 在依赖满足时读取源操作数、登记目标并从缓冲出队。
std::optional<IssuedInstr> Isu::issue(
    std::size_t warp, Ibuffer& buffer, Scoreboard& scoreboard, const Regfile& registers) const {
    const auto* candidate = buffer.front(warp);
    if (!candidate || !scoreboard.ready(*candidate)) {
        return std::nullopt;
    }
    const auto& decoded = candidate->m_instr;
    if (decoded.m_uses_rs1) {
        registers.validate_register(decoded.m_rs1);
    }
    if (decoded.m_uses_rs2) {
        registers.validate_register(decoded.m_rs2);
    }
    if (decoded.m_uses_rs3) {
        registers.validate_register(decoded.m_rs3);
    }
    if (decoded.m_writes_rd) {
        registers.validate_register(decoded.m_rd);
    }
    IssuedInstr issued;
    issued.m_decoded = *candidate;
    for (std::size_t thread = 0; thread < WARP_SIZE; ++thread) {
        if (!(candidate->m_path.m_mask & (1u << thread))) {
            continue;
        }
        const auto& decoded = candidate->m_instr;
        if (decoded.m_uses_rs1) {
            issued.m_src1[thread] = registers.read_gpr(warp, thread, decoded.m_rs1);
        }
        if (decoded.m_uses_rs2) {
            issued.m_src2[thread] = registers.read_gpr(warp, thread, decoded.m_rs2);
        }
        if (decoded.m_uses_rs3) {
            issued.m_src3[thread] = registers.read_gpr(warp, thread, decoded.m_rs3);
        }
        if (decoded.m_uses_fcsr) {
            issued.m_fcsr[thread] = registers.read_fcsr(warp, thread);
        }
    }
    scoreboard.reserve(*candidate);
    buffer.pop(warp);
    return issued;
}
}
