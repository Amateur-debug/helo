#include "rast/setup.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace helo::func {
// 计算点位于有向边左侧时为正的边函数。
double Setup::edge(const ScreenVertex& a, const ScreenVertex& b, double x, double y) const {
    return (double(b.m_position[0]) - a.m_position[0]) * (y - a.m_position[1]) -
           (double(b.m_position[1]) - a.m_position[1]) * (x - a.m_position[0]);
}

// 对左上原点坐标识别包含端点的上边和左边。
bool Setup::top_left(const ScreenVertex& a, const ScreenVertex& b) const {
    const double dy = double(b.m_position[1]) - a.m_position[1];
    const double dx = double(b.m_position[0]) - a.m_position[0];
    return dy < 0 || (dy == 0 && dx > 0);
}

// 丢弃退化或不可见三角形，并将像素中心包围盒限制在目标内。
std::optional<TriangleSetup> Setup::prepare(const ScreenTriangle& triangle,
    std::uint32_t width,
    std::uint32_t height,
    unsigned samples) const {
    if (width == 0 || height == 0) {
        return std::nullopt;
    }
    if (width > std::uint32_t(std::numeric_limits<int>::max()) ||
        height > std::uint32_t(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("raster extent exceeds integer range");
    }
    TriangleSetup setup;
    setup.m_vertices = triangle;
    for (const auto& vertex : triangle) {
        for (const float value : vertex.m_position) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument("nonfinite screen position");
            }
        }
        if (!std::isfinite(vertex.m_inv_w) || vertex.m_inv_w <= 0) {
            throw std::invalid_argument("invalid reciprocal clip w");
        }
    }
    setup.m_area =
        edge(triangle[0], triangle[1], triangle[2].m_position[0], triangle[2].m_position[1]);
    if (setup.m_area == 0) {
        return std::nullopt;
    }
    if (setup.m_area < 0) {
        std::swap(setup.m_vertices[1], setup.m_vertices[2]);
        setup.m_area = -setup.m_area;
    }
    const double min_x = std::min({double(triangle[0].m_position[0]),
        double(triangle[1].m_position[0]),
        double(triangle[2].m_position[0])});
    const double max_x = std::max({double(triangle[0].m_position[0]),
        double(triangle[1].m_position[0]),
        double(triangle[2].m_position[0])});
    const double min_y = std::min({double(triangle[0].m_position[1]),
        double(triangle[1].m_position[1]),
        double(triangle[2].m_position[1])});
    const double max_y = std::max({double(triangle[0].m_position[1]),
        double(triangle[1].m_position[1]),
        double(triangle[2].m_position[1])});
    const double near = samples == 4 ? 0.25 : 0.5;
    const double far = 1 - near;
    if (max_x < near || max_y < near || min_x > double(width) - near ||
        min_y > double(height) - near) {
        return std::nullopt;
    }
    setup.m_min_x = int(std::max(0.0, std::ceil(min_x - far)));
    setup.m_max_x = int(std::min(double(width) - 1, std::floor(max_x - near)));
    setup.m_min_y = int(std::max(0.0, std::ceil(min_y - far)));
    setup.m_max_y = int(std::min(double(height) - 1, std::floor(max_y - near)));
    return setup;
}
}
