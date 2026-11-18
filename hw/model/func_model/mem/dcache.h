#pragma once
#include "mem.h"

namespace helo::func {
// 将数据访问直通转发到统一内存，不模拟缓存命中或延迟。
class Dcache {
public:
    explicit Dcache(Mem& memory);
    std::uint32_t read(std::uint32_t address, unsigned size) const;
    void write(std::uint32_t address, unsigned size, std::uint32_t value);
    void validate(std::uint32_t address, unsigned size) const;

private:
    Mem& m_memory; // 引用数据访问最终读写的统一内存。
};
}
