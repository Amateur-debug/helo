#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace helo {
enum class RunStatus { COMPLETED, BUDGET_EXHAUSTED, FAULT };
enum class FaultCode { NONE, COMMAND, SHADER_EXECUTION, SHADER_BUDGET };

// 保存可供程序判断的故障类别及各层实际取得的定位信息。
struct FaultInfo {
    FaultCode m_code = FaultCode::NONE; // 区分命令错误、Shader故障和命令内Shader预算耗尽。
    std::string m_message; // 保存供人阅读的诊断，不用于判定运行状态。
    std::optional<std::uint32_t> m_command_address; // 保存失败命令包的字节地址。
    std::optional<std::size_t> m_warp; // 保存发生指令故障的warp编号。
    std::optional<std::uint32_t> m_mask; // 保存故障指令组的有效线程掩码。
    std::optional<std::uint32_t> m_pc; // 保存故障指令的字节地址。
    std::optional<std::uint64_t> m_instruction; // 仅在取指成功时保存原始机器码。
};

// 报告Shader运行状态与本次成功提交的指令组数。
struct RunResult {
    RunStatus m_status = RunStatus::BUDGET_EXHAUSTED; // 预算暂停时不携带故障。
    std::size_t m_steps = 0; // 记录成功指令组数，不表示周期。
    FaultInfo m_fault; // 仅在FAULT状态保存故障。

    // 从唯一状态字段判断任务是否完成。
    bool completed() const {
        return m_status == RunStatus::COMPLETED;
    }
};

// 报告CP运行状态与本次成功消费的命令包数。
struct SubmitResult {
    RunStatus m_status = RunStatus::BUDGET_EXHAUSTED; // CP包预算暂停可续跑，包内失败为FAULT。
    std::size_t m_commands = 0; // 记录本次成功执行的命令包数。
    FaultInfo m_fault; // 保存失败包地址及可用的Shader故障上下文。

    // 从唯一状态字段判断全部缓冲区是否完成。
    bool completed() const {
        return m_status == RunStatus::COMPLETED;
    }
};
}
