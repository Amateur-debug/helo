#pragma once
#include <cmath>
#include <stdexcept>
#include <string>

namespace helo::verif {
// 以可定位的说明报告独立期望值不匹配。
inline void graphics_check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 按数值契约同时限制绝对和相对误差，拒绝意外非有限结果。
inline void graphics_near(float actual,
    float expected,
    const std::string& message,
    float absolute_tolerance = 1e-5f,
    float relative_tolerance = 1e-5f) {
    const double error = std::abs(double(actual) - expected);
    graphics_check(
        std::isfinite(actual) && std::isfinite(expected) &&
            error <= absolute_tolerance + relative_tolerance * std::abs(double(expected)),
        message);
}

// 检查非法输入确实被实现拒绝。
template <class Operation> void graphics_reject(Operation operation, const std::string& message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    graphics_check(rejected, message);
}
}
