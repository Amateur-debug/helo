#pragma once
#include "clip.h"
#include "common/config.h"
#include "common/types.h"
#include "pa.h"
#include "vfetch.h"
#include "vp.h"
#include <vector>

namespace helo::func {
// 组合图元装配、齐次裁剪、视口变换与面剔除。
class Geom {
public:
    explicit Geom(const Mem& memory);
    std::vector<Vertex> fetch(std::uint32_t address,
        std::uint32_t count,
        std::uint32_t stride = 40,
        std::uint32_t layout_address = 0,
        unsigned varying_count = 0) const;
    std::vector<std::uint32_t> fetch_indices(std::uint32_t address, std::uint32_t count) const;
    void validate_assembly(
        std::size_t vertex_count, const std::vector<std::uint32_t>& indices) const;
    std::vector<Triangle> assemble(
        const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices = {}) const;
    std::vector<ScreenTriangle> process(const std::vector<Vertex>& vertices,
        const std::vector<std::uint32_t>& indices,
        const GeometryState& state) const;

private:
    Vfetch m_vfetch; // 持有顶点及索引获取单元。
    Pa m_pa; // 持有三角形装配单元。
    Clip m_clip; // 持有齐次裁剪单元。
    Vp m_vp; // 持有视口变换单元。
};
}
