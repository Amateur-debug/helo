#pragma once
#include "cp/cp.h"
#include "geom/geom.h"
#include "rast/rast.h"
#include "rop/rop.h"
#include "shader/shader.h"
#include "tex/tex.h"

namespace helo::func {
// 连接统一内存、命令处理、可编程着色器与完整三角形渲染通路。
class Gpu {
public:
    explicit Gpu(const GpuConfig& config = {});
    // 禁止复制，防止子模块引用原GPU的内存或执行上下文。
    Gpu(const Gpu&) = delete;
    // 禁止复制赋值，保持子模块连接与所属GPU一致。
    Gpu& operator=(const Gpu&) = delete;
    // 禁止移动，保证已绑定的内部引用在整个生命周期内稳定。
    Gpu(Gpu&&) = delete;
    // 禁止移动赋值，避免连接指向被移动对象。
    Gpu& operator=(Gpu&&) = delete;
    Mem& memory();
    const Mem& memory() const;
    const GpuStats& stats() const;
    void submit(std::uint32_t address, std::uint32_t byte_size);
    SubmitResult run(std::size_t max_commands = 100000);
    void reset();
    std::size_t pending() const;
    std::vector<std::uint8_t> read_color(
        const FramebufferDesc& framebuffer, unsigned attachment = 0) const;

private:
    Mem m_memory; // 保存代码、顶点、纹理及所有附件的统一地址空间。
    Shader m_shader; // 复用同一SIMT核心执行顶点、片元和计算程序。
    Geom m_geom; // 持有几何模块及其顶点获取、装配、裁剪和视口单元。
    Rast m_rast; // 持有光栅模块及其设置和插值单元。
    Tex m_tex; // 持有连接统一内存的纹理采样模块。
    Rop m_rop; // 持有连接统一内存的深度模板与颜色输出模块。
    GpuStats m_stats; // 累计复位以来各阶段的实际执行量。
    Cp m_cp; // 持有解析command buffer并调度内部模块的命令处理器。
};
}
