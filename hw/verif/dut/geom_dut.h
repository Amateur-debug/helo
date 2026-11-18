#pragma once
#include "helo/graphics.h"
#include <memory>
#include <vector>

namespace helo::verif {
// 规定Geom模块各实现共用的功能行为验证接口。
class GeomDut {
public:
    // 允许通过模块接口释放适配器。
    virtual ~GeomDut() = default;
    virtual std::vector<helo::Vertex> fetch(
        std::uint32_t address, std::uint32_t count, std::uint32_t stride = 40) = 0;
    virtual std::vector<std::uint32_t> fetch_indices(
        std::uint32_t address, std::uint32_t count) = 0;
    virtual void write_memory(std::uint32_t address, std::uint32_t value) = 0;
    virtual std::vector<helo::Triangle> assemble(const std::vector<helo::Vertex>& vertices,
        const std::vector<std::uint32_t>& indices = {}) = 0;
    virtual std::vector<helo::Triangle> clip(const helo::Triangle& triangle) = 0;
    virtual helo::ScreenVertex viewport(
        const helo::Vertex& vertex, const helo::Viewport& viewport) = 0;
    virtual std::vector<helo::ScreenTriangle> geometry(const std::vector<helo::Vertex>& vertices,
        const std::vector<std::uint32_t>& indices,
        const helo::GeometryState& state) = 0;
};

std::unique_ptr<GeomDut> make_geom_dut();
}
