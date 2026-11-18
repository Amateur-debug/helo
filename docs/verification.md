# 验证结构

功能模型、性能模型和 RTL 共用 `hw/verif/tests/` 中的 C++ 测试，通过统一的 DUT 接口访问被测对象：

```text
verif/
├── dut/
│   ├── dut.h          # Shader统一测试接口
│   ├── geom_dut.h     # 几何模块接口
│   ├── rast_dut.h     # 光栅模块接口
│   ├── tex_dut.h      # 纹理模块接口
│   ├── rop_dut.h      # 输出模块接口
│   ├── memory_dut.h   # 内存与取指/数据端口接口
│   ├── gpu_dut.h      # GPU/CP整机接口
│   ├── func_dut.cpp   # 功能模型适配
│   ├── perf_dut.cpp   # 性能模型适配
│   └── rtl_dut.cpp    # RTL 仿真适配
└── tests/             # 共用 C++ 测试，与功能模型模块目录对应
    ├── func/           # 功能模型内部资源和生命周期检查
    ├── common/
    ├── gpu/
    ├── cp/
    ├── geom/
    ├── rast/
    ├── shader/
    ├── tex/
    ├── rop/
    └── mem/
```

编译时选择对应的 DUT 实现。测试用例通过统一接口提交操作和检查结果，模型调用、模拟推进及 RTL 信号驱动由适配层处理。功能断言共用，周期和性能断言仅用于支持相应能力的实现。

测试目录与 `hw/model/func_model/` 的模块目录保持对应，例如 `shader/` 模块的测试放在 `hw/verif/tests/shader/`。同一模块的功能行为测试由各DUT实现共用，不按实现复制测试。
`tests/func/`单独检查C++游标/句柄的生命周期、缓存和分配策略，可直接访问功能模型；
这些不是跨实现硬件功能要求，不得将它们混入共用DUT的纯虚接口。

测试文件采用 `<单元名>_test.cpp` 和 `<单元名>_test.h` 命名，例如 `shader/lsu.h` 对应 `hw/verif/tests/shader/lsu_test.cpp` 和 `lsu_test.h`。

## 构建与测试运行

- 项目使用 CMake 管理构建，使用 CTest 作为统一测试运行入口。
- 启用测试需要clang-format 18.1.8；通过PATH查找，或用-DHELO_CLANG_FORMAT=/绝对路径指定。版本固定以避免不同工具产生排版漂移，纯库构建不需要此工具。
- 所有已实现的自动化测试均须注册到 CTest；功能模型、性能模型和 RTL 的测试遵循相同要求。
- 顶层 CMake 配置通过 `include(CTest)` 启用测试支持，测试目标和注册规则置于 `BUILD_TESTING` 条件下。
- 测试可通过 `add_test()` 或测试框架配套的 CMake 发现机制注册。CTest 负责执行和汇总结果，不限定断言框架。
- CTest 测试名称应唯一，并标明 DUT 实现，例如 `lsu_test_func`、`lsu_test_perf` 和 `lsu_test_rtl`；可通过标签按模块和 DUT 筛选。
- 测试失败必须返回非零退出码，并为测试设置合理的超时时间。尚未实现的空测试文件不注册为通过的测试。

## 构建输出目录

- 所有构建产物统一保存在仓库根目录的 `build/` 下，禁止在源码目录内构建。
- 每次构建使用独立目录 `build/<构建名称>_<时间戳>/`。构建名称采用小写 `snake_case`，建议标明 DUT 和配置，例如 `func_debug_20260915_143025`。
- 时间戳使用本地时间，格式为 `YYYYMMDD_HHMMSS`，精确到秒；同名目录已存在时追加序号，不覆盖或复用其他构建批次。
- 同一批次的 CMake 配置、编译和 CTest 执行使用同一个构建目录；新批次创建新目录。
- 可执行文件统一输出到该批次的 `bin/`，库文件输出到 `lib/`，CMake 缓存和中间产物保留在该批次目录内。
- CMake 须显式配置运行时和库输出目录。多配置生成器须按配置隔离，例如 `bin/Debug/`、`lib/Debug/`，避免配置间覆盖。
- `build/` 仅用于本地生成产物，不纳入 Git。测试日志仍输出到仓库根目录 `logs/`，并记录所使用的构建目录。

