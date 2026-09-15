# 验证结构

功能模型、性能模型和 RTL 共用 `hw/verif/tests/` 中的 C++ 测试，通过统一的 DUT 接口访问被测对象：

```text
verif/
├── dut/
│   ├── dut.h          # 统一测试接口
│   ├── func_dut.cpp   # 功能模型适配
│   ├── perf_dut.cpp   # 性能模型适配
│   └── rtl_dut.cpp    # RTL 仿真适配
└── tests/             # 共用 C++ 测试，与功能模型模块目录对应
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

测试目录与 `hw/model/func_model/` 的模块目录保持对应，例如 `shader/` 模块的测试放在 `hw/verif/tests/shader/`。同一模块的测试由各 DUT 实现共用，不按功能模型、性能模型或 RTL 分设测试目录。

测试文件采用 `<单元名>_test.cpp` 和 `<单元名>_test.h` 命名，例如 `shader/lsu.h` 对应 `hw/verif/tests/shader/lsu_test.cpp` 和 `lsu_test.h`。
