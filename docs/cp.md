# Cp 命令处理模块

## 职责与连接

Cp是[GPU](gpu.md)内部统一的命令解析和调度模块。主机将命令写入Mem中的
command buffer，只提交起始地址和字节长度；CP从内存读取包、解析状态，
直接协调Geom、Rast、Shader、Tex和Rop。Gpu不提供独立draw/dispatch/clear
入口，也不再承担任务执行算法。

Gpu持有所有子模块，并在构造Cp时传入Mem、Geom、Rast、Shader、Tex、Rop
和GpuStats引用。Cp不持有Gpu引用，不通过Gpu回调执行。代码入口：
[`cp.h`](../hw/model/func_model/cp/cp.h)、
[`cp.cpp`](../hw/model/func_model/cp/cp.cpp)、
[`command.h`](../hw/model/func_model/cp/command.h)。

## 统一提交、游标与生命周期

`Gpu::submit(address,byte_size)`转交`Cp::enqueue(address,byte_size)`。
每个buffer可包含多个不同操作的包，多个buffer按提交顺序执行。
提交要求起点4字节对齐、非零长度、长度为256倍数，完整范围处于Mem和32位
地址空间内。范围错误在提交时抛异常，不入队，不改变已有队列。

Cp保存每个buffer的地址、长度和消费偏移，不在提交时复制或解析所有包。
`run(max_commands=100000)`的预算单位是命令包；成功执行一包后才推进
游标256字节，消费完整buffer后才移除该buffer。`pending()`统计未完成
buffer数，包含失败buffer，不再表示包数。

当前包执行前读取完整256字节快照，校验全部字段和保留字后才调度模块。
未消费包在稍后执行时读取，修改已消费包不会导致其重放。生产调用者应在
buffer完成前保持其内容稳定；此同步模型不阻止主机或Shader改写后续包，
若改写，后续解析会看到新值。资源也按执行时的内存内容读取。

`SubmitResult`的m_commands统计本次成功包数；m_status、completed()和m_fault
遵循[运行状态协议](execution.md)。故障包含命令字节地址，Shader故障还保留
warp/掩码/PC及已取得的机器码。包预算暂停没有故障，可继续run。

## 命令包格式 v1

每包固定256字节（64个小端uint32字），按包顺序连续排列，无宿主struct
填充。字索引从0开始，偏移为索引乘4；所有未使用负载字必须为0。
该协议独立于Shader的64位HELO指令编码，不提升HELO ISA版本。

| 字索引 | 含义 |
| --- | --- |
| 0 | 固定magic/version：0x48434d01 |
| 1 | opcode：1=CLEAR，2=RENDER，3=COMPUTE |
| 2 | 固定包长度256，其他值拒绝 |
| 3 | 保留，必须为0 |
| 4–63 | 操作负载及零填充 |

所有bool字段使用独立uint32字且只能为0/1；8位模板字段的高24位必须为0。
float按IEEE binary32位模式保存。枚举必须是已定义值，不能依赖窄化截断。
包没有默认字段省略语义，主机须显式写全所用状态。

### 公共嵌入记录

| 记录 | 顺序字段（每项一个字） |
| --- | --- |
| Framebuffer，7字 | color_address、depth_address、stencil_address、width、height、has_depth、has_stencil |
| ShaderProgram，5字 | enabled、entry、param_base、register_count、step_budget |

Framebuffer语义见[ROP](rop.md)。ShaderProgram预算是每批指令组数，启用
程序入口须8字节对齐，VS/FS寄存器声明分别至少12/16，上限256。
计算程序使用普通launch约定，图形程序使用[Shader图形ABI](graphics_abi.md)。

### CLEAR负载

| 字索引 | 字段 |
| --- | --- |
| 4–10 | Framebuffer |
| 11–14 | float color.r/g/b/a |
| 15 | float depth |
| 16 | stencil，范围0–255 |
| 17–18 | 附件扩展标志和第二颜色附件地址，同 RENDER 字 59–60 |
| 19–63 | 零填充 |

CP解码后调用Rop.clear，清除全部已配置附件，不使用RENDER的写入掩码。

### COMPUTE负载

| 字索引 | 字段 |
| --- | --- |
| 4 | entry |
| 5 | thread_count |
| 6 | param_base |
| 7 | register_count |
| 8 | step_budget |
| 9 | group_size；0 表示按核心容量分批，正数声明一维工作组大小 |
| 10 | shared_bytes；每组共享存储字节数，4 字节对齐，最多 65536 |
| 11–63 | 零填充 |

