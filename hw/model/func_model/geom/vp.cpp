#include "vp.h"
#include <cmath>
#include <stdexcept>

namespace helo::func {
// 校验有限正视口尺寸以及处于零到一范围内的有序深度区间。
void Vp::validate(const Viewport& viewport) const {
    if (!std::isfinite(viewport.m_x) || !std::isfinite(viewport.m_y) ||
        !std::isfinite(viewport.m_width) || !std::isfinite(viewport.m_height) ||
        viewport.m_width <= 0 || viewport.m_height <= 0 || !std::isfinite(viewport.m_min_depth) ||
        !std::isfinite(viewport.m_max_depth) || viewport.m_min_depth < 0 ||
        viewport.m_max_depth > 1 || viewport.m_min_depth > viewport.m_max_depth) {
        throw std::invalid_argument("invalid viewport");
    }
}

// 执行透视除法，并将 NDC 的向上 Y 轴转换为屏幕向下 Y 轴。
ScreenVertex Vp::transform(const Vertex& vertex, const Viewport& viewport) const {
    validate(viewport);
    for (const auto value : vertex.m_position) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("non-finite viewport input");
        }
    }
    if (vertex.m_position[3] <= 0) {
        throw std::invalid_argument("viewport requires positive W");
    }
    const auto inv_w = 1.0 / vertex.m_position[3];
    ScreenVertex result;
    result.m_inv_w = static_cast<float>(inv_w);
    result.m_position[0] = static_cast<float>(
        viewport.m_x + (vertex.m_position[0] * inv_w + 1) * 0.5 * viewport.m_width);
    result.m_position[1] = static_cast<float>(
        viewport.m_y + (1 - vertex.m_position[1] * inv_w) * 0.5 * viewport.m_height);
    result.m_position[2] = static_cast<float>(
        viewport.m_min_depth +
        (vertex.m_position[2] * inv_w + 1) * 0.5 * (viewport.m_max_depth - viewport.m_min_depth));
    for (const auto value : result.m_position) {
        if (!std::isfinite(value)) {
            throw std::overflow_error("viewport coordinate overflow");
        }
    }
    if (!std::isfinite(result.m_inv_w)) {
        throw std::overflow_error("reciprocal W overflow");
    }
    result.m_color = vertex.m_color;
    result.m_uv = vertex.m_uv;
    return result;
}
}