目录示例：

```text
build/
├── func_debug_20260915_143025/
│   ├── bin/
│   ├── lib/
│   └── CMakeCache.txt
└── rtl_debug_20260915_150010/
    ├── bin/
    ├── lib/
    └── CMakeCache.txt
```

每批次统一使用以下入口（目录名仅为示例）：

```sh
cmake -S . -B build/func_debug_20260915_143025 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/func_debug_20260915_143025
ctest --test-dir build/func_debug_20260915_143025 --output-on-failure
```

## 测试日志

通过 CTest 启动的测试也须遵守根目录 `AGENTS.md` 中的日志约定：每次测试的标准输出、标准错误及运行信息保存到 `logs/<测试名称>_<时间戳>.log`。由测试运行包装器或测试程序落实日志写入，并将真实退出状态传递给 CTest。CTest 自身在构建目录下产生的日志不能替代项目要求的日志。

## Shader 整程序回归

`shader_test_func` 通过统一 DUT 的 `load_program/launch/run/step` 执行机器码，覆盖真实 Shader 顶层的取指、解码、缓冲、发射、整数/浮点/访存执行、写回与路径控制。整程序用例不调用用于独立单元测试的手工发射或写回接口。

- 混合程序：33 条机器码、65 个线程跨三个 warp；整数乘除/取余、不同迭代次数循环、HELO FP32 转换/FMA/动态舍入、同 warp 条件分支、结果和浮点状态写回内存。独立整数公式校验结果，检查尾 warp 未启用线程哨兵，并比较一次运行与每次 1–7 指令组续跑的完整寄存器状态。
- 子程序：12 条机器码、35 个线程；非零入口、JAL/JALR 返回、存储后加载及整数/浮点共用寄存器。重复启动同一对象验证寄存器和浮点状态隔离。
- 故障恢复：9 条机器码、32 个线程；五条成功指令后，第六条存储在最后一个线程遇到越界。检查已完成结果保留、失败指令不部分提交、后续指令不执行、错误锁存以及复位后重跑。

指令和数据均经 `icache/dcache` 的直通接口访问 C++ `Mem`。日志输出程序名称、机器码数量、线程数及实际执行的指令组数；该步数不表示硬件周期。

## HELO ISA 与任务资源验证

共用 DUT 使用64位程序字，独立编码器覆盖七种格式、保留位和立即数边界。任务启动可声明1–256个逻辑寄存器（含r0），硬件分别配置 warp、thread 和 GPR 字节预算。资源测试覆盖三种上限、任务重新配置、r255、越过任务寄存器范围及拒绝启动时旧任务保持；整程序覆盖 CSR、屏障、分歧、FP32 和访存。

执行通路配置通过统一DUT传入。shader测试覆盖1/3/8/16/32及混合通路数量，核对跨warp和尾warp结果及批次统计；检查最后一批存储越界、非法动态舍入和未对齐跳转不部分提交，并检查配置范围与统计复位。

## 图形与存储共用验证

图形共用接口按职责拆为GeomDut、RastDut、TexDut、RopDut，分别由
func_geom_dut.cpp、func_rast_dut.cpp、func_tex_dut.cpp、func_rop_dut.cpp适配。
Shader、存储、整机继续使用Dut、MemoryDut、GpuDut。每个适配器只持有所属
模块需要的对象；新增一种RTL单元无需实现其他图形模块的方法。

共用测试不直接调用生产算法，公共记录来自hw/include/helo；机器码与命令
编码器保持独立。功能模型的游标、Sampler和Target句柄由func_resources_test
直接验证，不要求RTL暴露C++对象。HELO_DUT=rtl注册存储共用测试和
Shader子集差分/整程序验证、Rast覆盖单元的受限输入域差分、ROP打包
RGBA8混色片元差分及受限定点三角形绘制差分；
不注册尚未实现的完整图形/CP/整机RTL测试。

