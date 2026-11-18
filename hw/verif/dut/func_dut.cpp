#include "dut.h"
#include "shader/shader.h"
#include <stdexcept>

namespace helo::verif {
namespace {
// 将统一 DUT 接口适配到功能模型顶层与独立验证单元。
class FuncDut final : public Dut {
public:
    // 建立顶层及独立流水单元的适配实例。
    explicit FuncDut(const DutConfig& config)
        : m_memory(config.m_memory_bytes), m_shader(m_memory, config.m_shader),
          m_scoreboard(config.m_shader.m_max_warps), m_buffer(config.m_shader.m_max_warps) {
    }

    // 将测试机器码写入统一内存。
    void load_program(const std::vector<std::uint64_t>& words, std::uint32_t address) override {
        m_memory.load_instructions(address, words);
    }

    // 分配任务上下文并初始化独立单元状态。
    void launch(std::size_t threads,
        std::uint32_t entry,
        std::uint32_t param_base,
        unsigned register_count) override {
        m_shader.launch(entry, threads, param_base, register_count);
        m_scoreboard.reset();
        m_buffer.reset();
    }

    // 返回当前 kernel 配置可驻留的 warp 数量。
    std::size_t resident_warp_capacity() const override {
        return m_shader.resident_warp_capacity();
    }

    // 返回当前 kernel 配置可驻留的线程数量。
    std::size_t resident_thread_capacity() const override {
        return m_shader.resident_thread_capacity();
    }

    // 返回独立配置的硬件 GPR 字节预算。
    std::size_t regfile_capacity_bytes() const override {
        return m_shader.registers().capacity_bytes();
    }

    // 将功能模型执行批次转换为统一统计接口。
    ExecutionStats execution_stats() const override {
        const auto& stats = m_shader.execution_stats();
        return {stats.m_iu_batches, stats.m_fpu_batches, stats.m_lsu_batches, stats.m_wcu_batches};
    }

    // 返回与具体模型类型无关的执行结果。
    RunResult run(std::size_t budget) override {
        return m_shader.run(budget);
    }

    // 推进一个可执行线程组。
    bool step() override {
        return m_shader.step();
    }

    // 清除顶层及单元测试状态。
    void reset() override {
        m_shader.reset();
        m_scoreboard.reset();
        m_buffer.reset();
    }

    // 将全局线程编号映射到标量寄存器。
    std::uint32_t read_register(std::size_t thread, unsigned reg) const override {
        return m_shader.registers().read_gpr(thread / 32, thread % 32, reg);
    }

    // 设置指定线程的标量输入。
    void write_register(std::size_t thread, unsigned reg, std::uint32_t value) override {
        m_shader.registers().write_scalar(thread / 32, thread % 32, reg, value);
    }

    // 读取指定线程的浮点控制状态。
    std::uint32_t read_fcsr(std::size_t thread) const override {
        return m_shader.registers().read_fcsr(thread / 32, thread % 32);
    }

    // 设置指定线程的浮点控制状态。
    void write_fcsr(std::size_t thread, std::uint32_t value) override {
        m_shader.registers().write_fcsr(thread / 32, thread % 32, value);
    }

    // 通过公共内存接口读取测试结果。
    std::uint32_t read_memory(std::uint32_t address, unsigned size) const override {
        return m_memory.read(address, size);
    }

    // 通过公共内存接口初始化数据。
    void write_memory(std::uint32_t address, unsigned size, std::uint32_t value) override {
        m_memory.write(address, size, value);
    }

    // 查询测试指令在指定线程掩码下的依赖状态。
    bool dependency_ready(
        std::uint64_t encoding, std::size_t warp, std::uint32_t mask) const override {
        return m_scoreboard.ready(decode(encoding, warp, mask));
    }

    // 登记测试指令的目标寄存器占用。
    void reserve(std::uint64_t encoding, std::size_t warp, std::uint32_t mask) override {
        m_scoreboard.reserve(decode(encoding, warp, mask));
    }

    // 将独立测试指令放入指定 warp 队列。
    void enqueue(
        std::uint64_t encoding, std::size_t warp, std::uint32_t mask, std::uint32_t pc) override {
        auto instruction = decode(encoding, warp, mask);
        instruction.m_path.m_pc = pc;
        m_buffer.push(instruction);
    }

    // 报告指定 warp 的队列是否为空。
    bool queue_empty(std::size_t warp) const override {
        return m_buffer.front(warp) == nullptr;
    }

    // 返回队头地址以验证顺序和阻塞行为。
    std::uint32_t queue_pc(std::size_t warp) const override {
        const auto* front = m_buffer.front(warp);
        if (!front) {
            throw std::runtime_error("empty instruction queue");
        }
        return front->m_path.m_pc;
    }

    // 尝试发射并保留真实单元的依赖登记行为。
    bool issue(std::size_t warp) override {
        return m_isu.issue(warp, m_buffer, m_scoreboard, m_shader.registers()).has_value();
    }

    // 按原始发射掩码写回并解除对应占用。
    void writeback(std::uint64_t encoding,
        std::size_t warp,
        std::uint32_t mask,
        const std::array<std::uint32_t, 32>& values) override {
        func::ExecutionResult result;
        result.m_decoded = decode(encoding, warp, mask);
        result.m_values = values;
        m_wbu.writeback(result, m_shader.registers(), m_scoreboard);
    }

private:
    // 仅转换公共机器码输入，不在适配层执行功能断言。
    func::BufferedInstr decode(std::uint64_t encoding, std::size_t warp, std::uint32_t mask) const {
        return {{warp, 0, mask}, m_idu.decode(encoding)};
    }

    func::Mem m_memory; // 保存测试程序与数据，并供 Shader 共享访问。
    func::Shader m_shader; // 执行整程序测试并提供任务寄存器状态。
    func::Scoreboard m_scoreboard; // 保存独立单元测试的依赖状态，与顶层记分牌分离。
    func::Ibuffer m_buffer; // 保存独立单元测试的指令队列，与顶层缓冲分离。
    func::Idu m_idu; // 为独立单元测试解码机器码。
    func::Isu m_isu; // 执行独立发射测试并访问顶层寄存器。
    func::Wbu m_wbu; // 执行独立写回测试并更新对应记分牌。
};
}

// 创建功能模型适配器并隐藏模型私有类型。
std::unique_ptr<Dut> make_dut(const DutConfig& config) {
    return std::make_unique<FuncDut>(config);
}
}
