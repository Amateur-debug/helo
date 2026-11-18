#pragma once
#include "common/types.h"

namespace helo::func {
// 保存进入定点插值器时量化的三个顶点属性。
struct FixedVertex {
    unsigned m_depth; // 保存无符号Q16窗口深度。
    unsigned m_inv_w; // 保存正Q16透视权重。
    std::array<unsigned, 4> m_color; // 保存各通道UNORM8值。
    std::array<std::int32_t, 2> m_uv; // 保存有符号Q16坐标。
};

ScreenTriangle snap_positions(ScreenTriangle triangle);
FixedVertex quantize_vertex(const ScreenVertex& vertex);
void interpolate_fixed(const std::array<FixedVertex, 3>& vertices,
    const std::array<std::uint64_t, 3>& edges,
    Fragment& fragment);
}
