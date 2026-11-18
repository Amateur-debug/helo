#include "geom/geom.h"
#include "geom_dut.h"
#include "mem/mem.h"

namespace helo::verif {
// 将Geom模块共用测试转发至功能实现。
class FuncGeomDut final : public GeomDut {
public:
    // 从测试内存装载顶点。
    std::vector<helo::Vertex> fetch(
        std::uint32_t address, std::uint32_t count, std::uint32_t stride) override {
        return m_vfetch.fetch(address, count, stride);
    }

    // 从测试内存装载索引。
    std::vector<std::uint32_t> fetch_indices(std::uint32_t address, std::uint32_t count) override {
        return m_vfetch.fetch_indices(address, count);
    }

    // 更新测试内存中的一个字。
    void write_memory(std::uint32_t address, std::uint32_t value) override {
        m_memory.write(address, 4, value);
    }

    // 组装三角形列表。
    std::vector<helo::Triangle> assemble(const std::vector<helo::Vertex>& vertices,
        const std::vector<std::uint32_t>& indices) override {
        return m_pa.assemble(vertices, indices);
    }

    // 裁剪一个齐次三角形。
    std::vector<helo::Triangle> clip(const helo::Triangle& triangle) override {
        return m_clip.clip(triangle);
    }

    // 执行单个顶点的视口变换。
    helo::ScreenVertex viewport(const helo::Vertex& vertex, const helo::Viewport& vp) override {
        return m_vp.transform(vertex, vp);
    }

    // 执行完整几何处理。
    std::vector<helo::ScreenTriangle> geometry(const std::vector<helo::Vertex>& vertices,
        const std::vector<std::uint32_t>& indices,
        const helo::GeometryState& state) override {
        return m_geom.process(vertices, indices, state);
    }

private:
    func::Mem m_memory{65536}; // 保存顶点及索引测试数据。
    func::Vfetch m_vfetch{m_memory}; // 驱动顶点获取单元。
    func::Pa m_pa; // 驱动图元装配单元。
    func::Clip m_clip; // 驱动齐次裁剪单元。
    func::Vp m_vp; // 驱动视口变换单元。
    func::Geom m_geom{m_memory}; // 驱动完整几何模块。
};

// 为一次模块测试建立独立的功能适配器。
std::unique_ptr<GeomDut> make_geom_dut() {
    return std::make_unique<FuncGeomDut>();
}
}
