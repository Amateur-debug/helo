#include "pa.h"
#include <stdexcept>

namespace helo::func {
// 仅验证列表长度及索引，不分配或复制三角形数据。
void Pa::validate(std::size_t vertex_count, const std::vector<std::uint32_t>& indices) const {
    const auto count = indices.empty() ? vertex_count : indices.size();
    if (count % 3) {
        throw std::invalid_argument("triangle list count must be a multiple of three");
    }
    for (const auto index : indices) {
        if (index >= vertex_count) {
            throw std::out_of_range("vertex index out of range");
        }
    }
}

// 将每三个输入元素组成一个三角形，并拒绝不完整图元与越界索引。
std::vector<Triangle> Pa::assemble(
    const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices) const {
    const auto count = indices.empty() ? vertices.size() : indices.size();
    validate(vertices.size(), indices);
    std::vector<Triangle> result;
    result.reserve(count / 3);
    for (std::size_t i = 0; i < count; i += 3) {
        Triangle triangle;
        for (std::size_t j = 0; j < 3; ++j) {
            triangle[j] = vertices[indices.empty() ? i + j : indices[i + j]];
        }
        result.push_back(triangle);
    }
    return result;
}
}
