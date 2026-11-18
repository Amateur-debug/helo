#include "wsu.h"

namespace helo::func {
// 将轮转调度游标恢复至首个warp。
void Wsu::reset() {
    m_next_warp = 0;
}

// 轮转选择warp及活跃线程，将相同PC的线程组合成当前路径。
std::optional<ScheduledWarp> Wsu::select(std::vector<Warp>& warps) {
    for (std::size_t i = 0; i < warps.size(); ++i) {
        const auto index = (m_next_warp + i) % warps.size();
        auto& warp = warps[index];
        const auto ready_mask = warp.m_live_mask & ~warp.m_barrier_mask;
        if (!ready_mask) {
            continue;
        }
        for (std::size_t n = 0; n < WARP_SIZE; ++n) {
            const auto thread = (warp.m_next_thread + n) % WARP_SIZE;
            if (!(ready_mask & (1u << thread))) {
                continue;
            }
            ScheduledWarp path{index, warp.m_pc[thread], 0};
            for (std::size_t t = 0; t < WARP_SIZE; ++t) {
                if ((ready_mask & (1u << t)) && warp.m_pc[t] == path.m_pc) {
                    path.m_mask |= 1u << t;
                }
            }
            warp.m_next_thread = (thread + 1) % WARP_SIZE;
            m_next_warp = (index + 1) % warps.size();
            return path;
        }
    }
    return std::nullopt;
}
}
