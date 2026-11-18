# Shader 架构

Shader是[GPU](gpu.md)内部的可编程SIMT执行模块，由Gpu class按值持有，
顺序复用来执行顶点、片元和计算程序。“顶点Shader”和“片元Shader”在描述
程序时表示不同着色阶段，当前实现由同一个Shader模块执行。
本文件维护该子模块的内部架构；图形输入输出见[图形Shader ABI](graphics_abi.md)，
统一内存及取指/数据端口见[Mem模块](mem.md)。

## 指令集

Shader 核心执行 [HELO ISA v0.1](isa.md)，固定64位小端指令，8字节对齐；整数与 FP32 共用32位逻辑寄存器。指令语义与编码以 ISA 文档为准。

## RTL 当前实现范围

`hw/rtl/shader/`提供可综合的HELO译码器、整数单元、单lane控制单元、
访存单元和紧凑GPR/FCSR寄存器文件。`helo_shader_lane`连接IDU、IU、
LSU、WCU；LSU以单周期组合请求连接`helo_memory`共享字节存储，
有效存储在上升沿提交。`helo_shader_core`进一步串接取指、双源寄存器读取、
执行和写回，支持**单warp的lane 0一个线程**运行整数、控制和访存机器码，
提供任务启动、调试读写、完成与锁存故障端口。单线程BARW在参与者到达时推进PC。
`MAX_BYTES`决定内存物理容量，启动任务的寄存器声明使用一个完整warp的
GPR预算（即使只运行lane 0）。

本RTL尚未实现FPU/CSR、其余31个lane、多warp WMU/WSU、记分牌、分歧合组、
完整组预验证/原子提交、图形ABI与CP命令执行。FPU指令在核心中明确故障；
单lane访存故障不写回，不能据此推断多lane存储的整组提交。当前内存是零初始化
仿真数组和组合读/同步写端口，不包含带宽、延迟或外部存储协议。
功能模型的SIMT及GPU图形路径仍以以下章节描述为准。

## 执行模型

采用 SIMT 执行模型，每个 thread 执行标量指令：

| 项目 | 定义 |
| --- | --- |
| 执行方式 | SIMT（Single Instruction, Multiple Threads） |
| Warp 大小 | 32 个 thread |
| 通用寄存器 | 每个 thread 最多256个32位寄存器 r0–r255，数量由任务声明，r0 恒零 |
| 浮点状态 | 每个 thread 独立保存 fflags、frm，组合为 fcsr |
| SIMT 活跃掩码 | 32 位，每位对应一个 thread |

Warp 是指令调度与发射的基本单位。同一执行路径上的活跃 thread 执行相同指令，各 thread 保持独立的寄存器数据。分支发散由 SIMT 控制逻辑管理活跃掩码、执行路径和重汇合。

当 32 个 thread 全部活跃时，一条整数算术指令对 32 个 thread 各执行一次标量运算。该数值描述逻辑执行宽度，不限定物理运算通路数量或每周期吞吐。

## 硬件资源与任务配置

`Shader(memory, config={})`接收公共`ShaderConfig`，字段m_max_warps、
m_max_threads、m_regfile_bytes分别配置warp、线程槽位和GPR预算，默认
8、256、261120。它们独立约束资源，不由warp数自动推导GPR容量。
配置定义见[`helo/config.h`](../hw/include/helo/config.h)，不再使用多个位置参数。

`launch(entry, thread_count, param_base=0, register_count=256)` 将任务交给 WMU。`register_count` 是包含 r0 的连续逻辑编号范围大小，允许1–256；例如32表示使用 r0–r31，64表示使用 r0–r63。默认256只是省略参数时的值，每次启动可以传入不同数量。r0 不占 GPR 存储，其他寄存器没有别名或隐式溢出到内存。

```text
每线程 GPR 字节 = (register_count - 1) × 4
每 warp GPR 字节 = 32 × (register_count - 1) × 4
硬件 warp 槽位 = min(max_warps, floor(max_threads / 32))
任务驻留 warp 容量 = min(硬件 warp 槽位, floor(regfile_bytes / 每 warp GPR 字节))
任务驻留线程槽位 = 任务驻留 warp 容量 × 32
```

