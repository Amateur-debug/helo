#pragma once
#include "mem.h"

namespace helo::func {
// 从统一内存读取完整指令，不模拟缓存命中或延迟。
class Icache {
public:
    explicit Icache(Mem& memory);
    std::uint64_t fetch(std::uint32_t pc) const;

private:
    Mem& m_memory; // 引用取指访问的统一内存。
};
}
