# 文档导航

先阅读[GPU顶层架构](gpu.md)理解整机组成、模块连接和执行流程，再进入对应
模块文档。功能模型每个一级模块目录均有同名Markdown；内部单元在其所属
模块文档中说明，机器码和跨模块ABI分别集中维护。

模块文档按当前源码解释输入输出、数据流、执行顺序、关键算法、数值例子
及错误提交边界。算法章节描述功能模型实际执行的过程；没有实现的硬件
并行、缓存、周期行为会明确列为限制。

## 模块设计

GPU是整机；CP、Geom、Rast、Shader、Tex、ROP和Mem是GPU内部模块。
下表按文件索引列出顶层与子模块，不表示它们处于同一硬件层级。

| 模块 | 文档 | 内容 |
| --- | --- | --- |
| gpu | [gpu.md](gpu.md) | 整个GPU架构、对象所有权、数据/命令流、接口、资源、提交与生命周期 |
| cp | [cp.md](cp.md) | command buffer格式、内存解析、模块调度、游标与错误恢复 |
| geom | [geom.md](geom.md) | Vfetch/Pa/Clip/Vp层次、顶点布局、裁剪、视口和剔除 |
| rast | [rast.md](rast.md) | Setup/Interp层次、覆盖规则、包围盒及透视插值 |
| shader | [shader.md](shader.md) | GPU内部可编程执行模块的SIMT架构、调度/执行单元、资源、指令组执行和提交 |
| tex | [tex.md](tex.md) | 纹理布局、寻址、过滤、固定功能采样接口 |
| rop | [rop.md](rop.md) | Zrop/Crop层次、附件、深度模板、混合和写回 |
| mem | [mem.md](mem.md) | 统一存储、Icache/Dcache端口、对齐/边界与可见性 |
| common | [common.md](common.md) | 共享数据记录、配置与数值后端；不对应运行时硬件实例 |

## 协议、构建与验证

| 文档 | 内容 |
| --- | --- |
| [功能模型结构](func_model.md) | 源码组织、模块入口、构建依赖和方式 |
| [图形Shader ABI](graphics_abi.md) | 顶点/片元寄存器输入输出、全局编号、批次和discard |
| [运行状态协议](execution.md) | 完成、预算暂停、故障与跨层定位字段 |
| [图形数值契约](numerics.md) | 量化、舍入、比较容差与RTL接入要求 |
| [图形RTL实施](rtl_graphics_plan.md) | 已实现的覆盖单元接口、阶段连接顺序与各阶段验收门槛 |
| [HELO ISA](isa.md) | 指令编码、逐条语义、CSR、初值和提交边界 |
| [验证规范](verification.md) | 共用DUT、CTest入口、日志与PNG复现 |
| [SoftFloat来源](softfloat.md) | 数值源码来源、补丁、许可与校验清单 |
| [C++代码规范](coding-style/cpp.md) | 命名、注释与代码组织 |

机器码语义以ISA为准，图形寄存器约定属于独立执行环境ABI。文档描述当前
功能模型，不将尚未实现的RTL、性能模型、驱动或图形API兼容写作已完成。

协议文档规定可观察行为；模块文档中的批次、容器和同步调用描述当前实现策略。
公共类型的权威定义在`hw/include/helo/`，测试不依赖功能模型私有头。