- `register_count=1` 时仅有 r0，不消耗 GPR，驻留容量只受硬件 warp/thread 上限限制。
- 默认 GPR 预算为261120字节（255 KiB）。若硬件支持64个 warp、2048个线程，同一预算在256寄存器任务时支持8个 warp，在32寄存器任务时支持64个 warp。
- `max_warps>0`，`max_threads>=32`，线程上限向下取整为完整 warp 槽位；GPR 预算可以为零，但必须4字节对齐。容量计算先检查溢出。
- 尾 warp 按完整32线程分配 GPR，未启动的 lane 不执行。FCSR、记分牌和 PC 属于独立控制资源，不计入 GPR 字节预算。
- Regfile 仅为接纳任务实际需要的 warp 分配紧凑 GPR 存储，槽位索引由 warp、lane 和任务寄存器数共同确定。任务布局变化时重新分配；相同布局复用存储，每次启动仍清零。
- WMU 在替换旧任务前检查寄存器声明、硬件限制和所需 warp 数；整个任务必须能同时驻留，超限拒绝，不实现分批补充调度。入口取指验证也在替换前完成，拒绝的新任务不破坏旧任务。
- ISU 在执行前检查全部有效源和目的编号是否小于任务 `register_count`，包括第三源；越界失败不提交本指令组副作用。CSR zimm 是数据，不受 GPR 数量限制。
- `thread_capacity(register_count)`查询给定声明的容量且不改变任务；
  `resident_warp_capacity()` 和 `resident_thread_capacity()` 报告按当前任务寄存器数量计算的容量；启动前按默认256计算。`Regfile::capacity_bytes()` 始终返回硬件预算，`register_count()` 返回任务声明。

## 执行通路配置与分批执行

`ShaderConfig::m_execution`中的 `ExecutionConfig` 独立配置 `m_iu_count`、`m_fpu_count`、`m_lsu_count`、`m_wcu_count`，均默认32，范围1–32。数量表示每个 Shader 内对应的32位标量执行通路数：一批中每条通路最多处理一个线程；它不是完整SIMT单元的复制数量。整数乘除归IU，CSR归FPU，分支与屏障归WCU；未定义指令的SFU不配置数量。

```cpp
ShaderConfig config;
config.m_max_warps = 16;
config.m_max_threads = 512;
config.m_regfile_bytes = 64 * 1024;
config.m_execution.m_iu_count = 16;
config.m_execution.m_fpu_count = 8;
config.m_execution.m_lsu_count = 4;
config.m_execution.m_wcu_count = 8;
Shader shader(memory, config);
```

同一指令组只取指、译码、读取旧源操作数一次，然后按所选执行单元的数量划分连续lane区间。例如8条FPU通路依次处理lane0–7、8–15、16–23、24–31；全空区间跳过，稀疏掩码不压紧，不同warp不合批。数量不必整除32，最后一批可以不足配置数量。

每个批次复用原始指令和操作数快照，仅改变执行掩码。寄存器结果、异常标志、退出掩码和下一PC合并后，统一按原始线程组提交；BARW也只在整组完成后登记。LSU在首批访问前检查整个原始组的全部地址，防止后一批越界导致前面批次部分存储。

`execution_config()` 返回固定通路配置；`execution_stats()` 返回当前任务成功提交的IU/FPU/LSU/WCU批次数，reset或成功launch清零，续跑累计，失败指令的批次不计入。它们不是周期、延迟或访存事务数。一次step仍完成整个原始线程组，不模拟不同批次与其他warp交错、多发射、寄存器带宽和访存合并。通路数改变执行批次，不改变WMU驻留容量。

## 模块划分

源码位于 `hw/model/func_model/shader/`，按单元平铺组织。除数据结构头文件外，各单元采用同名 `.h` 和 `.cpp` 文件。

