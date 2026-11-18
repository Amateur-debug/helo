#include "wmu.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace helo::func {
// 根据任务寄存器声明和三类硬件资源计算完整warp驻留槽位。
std::size_t Wmu::resident_capacity(std::size_t max_warps,
    std::size_t max_threads,
    std::size_t regfile_bytes,
    unsigned register_count) const {
    if (!register_count || register_count > REGISTER_COUNT) {
        throw std::invalid_argument("register count must be in 1..256");
    }
    const auto hardware_limit = std::min(max_warps, max_threads / WARP_SIZE);
    const auto bytes_per_warp = WARP_SIZE * (register_count - 1) * sizeof(std::uint32_t);
    return bytes_per_warp ? std::min(hardware_limit, regfile_bytes / bytes_per_warp)
                          : hardware_limit;
}

// 检查任务容量并按连续线程编号创建warp上下文。
void Wmu::launch(std::vector<Warp>& warps,
    std::uint32_t entry,
    std::size_t thread_count,
    std::size_t capacity) const {
    if (entry % 8) {
        throw std::invalid_argument("misaligned entry PC");
    }
    if (thread_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("thread count exceeds 32 bits");
    }
    const auto count = thread_count / WARP_SIZE + (thread_count % WARP_SIZE != 0);
    if (count > capacity) {
        throw std::invalid_argument("shader warp capacity exceeded");
    }
    warps.assign(count, Warp{});
    for (std::size_t w = 0; w < count; ++w) {
        warps[w].m_id = w;
        warps[w].m_pc.fill(entry);
        const auto remaining = thread_count - w * WARP_SIZE;
        warps[w].m_live_mask =
            remaining >= WARP_SIZE ? 0xffffffffu : (std::uint32_t(1) << remaining) - 1;
        warps[w].m_launch_mask = warps[w].m_live_mask;
    }
}

// 当全部warp的线程均退出时报告任务完成。
bool Wmu::completed(const std::vector<Warp>& warps) const {
    for (const auto& warp : warps) {
        if (warp.m_live_mask) {
            return false;
        }
    }
    return true;
}
}
