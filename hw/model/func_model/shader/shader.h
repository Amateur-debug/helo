#pragma once
#include "fpu.h"
#include "helo/config.h"
#include "helo/execution.h"
#include "idu.h"
#include "ifu.h"
#include "isu.h"
#include "iu.h"
#include "lsu.h"
#include "tex/tex.h"
#include "wbu.h"
#include "wcu.h"
#include "wmu.h"
#include "wsu.h"
#include <string>

namespace helo::func {
// 累计当前任务已成功提交指令所使用的执行批次数，不表示周期。
struct ExecutionStats {
    std::uint64_t m_iu_batches = 0; // 记录整数执行批次数。
    std::uint64_t m_fpu_batches = 0; // 记录浮点及CSR执行批次数。
    std::uint64_t m_lsu_batches = 0; // 记录数据访问批次数。
    std::uint64_t m_wcu_batches = 0; // 记录控制执行批次数。
};

// 连接各执行单元，管理任务启动、指令组执行与错误状态。
class Shader {
public:
    explicit Shader(Mem& memory, const ShaderConfig& config = {});
    std::size_t resident_warp_capacity() const;
    std::size_t resident_thread_capacity() const;
    std::size_t thread_capacity(unsigned register_count) const;
    const ExecutionConfig& execution_config() const;
    const ExecutionStats& execution_stats() const;
    void reset();
    void launch(std::uint32_t entry,
        std::size_t thread_count,
        std::uint32_t param_base = 0,
        unsigned register_count = REGISTER_COUNT,
        const DispatchRange& range = {});
    bool step(const Tex::Bindings& textures = {});
    RunResult run(std::size_t max_steps = 100000, const Tex::Bindings& textures = {});
    Regfile& registers();
    const Regfile& registers() const;
    const std::vector<Warp>& warps() const;

private:
    void reset_control();
    ExecutionResult execute_batches(
        const IssuedInstr& instruction, std::uint64_t& batches, const Tex::Bindings& textures);
    DispatchRange m_range; // 保存当前批次的全局编号与工作组配置。
    Mem m_shared{0}; // 保存当前工作组独立且初始为零的共享存储。
    Dcache m_shared_port{m_shared}; // 转发工作组共享地址访问。
    Lsu m_shared_lsu{m_shared_port}; // 与全局地址空间共用相同的访存提交规则。
    ExecutionConfig m_execution; // 保存核心固定的执行通路配置。
    ExecutionStats m_stats; // 累计成功提交的各类执行批次。
    std::size_t m_max_threads; // 保存硬件支持的线程槽位上限。
    std::vector<Warp> m_warps; // 保存当前任务已分配的 warp 执行上下文。
    Icache m_icache; // 提供连接统一内存的取指接口。
    Dcache m_dcache; // 提供连接统一内存的数据访问接口。
    Regfile m_registers; // 保存当前任务的 GPR 与各线程浮点状态。
    Scoreboard m_scoreboard; // 跟踪核心内指令组的未完成寄存器与浮点状态依赖。
    Ibuffer m_buffer; // 保存各 warp 尚未发射的解码指令。
    Wmu m_wmu; // 负责资源容量计算与任务 warp 上下文初始化。
    Wsu m_wsu; // 选择下一组可执行的同 PC 线程。
    Ifu m_ifu; // 读取调度路径上的机器码。
    Idu m_idu; // 将机器码转换为解码指令。
    Isu m_isu; // 检查依赖并读取待执行指令的操作数。
    Iu m_iu; // 执行整数算术与逻辑运算。
    Fpu m_fpu; // 执行 FP32 运算与 CSR 访问。
    Lsu m_lsu; // 完成有效线程的数据加载与存储。
    Wcu m_wcu; // 检查并更新线程 PC、屏障和退出状态。
    Wbu m_wbu; // 提交寄存器结果并解除待写依赖。
    std::uint32_t m_thread_count = 0; // 保存当前任务启动的线程总数。
    std::uint32_t m_param_base = 0; // 保存当前任务参数区的字节基地址。
    FaultInfo m_fault; // 锁存执行故障和上下文，直到复位或成功启动新任务。
};
}
