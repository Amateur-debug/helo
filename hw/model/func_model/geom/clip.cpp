#include "clip.h"
#include <cmath>
#include <stdexcept>

namespace helo::func {
namespace {
// 返回齐次顶点到指定裁剪半空间的带符号距离。
double distance(const Vertex& vertex, unsigned plane) {
    const auto axis = plane / 2;
    return double(vertex.m_position[3]) +
           (plane % 2 ? -double(vertex.m_position[axis]) : double(vertex.m_position[axis]));
}

// 在齐次空间线性插值位置及全部顶点属性。
Vertex interpolate(const Vertex& a, const Vertex& b, double t) {
    Vertex result;
    for (unsigned i = 0; i < 4; ++i) {
        result.m_position[i] = static_cast<float>((1 - t) * a.m_position[i] + t * b.m_position[i]);
        result.m_color[i] = static_cast<float>((1 - t) * a.m_color[i] + t * b.m_color[i]);
    }
    for (unsigned i = 0; i < 2; ++i) {
        result.m_uv[i] = static_cast<float>((1 - t) * a.m_uv[i] + t * b.m_uv[i]);
    }
    for (unsigned i = 0; i < result.m_varyings.size(); ++i) {
        result.m_varyings[i] = float((1 - t) * a.m_varyings[i] + t * b.m_varyings[i]);
    }
    return result;
}
}

// 采用 Sutherland–Hodgman 算法裁剪六个平面并以扇形重新三角化。
std::vector<Triangle> Clip::clip(const Triangle& triangle) const {
    for (const auto& vertex : triangle) {
        for (const auto value : vertex.m_position) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument("non-finite vertex position");
            }
        }
        for (const auto value : vertex.m_color) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument("non-finite vertex color");
            }
        }
        for (const auto value : vertex.m_varyings) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument("non-finite vertex varying");
            }
        }
        for (const auto value : vertex.m_uv) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument("non-finite vertex UV");
            }
        }
    }
    std::vector<Vertex> polygon(triangle.begin(), triangle.end());
    for (unsigned plane = 0; plane < 6 && !polygon.empty(); ++plane) {
        std::vector<Vertex> output;
        auto previous = polygon.back();
        auto previous_distance = distance(previous, plane);
        for (const auto& current : polygon) {
            const auto current_distance = distance(current, plane);
            if ((previous_distance >= 0) != (current_distance >= 0)) {
                auto intersection = interpolate(
                    previous, current, previous_distance / (previous_distance - current_distance));
                // 精确锁定当前平面，避免浮点舍入将交点送回平面外。
                intersection.m_position[plane / 2] =
                    (plane % 2 ? 1 : -1) * intersection.m_position[3];
                output.push_back(intersection);
            }
            if (current_distance >= 0) {
                output.push_back(current);
            }
            previous = current;
            previous_distance = current_distance;
        }
        polygon = std::move(output);
    }
    std::vector<Triangle> result;
    for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
        // 齐次原点没有透视投影，包含它的退化扇形不生成屏幕图元。
        if (polygon[0].m_position[3] <= 0 || polygon[i].m_position[3] <= 0 ||
            polygon[i + 1].m_position[3] <= 0) {
            continue;
        }
        result.push_back({polygon[0], polygon[i], polygon[i + 1]});
    }
    return result;
}
}
