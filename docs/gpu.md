# GPU 顶层架构

## 整机与内部模块

GPU是整机，Shader是GPU内部的可编程SIMT执行模块。功能模型的Gpu class
持有Mem、Cp、Geom、Rast、Shader、Tex、Rop，负责构造连接、统一提交和
结果读回；命令解析、任务调度和阶段间数据传递由内部Cp负责。
源码：[`gpu.h`](../hw/model/func_model/gpu/gpu.h)、
[`gpu.cpp`](../hw/model/func_model/gpu/gpu.cpp)。

本页整机组成目前由C++功能模型实现。RTL已具备共享内存、受限的单线程
Shader核心，以及Q4覆盖→Q16深度/透视UV和RGBA8颜色插值→RGBA8最近点纹理→
ROP深度/混色→共享内存的受限绘制闭环；尚无CP、Geom、完整Rast/Tex/ROP
或图形Shader与GPU顶层连线，
`HELO_DUT=rtl`不能运行本页描述的渲染/计算命令包。
图形RTL的阶段接口及推进次序见[实施计划](rtl_graphics_plan.md)。

```text
主机上传命令包和资源 → GPU统一Mem
主机submit(buffer地址,字节长度) → GPU内部Cp
                                   │ 读取、解析、校验包
                                   ├─ 清除 → Rop → Mem
                                   ├─ 计算 → Shader → Mem
                                   └─ 渲染 → Geom获取 → Shader执行顶点程序
                                              → Geom装配/裁剪/视口
                                              → Rast覆盖/插值 → Tex
                                              → Shader执行片元程序
                                              → Rop → Mem
主机read_color ← 颜色附件
```

清除、渲染和计算是同一command buffer中的操作码，共用提交、解析、预算和
错误路径；GPU对外没有独立draw/dispatch/clear接口。一个buffer可以混合
多个操作，CP按顺序消费。二进制包格式见[CP](cp.md)。

## 对象层次与生命周期

```text
Gpu
├── Mem
├── Geom
│   ├── Vfetch → 引用Gpu.Mem
│   ├── Pa
│   ├── Clip
│   └── Vp
├── Rast
│   ├── Setup
│   └── Interp
├── Shader
│   ├── Icache / Dcache → 引用Gpu.Mem
│   └── 调度、发射、寄存器、执行和写回单元
├── Tex → 引用Gpu.Mem
├── Rop → 引用Gpu.Mem
│   ├── Zrop
│   └── Crop
└── Cp → 引用以上模块与GpuStats，统一解析和调度
```

所有执行模块均为class，组合模块按值持有子单元；配置/记录使用struct，
common为公共源码目录而非运行时硬件实例。Gpu先构造Mem和各执行模块及
统计，再构造Cp并传入引用；Cp最先析构，Mem最后析构。Gpu禁止复制/移动，
确保内部引用稳定。CP不拥有另一套执行模块，不持有Gpu回调引用。

| 模块 | 职责与说明 |
| --- | --- |
| [Cp](cp.md) | 读取command buffer、校验解码、保持游标、协调执行并锁存错误 |
| [Geom](geom.md) | 顶点/索引获取、三角形装配、六平面齐次裁剪、视口和剔除 |
| [Rast](rast.md) | top-left覆盖、像素中心、深度及透视属性插值 |
| [Shader](shader.md) | 顺序复用同一SIMT核心执行顶点、片元和计算程序 |
| [Tex](tex.md) | 固定功能RGBA8 nearest/bilinear采样 |
| [Rop](rop.md) | 深度/模板测试、颜色量化/混合和附件写回 |
| [Mem](mem.md) | 命令包、代码、顶点、纹理和附件的统一小端字节存储 |
| [common](common.md) | 共享类型、配置和Shader FP32数值后端 |

## 统一对外接口

