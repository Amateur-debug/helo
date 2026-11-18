# Common 公共定义

## 职责与归属

功能模型common保留数值源码及公共头的转发入口，不是Gpu需要持有的硬件class。
实现无关的图形记录位于`hw/include/helo/`及`helo`命名空间，执行对象仍位于
`helo::func`。公共记录使用struct/array，供[Geom](geom.md)、[Rast](rast.md)、[Tex](tex.md)、
[ROP](rop.md)及统一测试协议使用。

## 图形记录

权威定义见[`helo/graphics.h`](../hw/include/helo/graphics.h)。
原common/types.h与config.h只转发公共头，不再定义另一套类型。

| 类型 | 字段与坐标含义 | 默认值 |
| --- | --- | --- |
| Color | 4个float，RGBA | 类型别名本身不规定初始化 |
| Vertex | position[4]、color[4]、uv[2]；VS输出时position为齐次裁剪坐标 | (0,0,0,1)、白色、(0,0) |
| Triangle | 3个Vertex | array类型 |
| ScreenVertex | position[3]为屏幕x/y/窗口z，inv_w，未除w的color/uv | 坐标0、inv_w=1、白色、uv=0 |
| ScreenTriangle | 3个ScreenVertex | array类型 |
| Fragment | uint32 x/y、float depth、校正后的color/uv | 坐标/深度0、白色、uv=0 |

Vertex在VS输入端也可暂存应用对象空间位置；Cp调度Shader执行VS后才要求满足后续
齐次裁剪约定。屏幕y向下，depth为窗口深度，uv为归一化纹理坐标。
这些C++结构不是可直接序列化的设备ABI，内存顶点布局需按[Geom](geom.md)
定义逐字段编码，不依赖宿主struct填充或字节序。

## 共享配置

公共graphics.h中的Viewport含
x/y/width/height/min_depth/max_depth，默认(0,0,1,1,0,1)。GeometryState
组合Viewport、CullMode及front_counter_clockwise，默认NONE剔除和CCW正面。
配置记录只保存值，合法性由消费模块验证；视口默认值不会自动跟随附件。
资源描述符同样位于graphics.h，命令描述与包常量位于commands.h；
运行结果位于execution.h，图形寄存器常量位于graphics_abi.h。
硬件配置使用独立config.h中的GpuConfig/ShaderConfig/ExecutionConfig。
这些头文件不包含任何模型实现，DUT接口和未来驱动可只依赖helo_protocol。

## 数值后端

`softfloat/`保存Shader FP32所用Berkeley SoftFloat子集，构建由
`cmake/softfloat.cmake`生成helo_softfloat静态库。来源、许可、补丁和SHA256
清单见[SoftFloat来源](softfloat.md)；数值语义以[ISA](isa.md)为准。
几何、光栅、采样和混合使用[图形数值契约](numerics.md)，不属于此后端。

## 跨模块记录如何转换

```text
主机float字段 → 小端uint32内存字 → Vfetch恢复Vertex
Vertex → CP按ABI写Regfile位模式 → Shader指令执行 → CP读回Vertex
Vertex → Vp生成ScreenVertex(position,inv_w,color,uv)
ScreenTriangle → Interp生成Fragment
Fragment.color → Crop量化RGBA8 → Mem字节
```

这里的float/uint32转换使用memcpy保留位模式，不是C++数值强制转换。
例如float 1.0的位模式为0x3f800000，写入Mem后字节为00 00 80 3f；
将0x3f800000按整数数值转换为float会得到完全不同的数值，不能用于ABI。
Vertex/ScreenVertex/Fragment之间逐字段赋值，不能通过reinterpret_cast
把一种记录当成另一种；ScreenVertex增加inv_w，Fragment改变坐标语义。

每个边界的合法性由实际消费者检查：Clip验证齐次位置/属性有限，Vp验证
w和视口，Setup验证屏幕位置与inv_w，Interp验证透视分母，Tex验证uv与
纹理资源，ROP验证片元坐标/深度及附件。数据struct自身没有隐式校验构造
逻辑，默认值只便于创建记录，不等于完整资源已经合法。

## SoftFloat在数据通路中的具体使用

FPU把IssuedInstr中的uint32源操作数包装为SoftFloat的float32_t.v，按
HELO操作选择f32_add/mul/div/sqrt/mulAdd等函数；结果仍以uint32位模式
返回，不经宿主float进行中间计算。FMA通过f32_mulAdd融合计算再舍入，
不能替换成分别舍入的multiply加add。

FPU为每个有效lane选择显式或动态舍入模式，清空本次SoftFloat异常，再
计算结果并将新异常合并到该lane旧fflags；整个指令组成功后才由WBU提交。
进入/退出FPU执行时保存和恢复SoftFloat线程局部环境，避免泄漏到其他
调用；该宿主线程局部环境不是GPU线程的架构FCSR存储。完整过程见
[Shader执行算法](shader.md#单个指令组的执行算法)。

common不实现独立调度或数据流推进算法，它提供上述表示约定与数值函数。
SoftFloat内部的规范化、舍入和异常实现以本地上游源码及[来源记录](softfloat.md)
为准；这里不将其软件过程描述为已实现的RTL浮点流水线。

## 验证与限制

共享记录通过geom/rast/tex/rop/gpu共用测试验证消费者行为；SoftFloat通过
fpu及shader回归验证指令语义。没有独立common测试注册或硬件执行状态，
不能把此目录理解为一个调度单元或新增的公共内存模块。
