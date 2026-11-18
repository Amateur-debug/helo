#include "shader.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace helo::func {
namespace {
// 按FP32位模式解释纹理坐标或显式LOD寄存器。
float texture_operand(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// 对活动lane使用各自的坐标与LOD，暂存目标通道供整组统一写回。
ExecutionResult sample_texture(const IssuedInstr& instruction, const Tex::Bindings& textures) {
    ExecutionResult result;
    result.m_decoded = instruction.m_decoded;
    const auto selector = instruction.m_decoded.m_instr.m_immediate;
    const auto slot = selector / 4;
    if (slot >= textures.size()) {
        throw std::out_of_range("unbound shader texture slot");
    }
    const auto& path = instruction.m_decoded.m_path;
    for (unsigned lane = 0; lane < WARP_SIZE; ++lane) {
        if (!(path.m_mask & (ThreadMask(1) << lane))) {
            continue;
        }
        const auto color = textures[slot].sample(texture_operand(instruction.m_src1[lane]),
            texture_operand(instruction.m_src2[lane]),
            texture_operand(instruction.m_src3[lane]));
        std::memcpy(&result.m_values[lane], &color[selector % 4], sizeof(float));
        result.m_next_pc[lane] = path.m_pc + 8;
    }
    return result;
}
}

// 按硬件warp、线程和GPR预算建立执行核心。
Shader::Shader(Mem& memory, const ShaderConfig& config)
    : m_execution(config.m_execution), m_max_threads(config.m_max_threads), m_icache(memory),
      m_dcache(memory), m_registers(config.m_max_warps, config.m_regfile_bytes),
      m_scoreboard(config.m_max_warps), m_buffer(config.m_max_warps), m_ifu(m_icache),
      m_lsu(m_dcache) {
    for (const auto count : {m_execution.m_iu_count,
             m_execution.m_fpu_count,
             m_execution.m_lsu_count,
             m_execution.m_wcu_count}) {
        if (!count || count > WARP_SIZE) {
            throw std::invalid_argument("execution unit count must be in 1..32");
        }
    }
    if (m_max_threads < WARP_SIZE) {
        throw std::invalid_argument("thread capacity must provide at least one warp");
    }
}

// 返回当前任务寄存器需求下的驻留warp容量。
std::size_t Shader::resident_warp_capacity() const {
    return m_wmu.resident_capacity(m_registers.warp_capacity(),
        m_max_threads,
        m_registers.capacity_bytes(),
        m_registers.register_count());
}

// 返回完整warp分配规则下可驻留的线程容量。
std::size_t Shader::resident_thread_capacity() const {
    return resident_warp_capacity() * WARP_SIZE;
}

// 按指定任务寄存器声明计算容量，不改变当前运行上下文。
std::size_t Shader::thread_capacity(unsigned register_count) const {
    return m_wmu.resident_capacity(m_registers.warp_capacity(),
               m_max_threads,
               m_registers.capacity_bytes(),
               register_count) *
           WARP_SIZE;
}

// 返回建立核心时指定的执行通路数量。
const ExecutionConfig& Shader::execution_config() const {
    return m_execution;
}

// 返回当前任务成功提交的执行批次统计。
const ExecutionStats& Shader::execution_stats() const {
    return m_stats;
}

// 清除任务状态及寄存器，保留外部内存内容。
void Shader::reset() {
    reset_control();
    m_registers.reset();
}

// 清理任务控制状态，寄存器由reset或configure中的一个入口负责初始化。
void Shader::reset_control() {
    m_warps.clear();
    m_scoreboard.reset();
    m_buffer.reset();
    m_wsu.reset();
    m_fault = {};
    m_range = {};
    m_shared = Mem(0);
    m_thread_count = 0;
    m_param_base = 0;
    m_stats = {};
}

// 验证新任务后替换旧上下文，输入寄存器可在启动后由调用者设置。
void Shader::launch(std::uint32_t entry,
    std::size_t thread_count,
    std::uint32_t param_base,
    unsigned register_count,
    const DispatchRange& range) {
    if (range.m_thread_base > range.m_total_threads ||
        (range.m_shared_bytes && !range.m_group_size) || range.m_shared_bytes > MAX_SHARED_BYTES ||
        range.m_shared_bytes % 4) {
        throw std::invalid_argument("invalid compute dispatch range or shared memory size");
    }
    const auto capacity = thread_capacity(register_count);
    if (range.m_group_size > capacity || (range.m_total_threads && !thread_count)) {
        throw std::invalid_argument("workgroup exceeds resident capacity or batch is empty");
    }
    const auto remaining = range.m_total_threads - range.m_thread_base;
    if (range.m_group_size) {
        if (range.m_thread_base % range.m_group_size ||
            thread_count != std::min(remaining, range.m_group_size)) {
            throw std::invalid_argument("compute batch must contain exactly one whole workgroup");
        }
    } else if (range.m_total_threads &&
               (range.m_thread_base % WARP_SIZE || thread_count > remaining ||
                   (thread_count < remaining && thread_count % WARP_SIZE))) {
        throw std::invalid_argument("independent compute batches must respect warp boundaries");
    }
    std::vector<Warp> warps;
    m_wmu.launch(warps, entry, thread_count, capacity / WARP_SIZE);
    if (thread_count) {
        m_icache.fetch(entry);
    }
    Mem shared(range.m_shared_bytes);
    m_registers.configure(warps.size(), register_count);
    reset_control();
    m_range = range;
    m_shared = std::move(shared);
    m_thread_count =
        range.m_total_threads ? range.m_total_threads : static_cast<std::uint32_t>(thread_count);
    m_param_base = param_base;
    m_warps = std::move(warps);
}

// 执行一个warp同PC线程组；错误锁存到复位或重新启动为止。
bool Shader::step(const Tex::Bindings& textures) {
    if (m_fault.m_code != FaultCode::NONE) {
        throw std::runtime_error(m_fault.m_message);
    }
    const auto path = m_wsu.select(m_warps);
    if (!path) {
        return false;
    }
    std::optional<IssuedInstr> issued;
    std::optional<std::uint64_t> encoding;
    try {
        if (!m_buffer.front(path->m_warp)) {
            encoding = m_ifu.fetch(*path);
            m_buffer.push({*path, m_idu.decode(*encoding)});
        }
        issued = m_isu.issue(path->m_warp, m_buffer, m_scoreboard, m_registers);
        if (!issued) {
            throw std::logic_error("synchronous shader dependency stall");
        }
        issued->m_range = m_range;
        issued->m_thread_count = m_thread_count;
        issued->m_param_base = m_param_base;
        m_wcu.validate(m_warps.at(path->m_warp), *issued);
        if (m_range.m_group_size) {
            m_wcu.validate_group(m_warps, *issued);
        } else if (issued->m_decoded.m_instr.m_operation == Operation::BARG) {
            throw std::runtime_error("BARG requires a workgroup");
        }
        std::uint64_t batches = 0;
        const auto result = execute_batches(*issued, batches, textures);
        m_wbu.writeback(result, m_registers, m_scoreboard);
        if (issued->m_decoded.m_instr.m_operation == Operation::BARG) {
            m_wcu.complete_group(m_warps, result);
        } else {
            m_wcu.complete(m_warps.at(path->m_warp), result);
        }
        switch (issued->m_decoded.m_instr.m_unit) {
        case ExecutionUnit::IU:
            m_stats.m_iu_batches += batches;
            break;
        case ExecutionUnit::FPU:
            m_stats.m_fpu_batches += batches;
            break;
        case ExecutionUnit::TEX:
        case ExecutionUnit::LSU:
            m_stats.m_lsu_batches += batches;
            break;
        case ExecutionUnit::WCU:
            m_stats.m_wcu_batches += batches;
            break;
        }
        return true;
    } catch (const std::exception& error) {
        if (issued) {
            m_scoreboard.release(issued->m_decoded);
        }
        m_buffer.reset();
        std::ostringstream diagnostic;
        diagnostic << error.what() << " warp=" << m_range.warp_base() + path->m_warp << " mask=0x"
                   << std::hex << path->m_mask << " pc=0x" << path->m_pc;
        if (encoding) {
            diagnostic << " instruction=0x" << *encoding;
        }
        m_fault.m_code = FaultCode::SHADER_EXECUTION;
        m_fault.m_message = diagnostic.str();
        m_fault.m_warp = m_range.warp_base() + path->m_warp;
        m_fault.m_mask = path->m_mask;
        m_fault.m_pc = path->m_pc;
        m_fault.m_instruction = encoding;
        throw std::runtime_error(m_fault.m_message);
    }
}

// 将原始线程组按连续lane范围分批执行，全部成功后才统一提交架构结果。
ExecutionResult Shader::execute_batches(
    const IssuedInstr& instruction, std::uint64_t& batches, const Tex::Bindings& textures) {
    const auto unit = instruction.m_decoded.m_instr.m_unit;
    unsigned count = 0;
    switch (unit) {
    case ExecutionUnit::IU:
        count = m_execution.m_iu_count;
        break;
    case ExecutionUnit::FPU:
        count = m_execution.m_fpu_count;
        break;
    case ExecutionUnit::TEX:
    case ExecutionUnit::LSU:
        count = m_execution.m_lsu_count;
        break;
    case ExecutionUnit::WCU:
        count = m_execution.m_wcu_count;
        break;
    }
    // 存储会直接修改内存，必须在第一批执行前验证原始线程组的全部地址。
    const auto op = instruction.m_decoded.m_instr.m_operation;
    const bool shared =
        op == Operation::LDS_W || op == Operation::STS_W || op == Operation::ATOM_ADD_SHARED;
    auto& lsu = shared ? m_shared_lsu : m_lsu;
    if (unit == ExecutionUnit::LSU) {
        lsu.validate(instruction);
    }
    ExecutionResult result;
    result.m_decoded = instruction.m_decoded;
    for (unsigned base = 0; base < WARP_SIZE; base += count) {
        ThreadMask mask = 0;
        for (unsigned lane = base; lane < WARP_SIZE && lane < base + count; ++lane) {
            mask |= instruction.m_decoded.m_path.m_mask & (ThreadMask(1) << lane);
        }
        if (!mask) {
            continue;
        }
        auto batch = instruction;
        batch.m_decoded.m_path.m_mask = mask;
        ExecutionResult partial;
        switch (unit) {
        case ExecutionUnit::IU:
            partial = m_iu.execute(batch);
            break;
        case ExecutionUnit::FPU:
            partial = m_fpu.execute(batch);
            break;
        case ExecutionUnit::TEX:
            partial = sample_texture(batch, textures);
            break;
        case ExecutionUnit::LSU:
            partial = lsu.execute(batch);
            break;
        case ExecutionUnit::WCU:
            partial = m_wcu.execute(batch);
            break;
        }
        for (unsigned lane = base; lane < WARP_SIZE && lane < base + count; ++lane) {
            if (!(mask & (ThreadMask(1) << lane))) {
                continue;
            }
            result.m_values[lane] = partial.m_values[lane];
            result.m_next_pc[lane] = partial.m_next_pc[lane];
            result.m_fcsr[lane] = partial.m_fcsr[lane];
        }
        result.m_exit_mask |= partial.m_exit_mask;
        result.m_writes_fcsr |= partial.m_writes_fcsr;
        ++batches;
    }
    return result;
}

// 在指令组预算内执行任务，预算耗尽可续跑，陷阱则返回稳定错误。
RunResult Shader::run(std::size_t max_steps, const Tex::Bindings& textures) {
    RunResult result;
    if (m_fault.m_code != FaultCode::NONE) {
        result.m_status = RunStatus::FAULT;
        result.m_fault = m_fault;
        return result;
    }
    try {
        while (result.m_steps < max_steps && !m_wmu.completed(m_warps)) {
            if (!step(textures)) {
                break;
            }
            ++result.m_steps;
        }
        result.m_status =
            m_wmu.completed(m_warps) ? RunStatus::COMPLETED : RunStatus::BUDGET_EXHAUSTED;
    } catch (const std::exception&) {
        result.m_status = RunStatus::FAULT;
        result.m_fault = m_fault;
    }
    return result;
}

// 提供初始化及检查线程寄存器的可写接口。
Regfile& Shader::registers() {
    return m_registers;
}

// 提供只读寄存器检查接口。
const Regfile& Shader::registers() const {
    return m_registers;
}

// 暴露只读warp完成状态及线程PC。
const std::vector<Warp>& Shader::warps() const {
    return m_warps;
}
}
