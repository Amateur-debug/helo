# 图形Shader ABI

该ABI由GPU内部Cp解析RENDER包后，在Shader普通launch零初始化完成与执行
机器码之间显式写入寄存器。Shader是GPU内部执行模块，不负责解析命令包。
ABI不新增HELO指令，不改变普通计算launch初始GPR/FCSR为零的规则。

| 阶段 | 寄存器 | 输入和输出 |
| --- | --- | --- |
| 顶点 | r1–r4 | position.xyzw输入；结束后作为齐次裁剪位置 |
| 顶点 | r5–r8 | color.rgba输入；结束后作为待插值颜色 |
| 顶点 | r9–r10 | uv输入；结束后作为待插值纹理坐标 |
| 顶点 | r11 | uint32顶点流全局编号输入；改写不影响装配索引 |
| 片元 | r1–r4 | 透视插值颜色；结束后作为ROP源颜色 |
| 片元 | r5–r8 | 固定功能采样RGBA输入，关闭纹理时为白色 |
| 片元 | r9–r10 | 透视校正uv |
| 片元 | r11–r12 | float像素中心x/y |
| 片元 | r13 | float窗口深度；允许FS覆盖输出深度 |
| 片元 | r14 | 初值0；结束时非零则丢弃，不写颜色/深度/模板 |
| 片元 | r15 | uint32累计片元调用编号，Gpu.reset后清零，溢出按低32位传入 |

其他寄存器初值零。VS声明至少12寄存器，FS至少16，上限256。全部调用必须
执行EXIT，错误或预算耗尽使CP锁存失败。关闭VS则顶点直通；关闭FS则CP
逐分量将插值颜色乘采样颜色。启用FS时输出完全由程序决定。

Cp每批取128与阶段寄存器声明下驻留容量的较小值；默认配置为128。片元批次不跨三角形，每批重新launch，GPR/FCSR不跨批
保留。线程CSR编号/count对应当前批次，不能当作整次渲染的编号；全局编号
使用上述r11/r15。参数地址来自命令包ShaderProgram的param_base，指令预算
按批独立计算。语法与执行提交边界分别见[CP](cp.md)和[GPU](gpu.md)。

寄存器常量定义在[`helo/graphics_abi.h`](../hw/include/helo/graphics_abi.h)。
寄存器编号和输入输出是协议；每批至多128是当前CP实现策略，不属于ISA编码。
共享数据记录位于[`helo/graphics.h`](../hw/include/helo/graphics.h)。
