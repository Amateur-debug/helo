#pragma once
#include "common/types.h"
#include <vector>

namespace helo::func {
// 将连续顶点或显式索引组装为三角形列表。
class Pa {
public:
    void validate(std::size_t vertex_count, const std::vector<std::uint32_t>& indices = {}) const;
    std::vector<Triangle> assemble(
        const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices = {}) const;
};
}
