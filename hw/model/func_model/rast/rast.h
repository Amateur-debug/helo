#pragma once
#include "fixed.h"
#include "interp.h"
#include "setup.h"
#include <vector>

namespace helo::func {
// 对单个三角形生成像素中心采样片元。
class Rast {
public:
    // 持有一个三角形的覆盖遍历进度，不保存已生成的片元。
    class Cursor {
    public:
        // 创建已经耗尽的空游标，实际遍历由begin建立。
        Cursor() = default;

    private:
        friend class Rast;
        std::optional<TriangleSetup> m_setup; // 保存不可由调用者改写的三角形设置。
        std::optional<std::array<FixedVertex, 3>> m_fixed; // 保存可选的定点插值输入。
        unsigned m_samples = 1; // 保存像素内样本总数。
        unsigned m_sample = 0; // 保存下一候选样本编号。
        int m_x = 0; // 指向下一候选像素列。
        int m_y = 0; // 指向下一候选像素行。
    };

    static std::array<float, 2> sample_offset(unsigned samples, unsigned sample);
    Cursor begin(const ScreenTriangle& triangle,
        std::uint32_t width,
        std::uint32_t height,
        unsigned samples = 1,
        const Scissor& scissor = {},
        RasterPrecision precision = RasterPrecision::FLOAT) const;
    bool next(Cursor& cursor, std::vector<Fragment>& fragments, std::size_t capacity) const;
    std::vector<Fragment> rasterize(const ScreenTriangle& triangle,
        std::uint32_t width,
        std::uint32_t height,
        RasterPrecision precision = RasterPrecision::FLOAT) const;

private:
    Setup m_setup; // 持有三角形设置与覆盖判定单元。
    Interp m_interp; // 持有深度及透视属性插值单元。
};
}