| CTest 名称 | 验证范围 |
| --- | --- |
| `geom_test_func` | 顶点/索引流、装配、六平面裁剪及属性、视口、朝向和退化图元 |
| `rast_test_func` | 共享边只覆盖一次、像素中心、包围盒、绕序、线性深度和透视属性 |
| `tex_test_func` | 最近点/双线性、clamp/repeat、纹理边界与非法资源 |
| `rop_test_func` | RGBA量化、source-over、深度比较、模板操作/掩码、附件验证和丢弃 |
| `mem_test_func` | 小端读写、宽度/对齐/地址边界、批量装载失败不部分写 |
| `icache_test_func` | 64位取指、入口对齐、指令范围和代码更新可见性 |
| `dcache_test_func` | 窄访存、共享底层内存与取指/数据端口一致性 |
| `gpu_test_func` | 真实VS/FS程序、纹理、深度顺序、discard、索引及多批次ABI |
| `cp_test_func` | buffer内包预算续跑、内存解析、坏包校验、错误锁存/复位及计算执行 |
| `gpu_render_test_func` | 单buffer包含清除和8个渲染包，经真实矩阵VS/纹理FS输出PNG |

整机组成与命令执行见 [GPU顶层](gpu.md)，图形寄存器协议见 [图形ABI](graphics_abi.md)。
最终图片保存在当前构建批次的 `artifacts/gpu_scene.png`；像素只能来自
GPU帧缓冲读回，PNG writer只负责编码。端到端测试检查场景覆盖和Shader
执行统计；单元和GPU行为测试负责独立数值断言，避免仅用“生成了文件”
作为功能正确性的依据。

```sh
ctest --test-dir "$build_dir" --output-on-failure
ctest --test-dir "$build_dir" -R '^gpu_render_test_func$' --output-on-failure
```

渲染测试设定更长的运行超时，并串行生成该构建目录中的单个PNG。
每项测试与整体CTest运行都应保留真实退出状态；单项日志由统一包装器
自动写入仓库根目录 `logs/`。


## 测试程序与入口索引

`HELO_DUT=func`当前CTest注册26项（每项包含多个用例）：Shader 14项、图形单元4项、存储3项、GPU/CP 2项、
完整渲染1项、功能模型内部资源1项、代码格式检查1项。多个子单元通过所属模块测试覆盖，空骨架文件不计作独立测试。

| 可执行程序 | selector | 对应接口 |
| --- | --- | --- |
| `shader_tests_func` | shader、iu、fpu、lsu、wcu、regfile、scoreboard、idu、ibuffer、isu、wbu、ifu、wmu、wsu | `Dut` |
| `graphics_tests_func` | geom、rast、tex、rop | GeomDut、RastDut、TexDut、RopDut |
| `memory_tests_func` | mem、icache、dcache | `MemoryDut` |
| `gpu_tests_func` | gpu、cp、render | `GpuDut` |
| `func_resources_tests` | 无参数 | 直接验证功能模型内部资源 |
| format_cpp.py | 默认只读检查 | 固定版本clang-format |

常规单元包装器超时30秒，CTest超时40秒；gpu/cp分别为120/130秒，render
为180/190秒。render设置RUN_SERIAL；多配置构建共享同一
`artifacts/gpu_scene.png`，不要在同一构建目录并发启动不同配置的渲染。
测试命令通过CTest启动才能自动获得项目日志，不建议直接裸跑可执行程序。

## 从源码复现完整 PNG

先安装可在PATH调用的CMake 3.20+（含CTest）、C/C++编译器、Python 3及
clang-format 18.1.8。工具不在PATH时为下面的配置命令添加
`-DHELO_CLANG_FORMAT=/绝对路径/clang-format`。
以下命令在仓库根目录运行，使用独立构建目录，配置或编译失败即停止：

```sh
set -e
build_base="build/gpu_debug_$(date +%Y%m%d_%H%M%S)"
build_dir="$build_base"
build_index=0
while [ -e "$build_dir" ]; do
    build_index=$((build_index + 1))
    build_dir="${build_base}_${build_index}"
done
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug -DHELO_DUT=func
cmake --build "$build_dir" --config Debug --parallel
python3 scripts/run_test.py --name gpu_full_ctest_func --dut func \
    --logs logs --build "$build_dir" --timeout 300 -- \
    ctest --test-dir "$build_dir" -C Debug --output-on-failure
```

结果为 `$build_dir/artifacts/gpu_scene.png`，整体与单项日志均写入 `logs/`。
只重跑图片时使用以下命令，单项日志仍自动保存：

