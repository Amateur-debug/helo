#include "rast/fixed.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace helo::func {
namespace {
// 将有限输入按最接近、半值远离零舍入，并饱和到指定整数范围。
std::int64_t quantize(float value, unsigned scale, std::int64_t low, std::int64_t high) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("nonfinite fixed raster input");
    }
    return static_cast<std::int64_t>(
        std::clamp(std::round(double(value) * scale), double(low), double(high)));
}

// 当直接乘积超出64位时，按位累加商和余数，避免宽整数或宿主浮点舍入。
std::int64_t weighted(
    const std::array<std::uint64_t, 3>& weights, const std::array<std::int64_t, 3>& values) {
    const auto total = std::accumulate(weights.begin(), weights.end(), std::uint64_t{0});
    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    const auto span = static_cast<std::uint64_t>(*hi - *lo);
    if (span == 0) {
        return *lo;
    }
    std::uint64_t quotient = 0;
    std::uint64_t remainder = 0;
    if (total <= std::numeric_limits<std::uint64_t>::max() / span) {
        for (unsigned i = 0; i < 3; ++i) {
            remainder += weights[i] * static_cast<std::uint64_t>(values[i] - *lo);
        }
        quotient = remainder / total;
        remainder %= total;
    } else {
        for (int bit = 31; bit >= 0; --bit) {
            remainder *= 2;
            for (unsigned i = 0; i < 3; ++i) {
                if ((static_cast<std::uint64_t>(values[i] - *lo) >> bit) & 1U) {
                    remainder += weights[i];
                }
            }
            quotient = quotient * 2 + remainder / total;
            remainder %= total;
        }
    }
    const auto floor_value = *lo + static_cast<std::int64_t>(quotient);
    return floor_value + (2 * remainder > total || (2 * remainder == total && floor_value >= 0));
}
}

// 视口变换之后、覆盖和绕序标准化之前将坐标落到20位有符号Q4网格。
ScreenTriangle snap_positions(ScreenTriangle triangle) {
    for (auto& v : triangle) {
        for (unsigned c = 0; c < 2; ++c) {
            v.m_position[c] = float(quantize(v.m_position[c], 16, -524288, 524287)) / 16;
        }
    }
    return triangle;
}

// 按RTL绘制路径的端口位宽固定每个顶点的深度、倒数w、颜色与纹理坐标。
FixedVertex quantize_vertex(const ScreenVertex& vertex) {
    FixedVertex q{};
    q.m_depth = static_cast<unsigned>(quantize(vertex.m_position[2], 65536, 0, 65536));
    q.m_inv_w = static_cast<unsigned>(quantize(vertex.m_inv_w, 65536, 1, 65536));
    for (unsigned c = 0; c < 4; ++c) {
        q.m_color[c] = static_cast<unsigned>(quantize(vertex.m_color[c], 255, 0, 255));
    }
    for (unsigned c = 0; c < 2; ++c) {
        q.m_uv[c] =
            static_cast<std::int32_t>(quantize(vertex.m_uv[c], 65536, INT32_MIN, INT32_MAX));
    }
    return q;
}

// 对精确Q8边值作有理数加权，在最终除法处按目标精度舍入一次。
void interpolate_fixed(
    const std::array<FixedVertex, 3>& v, const std::array<std::uint64_t, 3>& edges, Fragment& f) {
    f.m_depth = float(weighted(edges, {v[0].m_depth, v[1].m_depth, v[2].m_depth})) / 65536;
    std::array<std::uint64_t, 3> weights{};
    for (unsigned i = 0; i < 3; ++i) {
        weights[i] = edges[i] * v[i].m_inv_w;
    }
    for (unsigned c = 0; c < 4; ++c) {
        f.m_color[c] =
            float(weighted(weights, {v[0].m_color[c], v[1].m_color[c], v[2].m_color[c]})) / 255;
    }
    for (unsigned c = 0; c < 2; ++c) {
        f.m_q16_uv[c] = static_cast<std::int32_t>(
            weighted(weights, {v[0].m_uv[c], v[1].m_uv[c], v[2].m_uv[c]}));
        f.m_uv[c] = float(f.m_q16_uv[c]) / 65536;
    }
}
}