| 模块 | 职责 |
| --- | --- |
| `shader` | 核心顶层：实例化、连接各单元，协调复位、运行与完成通知 |
| `wmu` | Warp Management Unit：接收 Shader 任务，将调用组织为 warp，分配执行上下文并初始化输入；跟踪完成状态并回收上下文 |
| `wsu` | Warp Scheduling Unit：根据 warp 运行状态选择可执行 warp，向取指单元提供 warp 标识和当前路径 PC |
| `ifu` | 根据 PC 向 `icache` 请求指令编码，携带 warp 标识与 PC 交给解码单元 |
| `idu` | Instruction Decode Unit：解码 HELO 指令，产生操作、操作数编号、立即数和目标执行单元等信息 |
| `isu` | 指令发射：从 `ibuffer` 获取候选指令，查询 `scoreboard` 检查依赖，读取 `regfile` 并向目标执行单元发射 |
| `ibuffer` | 独立指令缓冲：接收 `idu` 的解码结果及 warp 标识，按各 warp 的程序顺序提供待发射指令 |
| `scoreboard` | 独立依赖状态表：跟踪各 warp/thread 尚未完成的寄存器写入，提供依赖查询并处理发射登记与写回完成通知 |
| `regfile` | 保存每个 thread 的通用寄存器及浮点状态，提供操作数读取和结果写回接口 |
| `wbu` | Writeback Unit：接收执行结果，根据目标寄存器和指令执行时的有效写入掩码写入 `regfile`，反馈写回完成 |
| `wcu` | Warp Control Unit：计算执行路径的下一 PC，处理跳转、分支发散、活跃掩码、重汇合及线程退出 |
| `iu` | 执行整数算术、比较、逻辑和移位等操作 |
| `fpu` | 执行常规浮点算术、比较与转换，遵循规定的数值语义 |
| `lsu` | 生成数据访问地址，通过 `dcache` 接口完成加载、存储 |
| `sfu`（预留） | 尚未定义指令，未接入Shader；现有占位实现明确拒绝请求 |

`encoding.h` 提供主 opcode 宏、格式分类与字段解析宏。IDU 先确定 R、R4、I、S、B、U、J 格式，再调用对应 `HELO_DECODE_<格式>` 宏，最后通过指令族内的 funct 表生成执行操作与依赖。保留位、未用操作数、立即数移位范围和舍入控制严格检查。I/S 的立即数为12位，B/J 恢复隐含零位，U 的20位立即数左移12位；CSR 地址及 zimm 独立解释。共用测试独立编码，不共享生产解码宏。

`warp.h` 和 `instr.h` 是数据结构定义，不是独立执行单元：前者描述执行上下文、当前路径 PC、线程掩码及重汇合状态，后者描述解码后的指令。Warp 上下文由核心统一持有，`wmu` 初始化，`wsu` 读取调度状态，`wcu` 更新路径控制状态；寄存器数据由 `regfile` 单独保存。

## 执行流程与接口边界

```text
wmu：创建并初始化 warp 上下文
    ↓
wsu → ifu → idu → ibuffer → isu ← regfile
                          ↓
                iu / fpu / lsu / wcu
                          ↓
                wbu → regfile（有寄存器结果时）
                          ↓
                执行完成反馈
                          ↓
                wcu 更新 PC 和线程掩码
                          ↓
                wsu 选择下一可执行 warp
```

- `shader` 顶层负责连接和协调，取指、解码和具体指令语义由相应单元实现。
- `cp` 负责 GPU 级命令与任务分发；`wmu` 负责核心内部的 warp 上下文分配；`wsu` 负责已分配 warp 的执行选择。
- `ifu` 使用当前路径 PC 取指，不独立决定分支或重汇合后的路径。
- `isu` 负责读取源操作数，`wbu` 负责写回执行结果，`regfile` 负责保存寄存器数据并提供读写接口。
- 写回请求携带 warp 标识、目标寄存器、结果及有效写入掩码，避免使用后续已变化的活跃掩码。无寄存器结果的指令不产生寄存器写回，仍需反馈执行完成；`wbu` 不负责 PC、执行路径或存储器写入。
- `wcu` 根据普通指令完成、分支条件或控制指令更新路径状态。按独立线程PC合并可执行路径，不要求固定重汇合点。
- `ifu` 与 `mem/icache` 交互完成取指；`lsu` 与 `mem/dcache` 交互完成数据访存。两类请求最终访问同一底层内存。
- 当前 Shader 没有纹理请求指令或采样回调接口。GPU内部Cp在启动片元程序前调用 `tex`，将结果按图形 ABI 写入输入寄存器；未来 ISA 纹理扩展的接口尚未实现。`sfu` 不承担纹理采样。
- 功能模型保留 `ibuffer` 排队与 `scoreboard` 依赖状态，但不模拟流水线寄存器、缓存延迟或周期带宽。图中流程表示职责关系，不限定未来 RTL 的流水级划分。

