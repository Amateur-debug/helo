#pragma once
#include "common/types.h"
#include <vector>

namespace helo::func {
class Mem;

// 从统一内存按声明布局装载顶点与索引。
class Vfetch {
public:
    explicit Vfetch(const Mem& memory);
    std::vector<Vertex> fetch(std::uint32_t address,
        std::uint32_t count,
        std::uint32_t stride = 40,
        std::uint32_t layout_address = 0,
        unsigned varying_count = 0) const;
    std::vector<std::uint32_t> fetch_indices(std::uint32_t address, std::uint32_t count) const;

private:
    const Mem& m_memory; // 引用构造时绑定的统一顶点与索引存储。
};
}
