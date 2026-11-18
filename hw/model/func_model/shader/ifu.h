#pragma once
#include "../mem/icache.h"
#include "warp.h"

namespace helo::func {
// 通过指令缓存接口读取调度路径上的机器码。
class Ifu {
public:
    explicit Ifu(Icache& cache);
    std::uint64_t fetch(const ScheduledWarp& path) const;

private:
    Icache& m_cache; // 引用用于读取机器码的指令缓存接口。
};
}