```sh
ctest --test-dir "$build_dir" -C Debug -R '^gpu_render_test_func$' --output-on-failure
```

无需PNG库、系统图形API或初始化Spike子模块。

完整场景与行为断言入口是
[`gpu_test.cpp`](../hw/verif/tests/gpu/gpu_test.cpp)，
[`png_writer.cpp`](../hw/verif/tests/common/png_writer.cpp)只编码附件字节。
场景使用38条VS指令执行MVP矩阵加载和浮点乘加，5条FS指令完成纹理调制。
包括4个立方体（其中1个被大立方体完全遮挡）、地面、网格、透明轨道和玻璃。
测试同时断言工作负载、颜色多样性与遮挡，具体像素和尾批次正确性由
`gpu_test_func`及各图形单元测试验证。

## Verilator RTL 内存、Shader及图形子集回归

RTL源码统一采用Verilog-2005（`.v`，共享常量头文件为`.vh`），不使用
SystemVerilog；Verilator构建和lint均显式指定`--language 1364-2005`。
配置时发现`hw/rtl/`下的`.sv`文件会直接报错。

配置独立构建批次时指定`-DHELO_DUT=rtl`，需要Verilator、CMake、C++17、
Python3与项目指定版本的clang-format。`cmake --build`生成
`bin/memory_tests_rtl`、Shader单元、单线程核心及Rast覆盖、ROP、平面绘制的Verilator可执行程序。
CTest运行内存共用测试`mem_test_rtl`、`icache_test_rtl`、`dcache_test_rtl`，
差分测试`memory_compare_test_rtl`，以及IDU/IU/WCU/Regfile/LSU、
机器码lane和单线程整程序差分；各RTL模块同时用`-Wall`进行lint，
`cpp_style_test`检查C++排版。
前三项复用功能模型的同一组MemoryDut断言。差分用固定种子对比4096步
主机/数据读写、取指与非法请求，并直接驱动同周期双端口写入和无效写入；
LSU测试对比全部八种load/store及负偏移、未对齐和越界地址；单线程核心逐条
对比C++ Shader的PC，并在完成时核对寄存器及共享内存，还覆盖故障锁存和重启。
`rast_coverage_compare_test_rtl`仅在Q4精确网格输入上检查片元坐标、
边值/面积、共享边、随机三角形、背压和复位；不单独验证属性插值或完整图形管线。
`rop_compare_test_rtl`检查打包RGBA8、深度/模板状态矩阵、非法附件布局
和混色/全附件差分；`flat_draw_compare_test_rtl`检查Q16深度、透视UV与
RGBA8颜色、纹理nearest/clamp/repeat、颜色调制及覆盖到ROP写回、遮挡、非法配置、
故障锁存与复位恢复。两个顶层均有独立`-Wall` lint。
测试日志由同一包装器写入`logs/`。例如：

```sh
cmake -S . -B build/rtl_memory_YYYYMMDD_HHMMSS -DHELO_DUT=rtl \
    -DHELO_CLANG_FORMAT=/path/to/clang-format-18.1.8
cmake --build build/rtl_memory_YYYYMMDD_HHMMSS
ctest --test-dir build/rtl_memory_YYYYMMDD_HHMMSS --output-on-failure
```

