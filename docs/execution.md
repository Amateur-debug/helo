# 运行状态与故障协议

权威C++定义位于[`helo/execution.h`](../hw/include/helo/execution.h)，不依赖
功能模型。Shader返回RunResult，CP/GPU返回SubmitResult；两者使用相同的
RunStatus与FaultInfo。调用者以m_status分支，诊断文本只供人阅读。

| 状态 | 含义 | m_fault | 后续动作 |
| --- | --- | --- | --- |
| COMPLETED | 本层全部工作完成 | code=NONE，其余为空 | 可提交新工作 |
| BUDGET_EXHAUSTED | 本次推进预算用完，仍有工作 | code=NONE，其余为空 | 在当前状态继续run |
| FAULT | 执行故障已锁存 | 类别、文字及可用上下文 | 按层级规则复位或重新启动 |

`completed()`由状态计算，不再维护第二份布尔状态。Shader的m_steps统计本次
成功指令组，CP的m_commands统计本次成功命令包，均不表示周期。
非空任务run(0)返回BUDGET_EXHAUSTED，已完成任务run(0)返回COMPLETED；
已锁存故障即使预算为零也返回FAULT。暂停不产生故障字符串。

FaultInfo的m_code为NONE、COMMAND、SHADER_EXECUTION或SHADER_BUDGET。
COMMAND表示命令解析或模块执行阶段的异常；SHADER_EXECUTION表示Shader
指令故障；SHADER_BUDGET表示一个CP命令内部的Shader预算不足。
m_command_address、m_warp、m_mask、m_pc、m_instruction均为optional，
只保存确实取得的信息。取指失败没有机器码；机器码0是有效诊断值，不代表缺失。
CP传播Shader故障时保留全部指令上下文，并补充失败命令包地址。

Shader自己的指令组预算耗尽可续跑；CP包内Shader预算耗尽会变为锁存的
SHADER_BUDGET故障，整条命令不能据此暂停恢复。CP的包数预算只在包间暂停。
单指令组、片元、命令包各自的提交边界不变，见[Shader](shader.md)、
[ROP](rop.md)和[CP](cp.md)。

Shader故障由reset或成功launch解除；CP故障必须reset后重新提交。
复位保留资源内存。新的接口替代旧completed/m_completed布尔字段及error/
m_error字符串；调用方应使用completed()和m_fault.m_message，并通过m_status/
m_fault.m_code判断语义，不能再匹配“instruction budget exhausted”文本。
