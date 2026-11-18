#pragma once
#include "common/types.h"
#include <optional>

namespace helo::func {
// 保存朝向统一的三角形、像素包围盒及边函数面积。
struct TriangleSetup {
    ScreenTriangle m_vertices; // 保存正面积屏幕顶点。
    double m_area = 0; // 保存屏幕双倍面积。
    int m_min_x = 0; // 保存首个候选像素列。
    int m_max_x = -1; // 保存最后候选像素列。
    int m_min_y = 0; // 保存首个候选像素行。
    int m_max_y = -1; // 保存最后候选像素行。
};

// 建立裁剪至目标范围的光栅化参数。
class Setup {
public:
    std::optional<TriangleSetup> prepare(
        const ScreenTriangle& triangle, std::uint32_t width, std::uint32_t height) const;
    double edge(const ScreenVertex& a, const ScreenVertex& b, double x, double y) const;
    bool top_left(const ScreenVertex& a, const ScreenVertex& b) const;
};
}
