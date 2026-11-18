#pragma once
#include "helo/graphics.h"

namespace helo::func {
// 计算深度比较与模板更新的纯函数结果。
class Zrop {
public:
    bool compare(CompareOp operation, float source, float destination) const;
    std::uint8_t stencil(
        StencilOp operation, std::uint8_t stored, std::uint8_t reference, std::uint8_t mask) const;
};
}
