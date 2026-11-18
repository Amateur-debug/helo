# Rop 输出模块

## 职责与层次

Rop由[Gpu](gpu.md)持有，构造时引用同一个Mem；内部持有Zrop和Crop两个
class实例。Zrop计算深度/模板比较与更新，Crop处理颜色量化和混合，
Rop负责验证附件、组合各单元结果及内存提交。
源码：[`rop/`](../hw/model/func_model/rop/)。

## 接口与默认状态

| 接口/描述符 | 行为或默认值 |
| --- | --- |
| `Rop(Mem&)` | 构造时连接附件存储 |
| `prepare(framebuffer)` | 校验布局并生成绑定本ROP的Target句柄，持有不可变布局快照 |
| `Target.write(state,fragment)` | 复用布局验证，仍逐次验证片元及动态ROP状态 |
| `validate(framebuffer)` | 检查尺寸、完整地址范围、对齐和附件重叠 |
| `clear(framebuffer,color,depth=1,stencil=0)` | 同步清除全部配置附件 |
| `write(framebuffer,state,fragment)` | 执行一次片元操作，返回是否通过测试 |
| FramebufferDesc | 尺寸/地址0，has_depth=true，has_stencil=false |
| RopState | depth_test/write=true、LESS；blend=false、color_write=true |
| DepthStencilState模板部分 | test=false，ALWAYS，reference=0，read/write mask=255，fail/depth_fail/pass均KEEP |

返回true不意味着一定写了颜色：color_write可关闭。返回false可能仍按
模板失败或深度失败策略写入模板；唯有下述目标外/非法深度丢弃保证附件
不变。clear不受Draw的深度/模板/颜色写掩码控制。

## 存储、比较与混合语义

颜色附件为RGBA8，深度为FP32，模板为8位。附件紧密存储且不能互相重叠；
可以关闭深度附件，使用时必须同时关闭 depth_test 和 depth_write。
模板测试需配置模板附件。完整范围和自然对齐在清除或访问前验证。

支持八种深度/模板比较、模板掩码与 keep/zero/replace/increment/decrement/
invert（含饱和及回绕）操作。模板比较方向为 `reference op stored`。
模板失败、深度失败与通过分别选择模板更新操作；丢弃片元不进入 ROP。
深度写使能独立于深度测试使能，关闭深度测试而打开深度写仍会写入通过片元。
非有限或超出 `[0,1]` 的片元深度不写回。

颜色可禁止写入或使用非预乘 source-alpha 混合：RGB 为
`src.rgb*src.a + dst.rgb*(1-src.a)`，alpha 为
`src.a + dst.a*(1-src.a)`。颜色量化前限幅到 `[0,1]`，NaN 映射到0，正负无穷分别映射到1和0。
深度/模板测试在片元程序之后执行，因此隐藏片元也会运行片元程序。
透明几何的绘制顺序由调用者负责。

### 数值与错误处理细节

`CompareOp` 的八种值为 NEVER、LESS、EQUAL、LESS_EQUAL、GREATER、
NOT_EQUAL、GREATER_EQUAL、ALWAYS；比较不引入epsilon。模板先按read_mask
取参考值和存储值，再执行比较；更新结果按write_mask合并，掩码外原值保留。
INCREMENT/DECREMENT_CLAMP分别在255/0饱和，WRAP按8位回绕。

`Crop` 将颜色分量限幅后用 `floor(component*255+0.5)` 量化；混合时先对
源/目标分量作相同的UNORM限幅，再按前述公式计算。`Rop::write()`对目标外
坐标或非法深度返回false且全部附件不变。`clear()`的深度必须有限并位于
`[0,1]`；即使未配置深度附件也验证该值。描述符范围、重叠、所需附件缺失，
以及执行到的非法比较/模板枚举路径会抛异常，不等同于普通深度测试失败。

## 提交顺序与生命周期

片元在Cp调度Shader完成FS并判断discard后到达Rop，深度/模板测试不跳过FS。
独立write先验证完整附件，CP则每次绘制prepare一次。Target只复用布局校验，
不缓存附件内容；句柄不得比所属ROP存活更久。逐片元仍检查坐标、深度和所需
附件，再读取旧附件并计算比较、模板和颜色结果，
之后提交模板，再在通过时按使能写深度和颜色。所需计算报错时不提前写附件。
该边界是一片元操作，不使整条Draw具备回滚能力。

Rop/Zrop/Crop没有跨调用流水状态，GPU复位不重建它们，也不自动清除附件。

## 地址生成与逐片元执行算法

单样本时索引p=y*width+x，颜色/深度/模板地址分别为color_base+4p、
depth_base+4p、stencil_base+p。范围计算使用64位中间值，附件全部验证
通过后才窄化到32位地址。两个启用附件的半开区间[a,a+size)、[b,b+len)
在`a<b+len && b<a+size`时视为重叠并拒绝。

