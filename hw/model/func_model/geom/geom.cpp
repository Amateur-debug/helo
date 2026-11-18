#include "geom.h"
#include <stdexcept>

namespace helo::func {
// 构造几何子模块并将顶点获取端口连接到统一内存。
Geom::Geom(const Mem& memory) : m_vfetch(memory) {
}

// 通过持有的获取单元装载顶点记录。
std::vector<Vertex> Geom::fetch(std::uint32_t address,
    std::uint32_t count,
    std::uint32_t stride,
    std::uint32_t layout_address,
    unsigned varying_count) const {
    return m_vfetch.fetch(address, count, stride, layout_address, varying_count);
}

// 通过持有的获取单元装载索引记录。
std::vector<std::uint32_t> Geom::fetch_indices(std::uint32_t address, std::uint32_t count) const {
    return m_vfetch.fetch_indices(address, count);
}

// 在顶点着色前检查装配参数而不创建待丢弃的图元副本。
void Geom::validate_assembly(
    std::size_t vertex_count, const std::vector<std::uint32_t>& indices) const {
    m_pa.validate(vertex_count, indices);
}

// 通过持有的装配单元验证并组装图元。
std::vector<Triangle> Geom::assemble(
    const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices) const {
    return m_pa.assemble(vertices, indices);
}

// 处理三角形列表并剔除退化图元和状态指定朝向的面。
std::vector<ScreenTriangle> Geom::process(const std::vector<Vertex>& vertices,
    const std::vector<std::uint32_t>& indices,
    const GeometryState& state) const {
    m_vp.validate(state.m_viewport);
    if (state.m_cull_mode != CullMode::NONE && state.m_cull_mode != CullMode::FRONT &&
        state.m_cull_mode != CullMode::BACK) {
        throw std::invalid_argument("invalid cull mode");
    }
    std::vector<ScreenTriangle> result;
    for (const auto& triangle : m_pa.assemble(vertices, indices)) {
        for (const auto& clipped : m_clip.clip(triangle)) {
            ScreenTriangle screen;
            for (unsigned i = 0; i < 3; ++i) {
                screen[i] = m_vp.transform(clipped[i], state.m_viewport);
            }
            const auto& a = screen[0].m_position;
            const auto& b = screen[1].m_position;
            const auto& c = screen[2].m_position;
            const auto area = (double(b[0]) - a[0]) * (double(c[1]) - a[1]) -
                              (double(b[1]) - a[1]) * (double(c[0]) - a[0]);
            if (area == 0) {
                continue;
            }
            const bool front = state.m_front_counter_clockwise ? area < 0 : area > 0;
            if ((state.m_cull_mode == CullMode::FRONT && front) ||
                (state.m_cull_mode == CullMode::BACK && !front)) {
                continue;
            }
            result.push_back(screen);
        }
    }
    return result;
}
}
