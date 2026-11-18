# 功能模型结构

`hw/model/func_model/` 按硬件职责平铺组织，模块目录统一使用小写：

```text
func_model/
├── common/  # 公共类型与数值运算
├── gpu/     # 顶层集成
├── cp/      # 命令处理与任务分发
├── geom/    # 几何处理
├── rast/    # 光栅化
├── shader/  # Shader 执行核心
├── tex/     # 纹理采样
├── rop/     # 深度、模板、混合与写回
└── mem/     # 存储系统
```

模块内部按单元平铺 `.h` 和 `.cpp` 文件，模块目录统一使用小写。

Shader 的执行模型、单元职责、指令流程及缓存接口见 [Shader 架构](shader.md)。

## 模块与源码入口

| 模块 | 源码单元 | 当前职责 |
| --- | --- | --- |
| [common](common.md) | `types.h`、`config.h`、`softfloat/` | 图形记录、视口/剔除状态、Shader FP32数值后端 |
| [gpu](gpu.md) | `gpu` | 按值持有Mem/Cp/Geom/Rast/Shader/Tex/Rop，连接所有阶段及读回 |
| [cp](cp.md) | `command.h`、`cp` | 内存命令包解析、顺序调度、预算和错误锁存 |
| [geom](geom.md) | `vfetch`、`pa`、`clip`、`vp`、`geom` | 顶点/索引获取、三角形装配、齐次裁剪、视口和剔除 |
| [rast](rast.md) | `setup`、`interp`、`rast` | 三角形设置、像素覆盖、透视属性插值 |
| [shader](shader.md) | `shader`及各执行/调度单元 | HELO SIMT32机器码执行；细分职责见Shader文档 |
| [tex](tex.md) | `tex` | RGBA8纹理寻址和最近点/双线性过滤 |
| [rop](rop.md) | `zrop`、`crop`、`rop` | 深度/模板、量化/混合及附件写回 |
| [mem](mem.md) | `mem`、`icache`、`dcache` | 统一小端字节存储及取指/数据直通访问 |

模块内部同样采用组合对象：Geom持有Vfetch/Pa/Clip/Vp，Rast持有Setup/Interp，
Rop持有Zrop/Crop。具体所有权、连接和生命周期见
[GPU对象层次](gpu.md#对象层次与生命周期)。

纯协议/记录位于`hw/include/helo/`及`helo`命名空间，由CMake接口库
`helo_protocol`提供；GPU、Shader等实现对象仍位于`helo::func`。
原common/types.h、config.h和cp/command.h仅作包含转发。GPU对外入口为
[`gpu/gpu.h`](../hw/model/func_model/gpu/gpu.h)，命令字段定义在
[`helo/commands.h`](../hw/include/helo/commands.h)。Gpu只提交内存缓冲区范围，命令包二进制格式和游标语义见[CP](cp.md)；
尚未定义MMIO门铃或系统驱动接口。
`perf_model/`与驱动未完成；RTL只覆盖内存及部分Shader执行路径，不能替代本节
描述的功能模型整机实现。RTL当前范围见[Shader](shader.md#rtl-当前实现范围)。

## 构建入口

仓库根目录的 CMake 构建入口生成 `helo_func_model` 静态库。编译
`gpu/`、`cp/`、`geom/`、`rast/`、`shader/`、`tex/`、`rop/` 和 `mem/`。
整机架构见 [GPU顶层架构](gpu.md)，每个一级模块的同名文档见
[文档导航](README.md)，跨模块着色器协议见 [图形Shader ABI](graphics_abi.md)。
默认启用共用 DUT 验证测试，配置 `-DBUILD_TESTING=OFF` 可仅构建模型库。
构建批次目录、产物布局与 CTest 日志要求见 [验证规范](verification.md)。

FP32 数值后端使用 `common/softfloat/` 中随仓库保存的 Berkeley SoftFloat
源码副本。`cmake/softfloat.cmake` 编译 HELO FP32 所需的 C 源码，生成
`helo_softfloat` 静态库并链接到模型；配置、编译和测试不依赖 Spike 子模块。
源码来源、版本与许可证说明见 [SoftFloat 来源记录](softfloat.md)。
舍入与异常状态由 Shader 浮点单元接入，指令约定见 [Shader 架构](shader.md)。

在仓库根目录运行（需要 CMake 3.20+、C11 和 C++17 编译器；启用测试时还需要 Python 3 与 clang-format 18.1.8）：

```sh
build_dir="build/func_debug_$(date +%Y%m%d_%H%M%S)"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug -DHELO_DUT=func
cmake --build "$build_dir"
ctest --test-dir "$build_dir" --output-on-failure
```

若目录已存在，应追加序号以创建新批次。`HELO_DUT=func`运行完整功能模型测试；
`HELO_DUT=rtl`运行已实现的内存和Shader子集RTL测试，不运行图形/CP整机测试；
选择尚未实现的`perf`会在配置阶段报错。
测试通过统一 DUT
接口运行，单项测试可用 CTest 的 `-R` 过滤；每次执行均由
`scripts/run_test.py` 将输出和运行信息保存到根目录 `logs/`。

## 规范与实现策略

- 指令编码和架构语义：[ISA](isa.md)。
- 图形寄存器输入输出：[图形Shader ABI](graphics_abi.md)。
- 命令包二进制格式：[CP](cp.md#命令包格式-v1)。
- 状态、预算与故障字段：[运行状态协议](execution.md)。
- 固定功能精度与跨实现比较：[数值契约](numerics.md)。

各模块文档解释算法和当前C++策略。128调用分批、游标、向量和资源句柄属于
实现组织，不定义RTL流水级或时钟周期；修改它们仍须保持可观察顺序与提交边界。
公共记录从func迁移至helo，结果接口改用m_status/m_fault及completed()；
仓库内调用方已同步迁移，外部调用方需要按上述协议更新。

Gpu与Shader构造使用helo/config.h中的具名配置；测试通过DutConfig或GpuConfig
传入。自动排版及检查入口见[C++规范](coding-style/cpp.md)。
