#pragma once
#include "command.h"
#include "gpu/stats.h"
#include "helo/execution.h"
#include "rop/rop.h"
#include "tex/tex.h"
#include <deque>
#include <string>

namespace helo::func {
class Geom;
class Rast;
class Shader;

// 从内存解析统一command buffer并直接调度GPU内部执行模块。
class Cp {
public:
    Cp(Mem& memory, Geom& geom, Rast& rast, Shader& shader, Tex& tex, Rop& rop, GpuStats& stats);
    void enqueue(std::uint32_t address, std::uint32_t byte_size);
    SubmitResult run(std::size_t max_commands = 100000);
    void reset();
    std::size_t pending() const;

private:
    // 跟踪一个已提交缓冲区的范围和下一待处理命令。
    struct Buffer {
        std::uint32_t m_address; // 保存缓冲区首字节地址。
        std::uint32_t m_byte_size; // 保存提交范围长度。
        std::uint32_t m_offset = 0; // 保存下一命令相对起点的字节偏移。
    };

    void execute_packet(std::uint32_t address);
    void execute_clear(const ClearCommand& command);
    void decode_raster_state(DrawCommand& command) const;
    void execute_render(const DrawCommand& command);
    std::size_t graphics_batch_size(const ShaderProgram& program) const;
    void shade_vertices(
        const DrawCommand& command, std::vector<Vertex>& vertices, const Tex::Bindings& textures);
    void process_fragments(const DrawCommand& command,
        const Rop::Target& target,
        const std::optional<Tex::Sampler>& sampler,
        const Tex::Bindings& textures,
        std::vector<Fragment>& fragments);
    void execute_compute(const ComputeCommand& command);
    void validate_program(const ShaderProgram& program, unsigned minimum_registers) const;
    void run_program(const ShaderProgram& program, const Tex::Bindings& textures);
    Mem& m_memory; // 引用命令包和全部资源所在的统一内存。
    Geom& m_geom; // 引用GPU持有的几何模块。
    Rast& m_rast; // 引用GPU持有的光栅模块。
    Shader& m_shader; // 引用GPU内部可编程执行模块。
    Tex& m_tex; // 引用GPU持有的纹理模块。
    Rop& m_rop; // 引用GPU持有的输出模块。
    GpuStats& m_stats; // 引用GPU持有的执行统计。
    std::deque<Buffer> m_queue; // 保存按顺序提交的缓冲区及解析游标。
    FaultInfo m_fault; // 锁存解析或执行故障及定位信息直到复位。
};
}
