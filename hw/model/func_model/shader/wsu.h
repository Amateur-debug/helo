#pragma once
#include "warp.h"
#include <optional>
#include <vector>

namespace helo::func {
// 轮转选择 warp 与线程，将相同 PC 的可执行线程组成调度路径。
class Wsu {
public:
    void reset();
    std::optional<ScheduledWarp> select(std::vector<Warp>& warps);

private:
    std::size_t m_next_warp = 0; // 记录下一轮优先尝试调度的 warp 索引。
};
}