### 指令缓冲、依赖跟踪与发射

```text
idu → ibuffer → isu → 执行单元 → wbu → regfile
                ↕                 │
            regfile               │ 写回完成
                ┌─────────────────┘
                ↓
            scoreboard ← isu（依赖查询与发射登记）
```

三个模块由 `shader` 顶层分别实例化并连接，源码平铺在 `shader/`，分别使用同名 `.h/.cpp` 文件；`isu` 不拥有缓冲存储或依赖状态表。

`ibuffer` 在指令被成功发射后出队。`isu` 只有在依赖满足、操作数可读且目标执行单元能接收时才发射，并通知 `scoreboard` 登记待写寄存器。相关结果经 `wbu` 写入且可读取后，`scoreboard` 解除占用。被阻塞的候选指令保留在 `ibuffer` 中。

线程的有效掩码应纳入依赖判断，或使用明确的保守阻塞策略。存储顺序与同步操作不能仅靠寄存器记分牌保证，需要访存和执行控制逻辑配合。

## 存储接口

`icache` 和 `dcache` 位于 `hw/model/func_model/mem/`，分别采用同名 `.h/.cpp` 文件：

```text
shader/ifu ←→ mem/icache ←→ mem/mem
shader/lsu ←→ mem/dcache ←→ mem/mem
```

- `icache` 提供指令读取接口，将取得的指令数据或访问错误返回 `ifu`。
- `dcache` 提供数据读取、写入接口；加载结果返回 `lsu`，由其提交给 `wbu` 写回寄存器。
- `mem` 使用 C++ 的 `std::vector<std::uint8_t>` 保存统一字节地址空间，按小端方式读写数据。
- 当前 `icache` 和 `dcache` 是 C++ 直通适配层：分别将取指和数据请求直接转发到同一个 `Mem` 对象，不建立缓存副本，不模拟缓存命中、缓存行、替换、延迟或带宽。实际缓存机制待后续定义。

## 运行与提交

- `Mem::load_instructions` 加载64位小端程序；`load_words` 保留为32位数据装载接口。`Icache::fetch` 返回完整64位指令，普通指令成功后 PC 增加8字节。JALR 不清除最低位；实际采用的目标要求8字节对齐。
- WSU 轮转 warp，再轮转选择可执行 lane，将同 warp、同 PC、非屏障等待的线程合组，保证持续可执行线程取得进展。
- BARW 使用固定 launch_mask；到达线程停在屏障 PC，全部到达后统一推进8字节并清除本轮等待。参与者已退出、等待时 EXIT、同轮不同 BARW PC 均报协议错误。
- 整数与 FP32 运算、访存、NOP/FENCE、EXIT/TRAP、BARW 和六种 CSR 指令通过完整 Shader 链路执行。FP32 使用本地 Berkeley SoftFloat，保留 IEEE 舍入、渐进下溢、NaN 和异常标志语义，详见 [来源记录](softfloat.md)。
- CSR 地址遵守 HELO：FFLAGS=0、FRM=1、FCSR=2；0x10–0x16 提供线程编号、warp编号、任务线程总数、warp宽度、参数地址和 ISA 版本。读写抑制、只读权限与 rd/rs1 重叠按 ISA 执行。
- 同 PC 指令组先验证后提交。加载/存储先验证所有有效线程地址，FPU 先计算所有线程结果与异常，再由 WBU 一并写回；失败组不部分更新 GPR、FCSR、内存或 PC。此前提交的其他指令保留。
- 数据访问在一个指令组内按 lane 升序同步完成，满足 ISA 全序要求；该具体顺序不是软件可依赖的跨实现保证。FENCE 在同步模型中不需要额外等待。
- `step()` 执行一个指令组返回 true，全部完成返回 false，真实错误抛出异常并锁存。`run(max_steps)` 返回结构化状态、成功指令组数和故障上下文；详细约定统一见[运行状态协议](execution.md)。
- `reset()` 清除上下文、寄存器内容与错误，不清除外部内存。launch 初始化全部已分配 GPR 和 FCSR 为零，参数通过 CSR 获取，无隐式输入 GPR。全部线程 EXIT 后完成。
- `read_gpr/write_scalar` 提供寄存器检查和初始化接口；FCSR 使用独立接口。零线程任务不分配可访问线程上下文，尾 warp 的未启动 lane 仍有清零槽位。

