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
