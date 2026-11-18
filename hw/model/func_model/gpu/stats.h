#pragma once
#include <cstdint>

namespace helo::func {
// 汇总GPU图形和计算任务的功能执行计数。
struct GpuStats {
    std::uint64_t m_draw_calls = 0; // 记录成功完成的绘制命令数。
    std::uint64_t m_dispatches = 0; // 记录成功完成的计算命令数。
    std::uint64_t m_vertex_invocations = 0; // 记录处理的顶点调用数。
    std::uint64_t m_fragment_invocations = 0; // 记录进入片元阶段的样本数。
    std::uint64_t m_raster_triangles = 0; // 记录裁剪和剔除后交给光栅器的三角形数。
    std::uint64_t m_passed_fragments = 0; // 记录通过ROP测试的样本数。
    std::uint64_t m_shader_steps = 0; // 记录成功执行的Shader指令组数而非周期。
};
}