| 接口 | 行为 |
| --- | --- |
| Gpu(config={}) | 用GpuConfig建立内存及其ShaderConfig指定的核心，默认配置保持不变 |
| memory() | 上传/读取命令字节、程序和资源，提供const读取重载 |
| submit(address,byte_size) | 交CP检查buffer范围并入队，不在提交时解析负载 |
| run(max_commands=100000) | 由CP按命令包数推进，不按buffer数计预算 |
| pending() | 返回未完成buffer数，包含失败buffer |
| stats() | 返回由CP更新的GpuStats，只统计实际执行 |
| reset() | 清除CP队列/游标/错误、Shader状态及统计，保留内存和对象连接 |
| read_color(framebuffer) | 读回顶部行优先紧密RGBA8字节，不执行或等待队列 |

运行结果使用公共SubmitResult，状态与故障字段见[运行状态协议](execution.md)。
CP的包预算耗尽可续跑；实际故障锁存并停在失败包，需reset后重新提交。

GpuConfig包含m_memory_bytes（默认16MiB）与m_shader。Shader默认8warp、
256线程槽、261120字节GPR、每类32条通路；顶层完整传递配置，非法配置在
构造时拒绝。图形批次取128与当前阶段寄存器声明下驻留容量的较小值，
默认仍为128；计算包要求整个任务
同时驻留。调用同步完成，不模拟多阶段并行或时钟周期，也不支持未经外部
同步的同一Gpu对象并发访问。

## CP调度的完整渲染流程

1. CP读取当前包的完整快照，校验头、操作码、字段编码和零填充，再构造执行状态。
2. 验证附件、启用纹理、程序描述和资源范围，通过Geom获取顶点与索引并验证装配。
3. 启用VS时按最多128调用分批，由CP初始化寄存器、启动Shader并读回position/color/uv；关闭VS则直通。
4. CP调用Geom.process，完成Pa装配、Clip六平面裁剪、Vp透视除法和视口映射、朝向剔除。
5. CP用Rast游标逐批获取最多128个片元；批次不跨三角形，处理完成后复用缓冲。
6. 启用纹理时调用Tex，以采样颜色及插值属性初始化片元输入；关闭纹理使用白色。启用FS时执行真实Shader机器码，否则默认将插值颜色乘采样颜色。
7. FS输出r14非零时丢弃，其余片元交Rop执行深度/模板和混合写回。全部图元完成后才推进当前命令包游标。

VS对顶点流每条记录执行一次，包括未被索引引用的记录。显式空索引包仍
做资源和程序描述检查及顶点获取，然后完成，不执行VS/FS。深度/模板测试
位于FS之后，隐藏片元也执行FS。图形寄存器、编号和分批规则见
[Shader图形ABI](graphics_abi.md)。

几何、光栅、采样和混合遵循[图形数值契约](numerics.md)，Shader FP32使用SoftFloat并执行
现有HELO v0.1机器码。command buffer格式不增加Shader指令。

## 存储、状态与提交边界

所有地址均为32位字节地址；命令buffer基址4字节对齐，长度为非零256倍数；
机器码入口8字节对齐；其他资源对齐、格式和验证见各模块。主机负责资源
分配及布局；ROP检查附件重叠，但不全局禁止代码/命令/资源地址别名。
未消费包按执行时的Mem内容读取；调用者应在完成前保持命令内容稳定。

当前包的语法校验全部完成后才执行，不会因为包尾保留字段错误而提前
产生片元副作用。但资源/指令错误仍可能在执行中发生；已完成的指令、
图元或片元不回滚，整包/整buffer不保证事务性。失败时游标停在该包，
此前成功包不会重放，后续包不执行。Shader仍保持自身单指令组提交边界。

固定功能模块没有跨命令流水状态，reset无需重建它们。附件不会自动清空，
需要通过buffer中的CLEAR包清除。

## 整机数据流中的记录、存储与所有权

| 流程边界 | 传递数据 | 当前模型保存位置 |
| --- | --- | --- |
| 主机 → CP | buffer地址/长度及256字节命令包 | 描述在CP队列，原包在Mem，当前包在解析器快照 |
| Mem → Geom → CP | Vertex向量及索引向量 | 当前RENDER处理过程中的临时向量 |
| CP → Shader | entry/count/param/register_count及图形输入 | Shader的Warp、Regfile、FCSR |
| Shader → CP → Geom | 着色后position/color/uv | 原顶点向量中的对应记录 |
| Geom → CP → Rast | ScreenTriangle向量 | 当前RENDER局部结果 |
| Rast → CP → Tex/Shader | 至多128个Fragment与采样Color | CP复用的批次缓冲及Shader输入寄存器 |
| CP → Rop → Mem | 片元颜色/深度与状态 | 颜色、深度、模板附件 |
| Mem → 主机 | 按行RGBA8字节向量 | read_color返回值，后续由测试编码PNG |

