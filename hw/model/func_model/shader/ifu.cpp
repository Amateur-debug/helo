#include "ifu.h"

namespace helo::func {
// 连接指令缓存端口。
Ifu::Ifu(Icache& cache) : m_cache(cache) {
}

// 按调度路径读取指令，不修改warp控制状态。
std::uint64_t Ifu::fetch(const ScheduledWarp& path) const {
    return m_cache.fetch(path.m_pc);
}
}
