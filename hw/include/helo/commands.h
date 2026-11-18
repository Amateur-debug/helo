#pragma once
#include "helo/graphics.h"
#include <cstddef>

namespace helo {
// 描述驻留统一内存的可编程着色器及其执行预算。
struct ShaderProgram {
    bool m_enabled = false; // 为真时执行机器码，否则使用阶段直通行为。
    std::uint32_t m_entry = 0; // 保存八字节对齐的程序入口。
    std::uint32_t m_param_base = 0; // 通过参数CSR传递应用参数地址。
    unsigned m_register_count = 32; // 声明含r0的逻辑寄存器数量。
    std::size_t m_step_budget = 100000; // 限制每批着色器的指令组执行次数。
};

// 保存CP从RENDER包解码的三角形任务状态。
struct DrawCommand {
    std::uint32_t m_vertex_address = 0; // 指向按当前布局编码的顶点流。
    std::uint32_t m_vertex_count = 0; // 指定顶点流中的顶点数量。
    std::uint32_t m_vertex_stride = 40; // 指定顶点记录字节跨度。
    bool m_indexed = false; // 选择32位索引三角形列表或顺序顶点列表。
    std::uint32_t m_index_address = 0; // 指向小端32位索引流。
    std::uint32_t m_index_count = 0; // 指定索引条目数量。
    GeometryState m_geometry; // 保存视口及面剔除状态。
    FramebufferDesc m_framebuffer; // 指定颜色、深度和模板附件。
    RopState m_rop; // 保存深度、模板、混合和写入状态。
    ShaderProgram m_vertex_shader; // 配置顶点变换及属性着色程序。
    ShaderProgram m_fragment_shader; // 配置插值和纹理输入的片元着色程序。
    bool m_texture_enabled = false; // 启用片元输入的固定功能纹理采样。
    TextureDesc m_texture; // 指定RGBA8纹理和过滤寻址方式。
    std::uint32_t m_vertex_layout_address = 0; // 指向顶点属性偏移表，零使用默认布局。
    unsigned m_varying_count = 0; // 指定本次绘制传递的自定义FP32分量数。
    std::uint32_t m_texture_table_address = 0; // 指向图形Shader可访问的纹理描述符表。
    std::uint32_t m_texture_binding_count = 0; // 指定从槽零起连续绑定的纹理数。
    std::uint32_t m_raster_state_address = 0; // 指定可选的裁切和混合描述符。
    RasterPrecision m_precision = RasterPrecision::FLOAT; // 指定光栅数值配置。
    Scissor m_scissor{}; // 保存CP从描述符解码的矩形。
};

// 保存CP从CLEAR包解码的附件清除状态。
struct ClearCommand {
    FramebufferDesc m_framebuffer; // 指定待清除的附件。
    Color m_color{0, 0, 0, 1}; // 指定线性RGBA清除值。
    float m_depth = 1; // 指定深度清除值。
    std::uint8_t m_stencil = 0; // 指定模板清除值。
};

// 保存CP从COMPUTE包解码的程序启动状态。
struct ComputeCommand {
    std::uint32_t m_entry = 0; // 保存计算程序入口。
    std::size_t m_thread_count = 0; // 指定整包的逻辑线程总数。
    std::uint32_t m_param_base = 0; // 保存程序参数地址。
    unsigned m_register_count = 32; // 声明任务寄存器数量。
    std::size_t m_step_budget = 100000; // 限制整包所有批次合计的指令组数。
    std::uint32_t m_group_size = 0; // 指定一维工作组线程数；零表示独立线程任务。
    std::uint32_t m_shared_bytes = 0; // 指定每个工作组的独立共享存储字节数。
};

// 定义内存command buffer的固定包格式，所有字均按小端存储。
constexpr std::uint32_t COMMAND_MAGIC = 0x48434d01U;
constexpr std::uint32_t COMMAND_BYTES = 256;
enum class CommandOpcode : std::uint32_t { CLEAR = 1, RENDER = 2, COMPUTE = 3 };
}