这些边界是同步C++调用和数据记录，不是已经实现的硬件FIFO、valid/ready
或流水寄存器。CP引用Gpu已持有的模块并调用其方法，不复制模块，也不在
每个任务中临时重建模块。临时数据记录的生命周期通常短于模块实例。

## 一个像素贯穿整机的算例

主机提交同一buffer中的CLEAR和RENDER。CLEAR将4×4颜色附件清为黑色，
深度清为1。RENDER使用三个裁剪空间顶点(-1,1,0,1)、(1,1,0,1)、
(-1,-1,0,1)，颜色依次为红、绿、蓝，关闭VS/FS与纹理，视口(0,0,4,4)。

1. CP解析包，Geom获取记录；三顶点均在裁剪范围内。
2. Vp映射到屏幕(0,0,0.5)、(4,0,0.5)、(0,4,0.5)，inv_w均为1。
3. Rast在像素中心(0.5,0.5)得到权重(0.75,0.125,0.125)，深度0.5，颜色(0.75,0.125,0.125,1)。
4. CP提供白色纹理输入，关闭FS的默认乘法保持该颜色。
5. Rop的LESS比较0.5<1通过，写深度0.5；量化颜色为(191,32,32,255)，小端字为0xff2020bf。
6. 第二命令包成功后CP推进游标，read_color读取该像素的四字节bf 20 20 ff。

启用VS/FS时，相应步骤改由GPU内部Shader执行真实机器码，其他固定功能
阶段沿用相同接口。该算例说明数据如何流动，不是一个单独新增的注册测试。

## 完成条件与三级提交边界

GPU层的完成条件是CP提交队列为空；CP一包成功的条件是其内部任务全部
完成；Shader任务成功要求所有实际线程EXIT。Shader一条指令组完成又包括
所有有效lane运算成功、WBU写回及WCU路径更新。
因此“一个lane结果算完”“一个warp退出”“一个像素写出”都不等于GPU
任务完成。包语法校验、Shader指令组提交和ROP片元提交各自提供局部保证，
整条buffer不提供跨包事务回滚。

## 统计、验证与限制

GpuStats保存完成渲染/计算包数、顶点/片元调用、光栅三角形、通过ROP数和
成功Shader指令组数；失败任务可贡献已经完成阶段的计数。这些计数不是周期。

`gpu_test_func`经真实命令buffer验证VS/FS、索引、纹理、深度顺序、discard
和多批次；`cp_test_func`验证包解析与恢复；`gpu_render_test_func`一次提交
包含清除和8个渲染包的完整场景buffer，生成800×600 PNG。PNG writer只编码
实际颜色附件。复现入口见[验证规范](verification.md#从源码复现完整-png)。

这是基础三角形功能模型，不是Vulkan/OpenGL兼容实现。目前无MSAA、MRT、
mipmap/LOD、各向异性、片元导数、动态TEX、原子/共享内存或缓存时序模型，
未定义SFU指令仍拒绝；命令引擎也不模拟DMA、环形队列或并行执行引擎。

## 具名设备配置

```cpp
helo::GpuConfig config;
config.m_memory_bytes = 32 * 1024 * 1024;
config.m_shader.m_max_warps = 2;
config.m_shader.m_max_threads = 64;
config.m_shader.m_regfile_bytes = 8192;
config.m_shader.m_execution.m_fpu_count = 3;
helo::func::Gpu gpu(config);
```

图形阶段分别按VS/FS的寄存器声明计算批次上限；资源不足以接纳一个warp时
在该阶段启动前拒绝。片元批次仍不跨三角形，r11/r15全局编号保持连续。
自定义配置不改变ISA、命令包格式或单指令组提交边界。
