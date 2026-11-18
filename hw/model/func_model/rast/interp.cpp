#include "rast/interp.h"
#include <cmath>
#include <stdexcept>

namespace helo::func {
// 深度采用屏幕线性插值，颜色与纹理坐标采用倒数w校正。
Fragment Interp::interpolate(const ScreenTriangle& triangle,
    const std::array<double, 3>& weights,
    std::uint32_t x,
    std::uint32_t y) const {
    Fragment result;
    result.m_x = x;
    result.m_y = y;
    double denominator = 0;
    double depth = 0;
    std::array<double, 4> color{};
    std::array<double, 2> uv{};
    std::array<double, MAX_VARYING_COMPONENTS> varyings{};
    for (unsigned i = 0; i < 3; ++i) {
        const double factor = weights[i] * triangle[i].m_inv_w;
        denominator += factor;
        depth += weights[i] * triangle[i].m_position[2];
        for (unsigned c = 0; c < 4; ++c) {
            color[c] += factor * triangle[i].m_color[c];
        }
        for (unsigned c = 0; c < varyings.size(); ++c) {
            varyings[c] += factor * triangle[i].m_varyings[c];
        }
        for (unsigned c = 0; c < 2; ++c) {
            uv[c] += factor * triangle[i].m_uv[c];
        }
    }
    if (!std::isfinite(denominator) || denominator <= 0) {
        throw std::invalid_argument("invalid perspective denominator");
    }
    result.m_depth = float(depth);
    for (unsigned c = 0; c < 4; ++c) {
        result.m_color[c] = float(color[c] / denominator);
    }
    for (unsigned c = 0; c < 2; ++c) {
        result.m_uv[c] = float(uv[c] / denominator);
    }
    for (unsigned c = 0; c < varyings.size(); ++c) {
        result.m_varyings[c] = float(varyings[c] / denominator);
    }
    return result;
}
}
