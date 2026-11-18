#include "rop/zrop.h"
#include <stdexcept>

namespace helo::func {
// 比较输入片元值或模板参考值与缓冲值。
bool Zrop::compare(CompareOp operation, float source, float destination) const {
    switch (operation) {
    case CompareOp::NEVER:
        return false;
    case CompareOp::LESS:
        return source < destination;
    case CompareOp::EQUAL:
        return source == destination;
    case CompareOp::LESS_EQUAL:
        return source <= destination;
    case CompareOp::GREATER:
        return source > destination;
    case CompareOp::NOT_EQUAL:
        return source != destination;
    case CompareOp::GREATER_EQUAL:
        return source >= destination;
    case CompareOp::ALWAYS:
        return true;
    }
    throw std::invalid_argument("invalid depth/stencil comparison");
}

// 应用模板操作并保留写掩码之外的原值。
std::uint8_t Zrop::stencil(
    StencilOp operation, std::uint8_t stored, std::uint8_t reference, std::uint8_t mask) const {
    std::uint8_t value = stored;
    switch (operation) {
    case StencilOp::KEEP:
        break;
    case StencilOp::ZERO:
        value = 0;
        break;
    case StencilOp::REPLACE:
        value = reference;
        break;
    case StencilOp::INCREMENT_CLAMP:
        value = stored == 255 ? 255 : std::uint8_t(stored + 1);
        break;
    case StencilOp::DECREMENT_CLAMP:
        value = stored == 0 ? 0 : std::uint8_t(stored - 1);
        break;
    case StencilOp::INVERT:
        value = std::uint8_t(~stored);
        break;
    case StencilOp::INCREMENT_WRAP:
        value = std::uint8_t(stored + 1);
        break;
    case StencilOp::DECREMENT_WRAP:
        value = std::uint8_t(stored - 1);
        break;
    default:
        throw std::invalid_argument("invalid stencil operation");
    }
    return std::uint8_t((stored & ~mask) | (value & mask));
}
}
