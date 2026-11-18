#pragma once
#include "common/types.h"

namespace helo::func {
// 按屏幕重心坐标插值深度和透视属性。
class Interp {
public:
    Fragment interpolate(const ScreenTriangle& triangle,
        const std::array<double, 3>& weights,
        std::uint32_t x,
        std::uint32_t y) const;
};
}