本功能模型不模拟周期、缓存命中、带宽和流水延迟。纹理、共享内存、原子操作等尚未定义的 ISA 功能不作为已实现指令。

## 单个指令组的执行算法

Shader是GPU内部的执行模块，CP每次launch提供任务，Shader.step完成一个
warp中同PC的有效线程组。该同步实现的调用顺序如下：

```text
Wsu.select → ScheduledWarp(warp,pc,mask)
  → 若该warp的Ibuffer为空：Ifu/Icache取64位编码 → Idu译码 → Ibuffer.push
  → Isu检查依赖/寄存器声明 → 读取旧源操作数 → reserve → 出队
  → Wcu.validate检查控制协议
  → execute_batches按目标单元通路数执行
  → Wbu提交GPR/FCSR并release
  → Wcu.complete提交PC/退出/屏障
  → 累计成功批次，step返回true
```

ScheduledWarp仅表示本次选中的路径。BufferedInstr将路径与译码信息绑定；
IssuedInstr增加每lane的源值/FCSR快照及任务参数；ExecutionResult保存每lane
结果、下一PC、FCSR、退出mask。写回使用发射时的mask，不使用之后改变的
live_mask。异常时释放本次预留依赖、清除指令缓冲并锁存诊断；此前成功
指令保留。同步step中遇到不可发射的依赖视为内部停滞错误，不模拟等待周期。
实现：[shader.cpp](../hw/model/func_model/shader/shader.cpp)。

### WMU与Regfile：任务接纳和紧凑存储

所需warp数为`threads/32 + (threads%32 != 0)`；按前述资源公式检查容量。
WMU在临时向量内建立各warp，所有lane PC置entry，满warp的launch/live mask
为0xffffffff，尾warp为`(1<<remaining)-1`。有线程时还验证入口可完整取指，
再配置新的Regfile并替换旧任务；资源/入口拒绝不会先破坏旧任务。

每线程r0恒零且不存储。声明R个寄存器时，非零寄存器r的物理uint32索引是
`(warp*32+lane)*(R-1)+(r-1)`。先验证r<R，再读写；FCSR是独立的
warp×32数组。这样R越小每warp所占GPR越少，且不同任务重新配置不会产生
r32与r0之类的别名。实现：[wmu.cpp](../hw/model/func_model/shader/wmu.cpp)、
[regfile.cpp](../hw/model/func_model/shader/regfile.cpp)。

### WSU：双层轮转和动态合组

从m_next_warp开始轮转，计算`ready=live_mask & ~barrier_mask`。跳过无
ready线程的warp，从该warp的m_next_thread轮转找到一个ready lane；取其PC，
再扫描32lane，将ready且PC相等者合成mask。最后把两个轮转游标都推进到
本次选择项之后。不同PC路径交替取得进展，同PC可以再次合组。

例如lane0/1的PC为P、lane2/3为Q，调度一次P路径mask=0x3，另一次Q路径
mask=0xc；只有各lane的PC实际重新相等才可再次组合。不采用显式重汇合栈
或编译器指定的统一重汇合点。实现：[wsu.cpp](../hw/model/func_model/shader/wsu.cpp)。

### IDU、Ibuffer、ISU与Scoreboard

IDU按opcode确定格式，再按funct映射Operation与执行单元；同时生成
uses_rs1/2/3、writes_rd、uses_fcsr和立即数标志。未使用字段必须为零，
保留舍入值/越界移位立即数等在译码时拒绝；动态FRM合法性则在FPU执行时检查。
Ibuffer是每warp独立队列，ISU只取队首，不做乱序选取。

Scoreboard按每warp、每寄存器保存待写lane掩码，并有独立FCSR槽。
只要任一有效源、目标或所用FCSR的pending与当前mask相交就不能发射。
r0不占用pending。ISU先读取所有旧源（包括rs3），再reserve目标mask并
出队；WBU在提交后清除这些位。这同时处理RAW/WAW与FCSR依赖，允许
不相交lane的寄存器编号互不阻塞，不承担内存一致性或屏障职责。
实现：[idu.cpp](../hw/model/func_model/shader/idu.cpp)、
[isu.cpp](../hw/model/func_model/shader/isu.cpp)、
[scoreboard.cpp](../hw/model/func_model/shader/scoreboard.cpp)。

