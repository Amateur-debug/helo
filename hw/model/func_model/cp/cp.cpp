#include "cp.h"
#include "geom/geom.h"
#include "helo/graphics_abi.h"
#include "rast/rast.h"
#include "shader/shader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace helo::func {
namespace {
// 将Shader结构化故障传递给CP，同时保持包内预算耗尽为不可续跑的故障。
class ShaderFailure : public std::runtime_error {
public:
    // 复制故障上下文，避免仅通过诊断文本传递机器状态。
    explicit ShaderFailure(const FaultInfo& fault)
        : std::runtime_error(fault.m_message), m_fault(fault) {
    }

    FaultInfo m_fault; // 保存原始Shader故障和定位字段。
};

// 统一图形与计算命令对Shader运行结果的处理方式。
void require_shader_completion(const RunResult& result) {
    if (result.m_status == RunStatus::FAULT) {
        throw ShaderFailure(result.m_fault);
    }
    if (result.m_status == RunStatus::BUDGET_EXHAUSTED) {
        FaultInfo fault;
        fault.m_code = FaultCode::SHADER_BUDGET;
        fault.m_message = "shader instruction budget exhausted inside command";
        throw ShaderFailure(fault);
    }
}

// 在无副作用的包快照上逐字段解码并严格检查编码范围。
class PacketReader {
public:
    // 完整读取当前包，后续执行不能改变已经解码的状态。
    PacketReader(const Mem& memory, std::uint32_t address) {
        for (unsigned i = 0; i < m_words.size(); ++i) {
            m_words[i] = memory.read(address + i * 4, 4);
        }
        if (m_words[0] != COMMAND_MAGIC || m_words[2] != COMMAND_BYTES || m_words[3] != 0) {
            throw std::invalid_argument("invalid command header/version/length/reserved word");
        }
    }

    // 返回头部操作码供CP选择内部执行路径。
    std::uint32_t opcode() const {
        return m_words[1];
    }

    // 消费一个32位小端字段并检查包界限。
    std::uint32_t word() {
        if (m_cursor == m_words.size()) {
            throw std::out_of_range("command payload exhausted");
        }
        return m_words[m_cursor++];
    }

    // 验证布尔字段只使用规范的零或一编码。
    bool boolean() {
        const auto value = word();
        if (value > 1) {
            throw std::invalid_argument("invalid command boolean");
        }
        return value != 0;
    }

    // 验证八位字段没有被截断的高位。
    std::uint8_t byte() {
        const auto value = word();
        if (value > 255) {
            throw std::invalid_argument("invalid command byte");
        }
        return static_cast<std::uint8_t>(value);
    }

    // 验证枚举编码后转换为所属模块的状态类型。
    template <class Enum> Enum enumeration(unsigned maximum) {
        const auto value = word();
        if (value > maximum) {
            throw std::invalid_argument("invalid command enum");
        }
        return static_cast<Enum>(value);
    }