上面目录名中的时间戳须替换为实际运行环境的本地时间且避免重名。
目前`HELO_DUT=rtl`并不表示SIMT Shader、GPU顶层、CP及图形单元已有完整RTL；
受限核心不运行图形Shader ABI或多线程程序，详见[Shader RTL范围](shader.md#rtl-当前实现范围)。
图形阶段现状及数值输入域见[图形RTL实施](rtl_graphics_plan.md)。

### 已完成的验证基线（2026-09-15）

Debug和ASan/UBSan分别通过24/24测试，渲染像素完全一致。该结果对应此次
实现的实际验证记录，不代表未来修改后自动通过，也不是跨平台位精确承诺。

| 项目 | 本次实际结果 |
| --- | --- |
| 图片 | 800×600 RGBA8，1001种RGB |
| 完成绘制 / 顶点调用 | 8 / 720 |
| 光栅三角形 / 片元调用 | 407 / 544634 |
| 通过ROP / Shader指令组 | 463793 / 87338 |
| GPU多批次独立测试 | 132顶点、23232片元，全局编号与整个剩余哨兵区校验 |
| 图片完整性 | 独立验证PNG块CRC、zlib解压校验、scanline布局和两种构建像素一致性 |

ASan/UBSan构建使用C和C++选项
`-fsanitize=address,undefined -fno-omit-frame-pointer`及同名链接选项；
启用halt_on_error，未启用LeakSanitizer。新增代码及测试通过编译检查；
原SoftFloat头文件在AppleClang下存在 `_Thread_local` C11扩展警告。
历史日志和图片位于本地 `logs/` 与 `build/`，不随源码提交。


## Command buffer 验证入口

GpuDut的设备提交接口为submit_buffer(address,byte_size)，适配器不接收
C++操作variant。测试侧submit/submit_commands仅用独立编码器将状态变为
内存包，再调用统一入口；编码实现位于tests/common/command_buffer.cpp。
CP生产解码器不与测试编码器共享字段读取/构造逻辑。命令暂存区由GpuConfig的
实际内存容量确定，取末端至多2MiB且不超过容量八分之一，向下对齐256字节；
资源布局需避开该测试暂存区。默认16MiB设备仍使用从0xe00000开始的区域。

cp_test在同一buffer混合CLEAR/COMPUTE/CLEAR，核对逐包预算续跑、已消费
包不重放，以及未消费包执行时读取。坏magic/opcode/length/reserved、
bool高值、模板高位及包尾非零逐一验证失败偏移、零当前包副作用和后续
隔离；另验证未对齐/截断/空/溢出/越界提交不入队。完整render用例把所有
清除和绘制状态编码到单个buffer一次提交，比较实际Shader与附件结果。

## 公共接口、数值与优化回归

`protocol_headers_check`是构建期检查，只提供hw/include与verif包含路径，
不链接功能模型；任一DUT公共头若重新包含私有模型头，构建立即失败。
它不注册为空运行测试。执行结果比较使用[运行状态协议](execution.md)，
属性与像素比较使用[数值契约](numerics.md)，测试编码器继续独立实现。

- rast：种子0x48454c4f，64个1/16网格矩形；两种对角线、反绕序、裁界，
  精确检查共享边及矩形外边界覆盖。
- geom：种子0x434c4950，96组尺度最低到2^-100的齐次裁剪，并拒绝倒数溢出的极小W。
- cp：种子0x43504255，96个三类命令保留字变异，检查失败地址、前缀提交和后续隔离。
- shader/cp：区分零预算、暂停、完成、锁存故障；校验嵌套Shader的warp、mask、PC、机器码。
- func_resources_test：以1/3/32/128/1024容量验证游标、耗尽和互相隔离；检查Sampler/Target描述符快照、数据可见性及动态输入验证。
- gpu：默认配置下第二个片元批次陷阱仅保留前128个成功写回；自定义64线程核心与默认设备渲染结果相同，并检查65线程计算任务拒绝、64线程接纳，以及64KiB/32MiB内存配置。
- regfile：相同布局重复launch清空GPR/FCSR，显式reset清空架构状态但保留外部内存；旧任务在非法launch时保持。

随机性质用例失败时附带种子与trial编号。完整render还运行独立32×32解析
场景：四纹素底图、左半屏半透明蓝色、被深度遮挡的远处黄色图元；期望值由
四个区域的解析公式给出，全部RGBA字节及深度逐一比较，不从当前模型输出
生成期望。实际图保存为gpu_scene.png.numeric.png；颜色失败时生成
.expected.png和.diff.png。800×600主场景另检查顶部背景区域，保留统计断言。

优化前后完整PNG比较属于重构回归证据，不能替代上述独立数值断言。更改
输出顺序或失败提交边界时应先更新协议并新增针对性用例，不只更新图片基线。

## 自动风格检查

`cpp_style_test`随完整CTest运行；`cmake --build <构建目录> --target format-check`
执行相同只读检查，输出均保存至logs。`scripts/format_cpp.py --write`用于显式
整理代码，排除第三方SoftFloat。具体排版规则见[C++规范](coding-style/cpp.md)。
格式检查失败不能通过重新生成测试期望规避；先格式化，再验证功能。