```text
Fragment + FramebufferDesc + RopState
  → 验证附件、坐标和深度
  → 检查启用状态所需附件
  → 读取旧模板/旧深度
  → 计算模板与深度通过条件
  → 选择模板更新操作
  → 通过且允许颜色写入时计算颜色（混合时读取旧颜色）
  → 提交模板
  → 若未通过则返回false
  → 按使能提交深度和颜色，返回true
```

目前实现即使模板测试失败，只要深度测试启用，仍读取并计算深度比较；
模板失败优先决定结果及模板更新分支。它不是带early-stencil访存优化的
时序模型。关闭深度测试时depth_pass=true；关闭模板测试时stencil_pass=true，
也不访问或更新模板附件。

| 模板条件 | 深度条件 | 模板更新操作 | 颜色/深度写入 |
| --- | --- | --- | --- |
| 失败 | 任意 | stencil_fail | 均不写 |
| 通过 | 失败 | depth_fail | 均不写 |
| 通过 | 通过 | pass | 分别服从color_write/depth_write |

启用模板时，先计算未掩码操作值v，再合成
`new_stencil=(stored & ~write_mask)|(v & write_mask)`，最终取8位。
例如stored=0xa5、REPLACE参考值0x3c、write_mask=0x0f，结果为0xac。
比较使用read_mask而非write_mask，二者职责独立。
实现：[rop.cpp](../hw/model/func_model/rop/rop.cpp)、
[zrop.cpp](../hw/model/func_model/rop/zrop.cpp)。

## 颜色混合和量化例子

源红色(1,0,0,0.5)，目标蓝色(0,0,1,1)，source-over得到
(0.5,0,0.5,1)。按`floor(c*255+0.5)`量化为RGBA=(128,0,128,255)，
Mem中的四字节顺序为80 00 80 ff，对应小端uint32值0xff800080。
开启混合时先限幅源/目标分量及alpha，再计算组合；不使用预乘alpha。
实现：[crop.cpp](../hw/model/func_model/rop/crop.cpp)。

clear先验证完整附件和清除深度，再打包颜色与深度位模式，按像素索引
递增写颜色、可选深度、可选模板。无效附件不能留下部分清除；这是单次
clear的预验证保证，不意味着多包command buffer具备事务回滚。

## 验证与限制

`rop_test_func`通过RopDut独立验证量化、混合、比较、模板操作和掩码、
附件错误与丢弃不修改内存；测试：
[`rop_test.cpp`](../hw/verif/tests/rop/rop_test.cpp)。
功能模型支持一或两个 RGBA8 颜色目标，深度为 FP32，模板为 8 位。每像素可配置 1 或 4 个样本。同一像素的各样本连续排列，样本索引为 (y×width+x)×samples+sample_id。清除覆盖所有启用的附件和样本，两路颜色使用同一清除值。深度和模板只测试一次，两个目标的颜色各自与其旧值混合。关闭 FS 时默认颜色复制到两路输出。

启用 sRGB 时，读取目标 RGB 后解码为线性分量，在线性空间混合，再按标准 sRGB 分段传递函数编码为字节。透明度始终为线性 UNORM8。读回多样本图像时先解码，在同一像素的四个样本间求算术平均，再编码为目标格式。加法混合允许分别配置 RGB 和透明度的源项、目标项权重。包编码见 [CP](cp.md)。
透明图元排序由调用者负责，整机提交规则见[GPU](gpu.md)。

浮点比较与颜色量化的跨实现要求统一见[图形数值契约](numerics.md)。

## RTL当前范围

`helo_rop_fragment.v`在单个数据端口上顺序处理一个片元：完整验证附件
地址、重叠与容量，读取旧模板/深度，执行八种比较和模板操作，按模板、
深度、颜色顺序写回并返回通过/错误状态。`helo_rop_memory.v`将它连接到
共享`helo_memory`，主机端口可用于上传和读回附件。输入颜色已经是
小端打包RGBA8，不进行FP32颜色量化；`blend=1`读取旧颜色并以
`(src*src_alpha + dst*(255-src_alpha)+127)/255`逐通道舍入，alpha使用
`(src_alpha*255 + dst_alpha*(255-src_alpha)+127)/255`，与此输入域的
source-over功能模型逐字节差分。不支持清除或多片元并发。
一次请求执行期间必须保持容量配置稳定。
非法布局在写入前拒绝，动态修改容量导致中途存储出错不保证回滚已
提交的单片元写入，但错误后不再访问后续附件。

`rop_compare_test_rtl`以固定种子和状态矩阵逐片元对照C++ Rop与整个
颜色/深度/模板附件；`flat_draw_compare_test_rtl`验证它与Q4覆盖、
Q16插值及最近点纹理串接的绘制路径。这些测试不代表完整GPU顶层已实现。
