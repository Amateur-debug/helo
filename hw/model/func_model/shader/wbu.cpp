#include "wbu.h"

namespace helo::func {
// 按发射掩码提交整数、浮点结果及异常状态，再解除依赖。
void Wbu::writeback(
    const ExecutionResult& result, Regfile& registers, Scoreboard& scoreboard) const {
    const auto& decoded = result.m_decoded;
    for (std::size_t thread = 0; thread < WARP_SIZE; ++thread) {
        if (!(decoded.m_path.m_mask & (1u << thread))) {
            continue;
        }
        if (decoded.m_instr.m_writes_rd) {
            registers.write_scalar(
                decoded.m_path.m_warp, thread, decoded.m_instr.m_rd, result.m_values[thread]);
        }
        if (result.m_writes_fcsr) {
            registers.write_fcsr(decoded.m_path.m_warp, thread, result.m_fcsr[thread]);
        }
    }
    scoreboard.release(decoded);
}
}