所有字段为32位。CP按全局编号递增顺序启动批次，所有批次合计受同一个正数
step_budget限制。group_size为零时取当前寄存器声明下的驻留线程容量为批次
上限，shared_bytes须为零。group_size为正时每次执行一个组，声明值须能
完整驻留，最后一组可不足声明大小。每组从零初始化独立共享存储。成功的
全局写入和原子加立即可见，组之间不能使用屏障或依赖并发驻留。线程总数为
零时仍检查启动和资源字段，合法包消耗零条指令。编号和指令见
[工作组扩展](isa.md#计算工作组扩展)。

### RENDER负载

| 字索引 | 顺序字段 |
| --- | --- |
| 4–9 | vertex_address、vertex_count、vertex_stride、indexed、index_address、index_count |
| 10–15 | float viewport.x/y/width/height/min_depth/max_depth |
| 16–17 | cull_mode、front_counter_clockwise |
| 18–24 | Framebuffer |
| 25–29 | depth_test、depth_write、depth_compare、stencil_test、stencil_compare |
| 30–32 | stencil_reference、stencil_read_mask、stencil_write_mask |
| 33–35 | stencil_fail、depth_fail、pass（模板更新操作） |
| 36–37 | blend、color_write |
| 38–42 | 顶点ShaderProgram |
| 43–47 | 片元ShaderProgram |
| 48 | texture_enabled |
| 49–51 | texture.address/width/height |
| 52–54 | texture.filter/wrap_u/wrap_v |
| 55–56 | Shader 纹理绑定表地址、槽数。每槽七字，详见 [Tex 设计](tex.md) |
| 57–58 | 顶点布局表地址、自定义分量数 |
| 59–60 | 附件标志、第二颜色附件地址 |
| 61 | 可选光栅状态表地址，零使用默认值 |
| 62 | 数值配置，0 为浮点，1 为 Q4_Q16_V1 |
| 63 | 零填充 |

附件标志从低到高为四样本、启用第二颜色目标、颜色使用 sRGB 编码，位 3–31 须为零。关闭第二目标时其地址须为零。CLEAR 和 RENDER 必须分别提供对应附件布局。

光栅状态表由九个小端 uint32 组成，依次为 scissor_enable、x、y、width、height、src_rgb、dst_rgb、src_alpha、dst_alpha。矩形以左上为原点，右、下边界排除。enable 取 0 或 1，blend 因子取 0–9，编号依次对应 ZERO、ONE、SRC_ALPHA、INV_SRC_ALPHA、DST_ALPHA、INV_DST_ALPHA、SRC_COLOR、INV_SRC_COLOR、DST_COLOR、INV_DST_COLOR。表内混合因子仅在字 36 开启混合时生效。


字 57 是顶点布局表的起始字节地址（零使用默认布局），字 58 是自定义 FP32 分量数（0–16）。表内偏移相对于每条顶点记录。详细格式、缺省值及属性范围校验见[顶点布局](geom.md#资源布局裁剪与视口)。

cull_mode：0=NONE、1=FRONT、2=BACK；compare：0=NEVER、1=LESS、2=EQUAL、
3=LESS_EQUAL、4=GREATER、5=NOT_EQUAL、6=GREATER_EQUAL、7=ALWAYS。
stencil操作：0=KEEP、1=ZERO、2=REPLACE、3=INCREMENT_CLAMP、4=DECREMENT_CLAMP、
5=INVERT、6=INCREMENT_WRAP、7=DECREMENT_WRAP。filter：0=NEAREST、1=BILINEAR；
wrap：0=CLAMP、1=REPEAT。

CP解析后协调Geom获取、Shader顶点程序、Geom裁剪/视口、Rast覆盖/插值、
Tex采样、Shader片元程序和Rop输出，顺序与整机数据流见[GPU](gpu.md)。

ClearCommand/DrawCommand/ComputeCommand目前仅是CP解码后的临时C++状态
记录，不是Gpu提交API或CP队列元素，不允许memcpy作为包编码。测试侧的
variant和编码器只是主机造包辅助，功能DUT只接收buffer地址和长度。

## 错误与提交边界

未知操作码、版本/长度不符、非法bool/枚举/模板高位、非零保留字都在当前
包产生模块副作用前拒绝。资源和程序错误在对应执行阶段发生。CP捕获异常
后锁存命令字节地址和原因，游标停留在失败包；后续包及后续buffer不执行。
修补内存不会解除错误，重复run仍返回同一错误，需reset后重新提交。

已消费包和失败包已经提交的指令/片元副作用不回滚。命令包预算耗尽可
续跑；Shader预算耗尽是执行错误，不能作为缓冲区暂停恢复。reset丢弃队列
与游标、解除错误，Gpu.reset同时清理Shader状态和统计，保留Mem。

## 解析器与调度器的具体执行算法

包解析分为范围接纳、语法解码、模块执行三个边界。enqueue只验证buffer
范围后保存`{address,byte_size,offset=0}`，不构造ClearCommand/DrawCommand/
ComputeCommand。run每次根据队首address+offset选择待执行包。

```text
run(budget):
  若已有锁存错误：返回该错误，成功包数为0
  当队列非空且本次成功包数 < budget：
    buffer = 队首
    pc = buffer.address + buffer.offset
    words[0..63] = Mem.read(pc+4*i,4)的完整副本
    验证magic、version、固定长度、头保留字
    根据opcode，从word[4]起按格式逐字段解码
    验证bool/枚举/8位字段，以及剩余负载全零
    执行解码结果对应的模块调度流程
    成功后offset += 256，本次成功包数 += 1
    offset == byte_size时移除队首buffer
  返回队列是否为空
任一步解析或执行异常：保留失败包offset，锁存pc和原因并返回
```

PacketReader的cursor每读取一个字段递增1，先检查cursor<64。float用
位模式复制，不作uint32到float的数值转换；例如0x3f800000应得到1.0。
每个opcode先调用finish验证全部零填充，再进入execute_clear/render/compute，
因此坏包不会因为较早字段合法就部分调用执行模块。包内状态是本次读取
快照，执行中对当前命令内存的写入不会改变它；后续包仍独立读取。

### CP图形批次与Shader线程组的区别

CP图形批次最多128调用，自定义Shader资源较小时按阶段实际容量缩小，目的是把大顶点流或片元列表映射到Shader驻留
上下文。Shader内部每warp32线程，调度的是同PC线程组，还可按执行通路
数量进一步划分lane区间。这三者分别是任务批次、SIMT指令组和执行单元
批次，不能将一个CP批次视为一条Shader指令或一个硬件周期。

CP先无分配地验证装配索引，再对完整顶点流分批执行VS，取回全部结果后
才调用Geom.process。shade_vertices/process_fragments分别负责图形ABI装载、
回读及片元提交；寄存器常量见[图形ABI](graphics_abi.md)。
对片元则按三角形建立Rast游标，每次取得最多128个片元并复用缓冲；
绘制开始时取得已验证纹理/附件描述符，纹理及附件内容仍实时访问。
每批先准备所有纹理输入，再执行整个Shader任务，结束后按lane顺序处理
discard和ROP。FS若出错，本批不会向ROP提交，但FS此前成功的全局存储
仍保留。execute_compute按计算包的工作组与预算约定顺序启动各批。

### 游标推进的例子

地址0x1000、长度768的buffer包含三包，地址依次为0x1000、0x1100、0x1200。
run(1)成功后offset=256、pending=1；run(1)再成功后offset=512；第三包
成功后该buffer出队。若第二包失败，第一次成功的写入保留，offset停在256，
第三包及后续buffer均不执行；重试run不会重放第一包，也不会重新解码第二包。
一次run跨多个buffer时预算仍按包累计，不会因buffer切换重置。

实现入口：[cp.cpp](../hw/model/func_model/cp/cp.cpp)。解析每包读取固定64字，
成本O(1)；执行成本取决于包中任务规模，队列空间为提交buffer描述数O(N)。

## 验证与限制

`cp_test_func`验证同一buffer混合操作、包预算续跑不重放、内存读取时机、
坏头/非法字段/保留字、范围拒绝、失败偏移与后续隔离、计算尾warp和reset。
测试编码器独立于生产解码器，位于
[`command_buffer.cpp`](../hw/verif/tests/common/command_buffer.cpp)。
最完整render用例把清除和8次渲染装入一个buffer一次提交。

当前同步执行、固定包大小，无DMA/环形队列、硬件背压、并行引擎、间接包、
信号量或MMIO门铃。本版格式优先保证可解析与可验证，未实现命令压缩。
