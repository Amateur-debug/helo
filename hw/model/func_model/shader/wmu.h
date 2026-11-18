#pragma once
#include "warp.h"
#include <vector>

namespace helo::func {
// 根据硬件资源接纳任务，初始化 warp 上下文并判断任务完成。
class Wmu {
public:
    std::size_t resident_capacity(std::size_t max_warps,
        std::size_t max_threads,
        std::size_t regfile_bytes,
        unsigned register_count) const;
    void launch(std::vector<Warp>& warps,
        std::uint32_t entry,
        std::size_t thread_count,
        std::size_t capacity) const;
    bool completed(const std::vector<Warp>& warps) const;
};
}
