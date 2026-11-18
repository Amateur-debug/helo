#pragma once
#include "helo/graphics.h"
#include <memory>
#include <vector>

namespace helo::verif {
// 规定Rast模块各实现共用的功能行为验证接口。
class RastDut {
public:
    // 允许通过模块接口释放适配器。
    virtual ~RastDut() = default;
    virtual std::vector<helo::Fragment> rasterize(
        const helo::ScreenTriangle& triangle, std::uint32_t width, std::uint32_t height) = 0;
    virtual helo::Fragment interpolate(
        const helo::ScreenTriangle& triangle, const std::array<double, 3>& weights) = 0;
};

std::unique_ptr<RastDut> make_rast_dut();
}