    // 保留位模式解码一个IEEE binary32字段。
    float real() {
        const auto value = word();
        float result;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    // 解码附件布局，附件标志分别使用独立布尔字。
    FramebufferDesc framebuffer() {
        FramebufferDesc f;
        f.m_color_address = word();
        f.m_depth_address = word();
        f.m_stencil_address = word();
        f.m_width = word();
        f.m_height = word();
        f.m_has_depth = boolean();
        f.m_has_stencil = boolean();
        return f;
    }

    // 解码一个Shader阶段的入口、资源声明和指令预算。
    ShaderProgram program() {
        ShaderProgram p;
        p.m_enabled = boolean();
        p.m_entry = word();
        p.m_param_base = word();
        p.m_register_count = word();
        p.m_step_budget = word();
        return p;
    }

    // 拒绝未使用负载中的非零保留位。
    void finish() {
        while (m_cursor < m_words.size()) {
            if (word() != 0) {
                throw std::invalid_argument("nonzero command padding");
            }
        }
    }

private:
    std::array<std::uint32_t, COMMAND_BYTES / 4> m_words{}; // 保存当前包的完整快照。
    std::size_t m_cursor = 4; // 指向固定四字头之后的下一负载字。
};
}

// 将CP连接到GPU已构造的全部执行模块，CP不反向持有GPU对象。
Cp::Cp(Mem& memory, Geom& geom, Rast& rast, Shader& shader, Tex& tex, Rop& rop, GpuStats& stats)
    : m_memory(memory), m_geom(geom), m_rast(rast), m_shader(shader), m_tex(tex), m_rop(rop),
      m_stats(stats) {
}

// 验证提交范围后保存缓冲区描述，包内容在执行时读取。
void Cp::enqueue(std::uint32_t address, std::uint32_t byte_size) {
    if (address % 4 || byte_size == 0 || byte_size % COMMAND_BYTES ||
        std::uint64_t(address) + byte_size > 0x100000000ULL) {
        throw std::invalid_argument("invalid command buffer range");
    }
    m_memory.validate(address, 4);
    m_memory.validate(address + byte_size - 4, 4);
    m_queue.push_back({address, byte_size, 0});
}

// 按命令包预算推进缓冲区游标，并锁存首个解析或执行错误。
SubmitResult Cp::run(std::size_t max_commands) {
    SubmitResult result;
    if (m_fault.m_code != FaultCode::NONE) {
        result.m_status = RunStatus::FAULT;
        result.m_fault = m_fault;
        return result;
    }
    while (!m_queue.empty() && result.m_commands < max_commands) {
        auto& buffer = m_queue.front();
        const auto address = buffer.m_address + buffer.m_offset;
        try {
            execute_packet(address);
            buffer.m_offset += COMMAND_BYTES;
            ++result.m_commands;
            if (buffer.m_offset == buffer.m_byte_size) {
                m_queue.pop_front();
            }
        } catch (const ShaderFailure& error) {
            m_fault = error.m_fault;
        } catch (const std::exception& error) {
            m_fault.m_code = FaultCode::COMMAND;
            m_fault.m_message = error.what();
        }
        if (m_fault.m_code != FaultCode::NONE) {
            m_fault.m_command_address = address;
            m_fault.m_message =
                "CP command at byte " + std::to_string(address) + ": " + m_fault.m_message;
            result.m_status = RunStatus::FAULT;
            result.m_fault = m_fault;
            return result;
        }
    }
    result.m_status = m_queue.empty() ? RunStatus::COMPLETED : RunStatus::BUDGET_EXHAUSTED;
    return result;
}

// 丢弃待执行缓冲区和游标并解除错误，不修改资源内存。
void Cp::reset() {
    m_queue.clear();
    m_fault = {};
}

// 返回尚未完成的缓冲区数量，包含失败缓冲区。
std::size_t Cp::pending() const {
    return m_queue.size();
}

// 解析完整包后选择内部模块调度路径，禁止未验证字段产生副作用。
void Cp::execute_packet(std::uint32_t address) {
    PacketReader reader(m_memory, address);
    switch (static_cast<CommandOpcode>(reader.opcode())) {
    case CommandOpcode::CLEAR: {
        ClearCommand c;
        c.m_framebuffer = reader.framebuffer();
        for (auto& value : c.m_color) {
            value = reader.real();
        }
        c.m_depth = reader.real();
        c.m_stencil = reader.byte();
        reader.finish();
        execute_clear(c);
        return;
    }
    case CommandOpcode::COMPUTE: {
        ComputeCommand c;
        c.m_entry = reader.word();
        c.m_thread_count = reader.word();
        c.m_param_base = reader.word();
        c.m_register_count = reader.word();
        c.m_step_budget = reader.word();
        reader.finish();
        execute_compute(c);
        return;
    }
    case CommandOpcode::RENDER: {
        DrawCommand c;
        c.m_vertex_address = reader.word();
        c.m_vertex_count = reader.word();
        c.m_vertex_stride = reader.word();
        c.m_indexed = reader.boolean();
        c.m_index_address = reader.word();
        c.m_index_count = reader.word();
        auto& v = c.m_geometry.m_viewport;
        v.m_x = reader.real();
        v.m_y = reader.real();
        v.m_width = reader.real();
        v.m_height = reader.real();
        v.m_min_depth = reader.real();
        v.m_max_depth = reader.real();
        c.m_geometry.m_cull_mode = reader.enumeration<CullMode>(2);
        c.m_geometry.m_front_counter_clockwise = reader.boolean();
        c.m_framebuffer = reader.framebuffer();
        auto& d = c.m_rop.m_depth_stencil;
        d.m_depth_test = reader.boolean();
        d.m_depth_write = reader.boolean();
        d.m_depth_compare = reader.enumeration<CompareOp>(7);
        d.m_stencil_test = reader.boolean();
        d.m_stencil_compare = reader.enumeration<CompareOp>(7);
        d.m_stencil_reference = reader.byte();
        d.m_stencil_read_mask = reader.byte();
        d.m_stencil_write_mask = reader.byte();
        d.m_stencil_fail = reader.enumeration<StencilOp>(7);
        d.m_depth_fail = reader.enumeration<StencilOp>(7);
        d.m_pass = reader.enumeration<StencilOp>(7);
        c.m_rop.m_blend = reader.boolean();
        c.m_rop.m_color_write = reader.boolean();
        c.m_vertex_shader = reader.program();
        c.m_fragment_shader = reader.program();
        c.m_texture_enabled = reader.boolean();
        c.m_texture.m_address = reader.word();
        c.m_texture.m_width = reader.word();
        c.m_texture.m_height = reader.word();
        c.m_texture.m_filter = reader.enumeration<TextureFilter>(1);
        c.m_texture.m_wrap_u = reader.enumeration<TextureWrap>(1);
        c.m_texture.m_wrap_v = reader.enumeration<TextureWrap>(1);
        reader.finish();
        execute_render(c);
        return;
    }
    }
    throw std::invalid_argument("unknown command opcode");
}

namespace {
constexpr std::size_t GRAPHICS_BATCH_SIZE = 128;

// 保留FP32位模式以复用Shader的统一整数浮点寄存器。
std::uint32_t float_bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// 将统一寄存器中的FP32位模式恢复为图形属性。
float bits_float(std::uint32_t value) {
    float result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

// 通过公开寄存器初始化接口写入图形阶段的一个浮点输入。
void write_float(Shader& shader, std::size_t lane, unsigned reg, float value) {
    shader.registers().write_scalar(lane / WARP_SIZE, lane % WARP_SIZE, reg, float_bits(value));
}

// 从已结束的Shader调用中读取一个浮点输出。
float read_float(const Shader& shader, std::size_t lane, unsigned reg) {
    return bits_float(shader.registers().read_gpr(lane / WARP_SIZE, lane % WARP_SIZE, reg));
}
}

// 通过ROP统一验证并清除目标附件。
void Cp::execute_clear(const ClearCommand& command) {
    m_rop.clear(command.m_framebuffer, command.m_color, command.m_depth, command.m_stencil);
}

// 在执行图形阶段之前检查寄存器ABI、程序入口和运行预算。
void Cp::validate_program(const ShaderProgram& program, unsigned minimum_registers) const {
    if (!program.m_enabled) {
        return;
    }
    if (program.m_register_count < minimum_registers || program.m_register_count > REGISTER_COUNT) {
        throw std::invalid_argument("graphics program register declaration violates stage ABI");
    }
    if (program.m_step_budget == 0 || program.m_entry % 8 != 0 || program.m_entry > 0xfffffff8U) {
        throw std::invalid_argument("invalid graphics program entry or budget");
    }
    m_memory.validate(program.m_entry, 4);
    m_memory.validate(program.m_entry + 4, 4);
}

// 执行当前图形调用批次并拒绝错误或预算耗尽的输出。
void Cp::run_program(const ShaderProgram& program) {
    const auto result = m_shader.run(program.m_step_budget);
    m_stats.m_shader_steps += result.m_steps;
    require_shader_completion(result);
}

// 验证资源后按固定顺序调度各阶段，片元临时存储最多容纳一个批次。
void Cp::execute_render(const DrawCommand& command) {
    const auto target = m_rop.prepare(command.m_framebuffer);
    std::optional<Tex::Sampler> sampler;
    if (command.m_texture_enabled) {
        sampler.emplace(m_tex.prepare(command.m_texture));
    }
    validate_program(command.m_vertex_shader, graphics_abi::VS_REGISTER_COUNT);
    validate_program(command.m_fragment_shader, graphics_abi::FS_REGISTER_COUNT);
    auto vertices =
        m_geom.fetch(command.m_vertex_address, command.m_vertex_count, command.m_vertex_stride);
    const auto indices = command.m_indexed
                             ? m_geom.fetch_indices(command.m_index_address, command.m_index_count)
                             : std::vector<std::uint32_t>{};
    // 显式空索引绘制不启动着色器，也不退化为非索引绘制。
    if (command.m_indexed && indices.empty()) {
        ++m_stats.m_draw_calls;
        return;
    }
    m_geom.validate_assembly(vertices.size(), indices);
    shade_vertices(command, vertices);
    m_stats.m_vertex_invocations += vertices.size();
    const auto triangles = m_geom.process(vertices, indices, command.m_geometry);
    m_stats.m_raster_triangles += triangles.size();
    std::vector<Fragment> fragments;
    const auto fragment_capacity = command.m_fragment_shader.m_enabled && !triangles.empty()
                                       ? graphics_batch_size(command.m_fragment_shader)
                                       : GRAPHICS_BATCH_SIZE;
    fragments.reserve(fragment_capacity);
    for (const auto& triangle : triangles) {
        auto cursor =
            m_rast.begin(triangle, command.m_framebuffer.m_width, command.m_framebuffer.m_height);
        while (m_rast.next(cursor, fragments, fragment_capacity)) {
            process_fragments(command, target, sampler, fragments);
        }
    }
    ++m_stats.m_draw_calls;
}

// 以调用上限和实际驻留资源共同限制图形批次，拒绝无法接纳任何warp的配置。
std::size_t Cp::graphics_batch_size(const ShaderProgram& program) const {
    const auto capacity =
        std::min(GRAPHICS_BATCH_SIZE, m_shader.thread_capacity(program.m_register_count));
    if (!capacity) {
        throw std::invalid_argument("graphics program exceeds shader resources");
    }
    return capacity;
}

// 批量装载及读回顶点ABI，全部顶点完成后才进入几何处理。
void Cp::shade_vertices(const DrawCommand& command, std::vector<Vertex>& vertices) {
    if (!command.m_vertex_shader.m_enabled) {
        return;
    }
    if (vertices.empty()) {
        return;
    }
    const auto capacity = graphics_batch_size(command.m_vertex_shader);
    for (std::size_t first = 0; first < vertices.size(); first += capacity) {
        const auto count = std::min(capacity, vertices.size() - first);
        const auto& program = command.m_vertex_shader;
        m_shader.launch(program.m_entry, count, program.m_param_base, program.m_register_count);
        for (std::size_t lane = 0; lane < count; ++lane) {
            const auto& vertex = vertices[first + lane];
            for (unsigned component = 0; component < 4; ++component) {
                write_float(m_shader,
                    lane,
                    graphics_abi::VS_POSITION + component,
                    vertex.m_position[component]);
                write_float(
                    m_shader, lane, graphics_abi::VS_COLOR + component, vertex.m_color[component]);
            }
            for (unsigned component = 0; component < 2; ++component) {
                write_float(
                    m_shader, lane, graphics_abi::VS_UV + component, vertex.m_uv[component]);
            }
            m_shader.registers().write_scalar(lane / WARP_SIZE,
                lane % WARP_SIZE,
                graphics_abi::VS_VERTEX_ID,
                static_cast<std::uint32_t>(first + lane));
        }
        run_program(program);
        for (std::size_t lane = 0; lane < count; ++lane) {
            auto& vertex = vertices[first + lane];
            for (unsigned component = 0; component < 4; ++component) {
                vertex.m_position[component] =
                    read_float(m_shader, lane, graphics_abi::VS_POSITION + component);
                vertex.m_color[component] =
                    read_float(m_shader, lane, graphics_abi::VS_COLOR + component);
            }
            for (unsigned component = 0; component < 2; ++component) {
                vertex.m_uv[component] =
                    read_float(m_shader, lane, graphics_abi::VS_UV + component);
            }
        }
    }
}

// 先准备整个批次的采样输入，Shader成功后按原lane顺序提交ROP。
void Cp::process_fragments(const DrawCommand& command,
    const Rop::Target& target,
    const std::optional<Tex::Sampler>& sampler,
    std::vector<Fragment>& fragments) {
    const auto count = fragments.size();
    const auto& program = command.m_fragment_shader;
    if (program.m_enabled) {
        m_shader.launch(program.m_entry, count, program.m_param_base, program.m_register_count);
    }
    for (std::size_t lane = 0; lane < count; ++lane) {
        auto& fragment = fragments[lane];
        const Color sampled =
            sampler ? sampler->sample(fragment.m_uv[0], fragment.m_uv[1]) : Color{1, 1, 1, 1};
        if (program.m_enabled) {
            for (unsigned component = 0; component < 4; ++component) {
                write_float(m_shader,
                    lane,
                    graphics_abi::FS_COLOR + component,
                    fragment.m_color[component]);
                write_float(
                    m_shader, lane, graphics_abi::FS_TEXTURE + component, sampled[component]);
            }
            write_float(m_shader, lane, graphics_abi::FS_UV, fragment.m_uv[0]);
            write_float(m_shader, lane, graphics_abi::FS_UV + 1, fragment.m_uv[1]);
            write_float(
                m_shader, lane, graphics_abi::FS_PIXEL_X, static_cast<float>(fragment.m_x) + 0.5F);
            write_float(
                m_shader, lane, graphics_abi::FS_PIXEL_Y, static_cast<float>(fragment.m_y) + 0.5F);
            write_float(m_shader, lane, graphics_abi::FS_DEPTH, fragment.m_depth);
            m_shader.registers().write_scalar(lane / WARP_SIZE,
                lane % WARP_SIZE,
                graphics_abi::FS_FRAGMENT_ID,
                static_cast<std::uint32_t>(m_stats.m_fragment_invocations + lane));
        } else {
            for (unsigned component = 0; component < 4; ++component) {
                fragment.m_color[component] *= sampled[component];
            }
        }
    }
    if (program.m_enabled) {
        run_program(program);
    }
    m_stats.m_fragment_invocations += count;
    for (std::size_t lane = 0; lane < count; ++lane) {
        auto& fragment = fragments[lane];
        if (program.m_enabled) {
            if (m_shader.registers().read_gpr(
                    lane / WARP_SIZE, lane % WARP_SIZE, graphics_abi::FS_DISCARD) != 0) {
                continue;
            }
            for (unsigned component = 0; component < 4; ++component) {
                fragment.m_color[component] =
                    read_float(m_shader, lane, graphics_abi::FS_COLOR + component);
            }
            fragment.m_depth = read_float(m_shader, lane, graphics_abi::FS_DEPTH);
        }
        if (target.write(command.m_rop, fragment)) {
            ++m_stats.m_passed_fragments;
        }
    }
}

// 将计算任务交给现有SIMT执行链路并累计成功步骤。
void Cp::execute_compute(const ComputeCommand& command) {
    if (command.m_step_budget == 0) {
        throw std::invalid_argument("compute instruction budget must be positive");
    }
    m_shader.launch(
        command.m_entry, command.m_thread_count, command.m_param_base, command.m_register_count);
    const auto result = m_shader.run(command.m_step_budget);
    m_stats.m_shader_steps += result.m_steps;
    require_shader_completion(result);
    ++m_stats.m_dispatches;
}

}