### IU/FPU/LSU：计算与副作用

IU逐有效lane执行32位运算；加减用uint32模2^32，移位量取低5位。
有符号比较通过翻转符号位再作无符号比较；算术右移先逻辑移位再按原符号
补高位。乘法使用64位中间值取低/高32位，有符号乘除先扩为int64避免
32位INT_MIN/-1溢出；除零商为全1，余数保留被除数。它不模拟多周期乘除器。
逐条ISA语义见[ISA](isa.md)，实现：[iu.cpp](../hw/model/func_model/shader/iu.cpp)。

FPU选择rm（7表示从旧FCSR取FRM），对真正使用舍入的操作拒绝rm>4。
每lane清空SoftFloat本次异常，按操作调用软件FP32函数；FMA修改乘积/加数
符号后调用融合mulAdd，避免中间舍入。需要规范化的NaN结果变为0x7fc00000，
新fflags与旧值合并。CSR同样消费旧源/旧FCSR，先产生rd旧值再产生新状态，
重叠寄存器不影响读改写顺序。整个组结果先保存，WBU才提交；作用域退出
恢复宿主SoftFloat环境。实现：[fpu.cpp](../hw/model/func_model/shader/fpu.cpp)。

LSU按操作选择宽度1/2/4，用uint32计算`address=src1+immediate`。
Shader在任何分批存储前验证原始完整组所有地址，LSU执行时还验证当前
批次；然后按lane顺序读取或写入。窄有符号load用
`(value ^ sign_bit)-sign_bit`扩展，store使用src2低有效字节。
存储直接写Mem，加载结果等待WBU；整组预验证避免后批地址错误留下前批
存储。实现：[lsu.cpp](../hw/model/func_model/shader/lsu.cpp)。

### 分批合并、WBU与WCU

目标单元有C条通路时，遍历[0,C)、[C,2C)…连续lane区间，与原mask相交；
空区间跳过，不压紧稀疏lane。所有批次复用IssuedInstr旧源快照，按lane
复制结果/next_pc/FCSR并合并exit_mask。全部成功后WBU写寄存器/FCSR并
释放依赖，WCU再提交PC/控制状态，最后累计成功批次数。

普通分支每lane计算条件，taken时next_pc=path.pc+offset，未taken为pc+8；
JALR使用src1+offset，目标必须8字节对齐。EXIT从live_mask移除有效lane。
BARW先验证live_mask==launch_mask且同轮PC一致，执行结果让到达线程留在
屏障PC；complete合并barrier_mask，只有等于launch_mask时统一推进pc+8
并清空屏障状态。等待lane不再被WSU选择。不是所有控制路径都由相同lane
执行，因此不能用单个warp PC代替每lane PC数组。
实现：[wbu.cpp](../hw/model/func_model/shader/wbu.cpp)、
[wcu.cpp](../hw/model/func_model/shader/wcu.cpp)。

### 运行预算与错误状态

Shader.run使用公共RunResult；COMPLETED、BUDGET_EXHAUSTED、FAULT的定义、
定位字段及跨层恢复规则统一见[运行状态协议](execution.md)。暂停不写故障，
真实故障保持到reset或成功launch。CP包内的Shader预算不足属于命令故障。

## GPU 图形阶段集成

GPU内部Cp调度本Shader模块执行顶点与片元机器码，按最多128调用分批，
由Cp通过显式寄存器初始化/读取实现图形输入输出。纹理目前是片元启动前的
固定功能采样输入，不是已实现的 ISA 纹理指令。完整 ABI、批次内 CSR
语义及提交边界见 [图形Shader ABI](graphics_abi.md)。


## 图形调用寄存器ABI

权威寄存器表、调用编号和批次语义移至[图形Shader ABI](graphics_abi.md)。
本节保留导航锚点，避免旧链接失效。

## 初始化职责

launch先在临时对象中验证资源与入口，再由Regfile.configure准备布局并且只
初始化一次GPR/FCSR；相同布局复用已分配存储。随后reset_control仅清除warp、
调度、依赖、错误和计数，不再重复清零寄存器。显式reset则调用reset_control
和Regfile.reset，保留外部Mem。失败的launch不替换旧任务或清理旧寄存器。
