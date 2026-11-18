# Rast 光栅模块

## 职责与层次

Rast由[Gpu](gpu.md)持有，内部持有Setup与Interp两个class实例；输入是
[Geom](geom.md)产生的ScreenTriangle，输出是按行扫描的像素中心Fragment流，也保留完整列表收集接口。
不读取Mem、不执行Shader、不写附件。源码：
[`rast.h`](../hw/model/func_model/rast/rast.h)、
[`setup.h`](../hw/model/func_model/rast/setup.h)、
[`interp.h`](../hw/model/func_model/rast/interp.h)。

| 单元 | 实例入口 | 职责 |
| --- | --- | --- |
| Rast | begin(triangle,width,height) | 完成设置校验，返回独立遍历游标 |
| Rast | next(cursor,fragments,capacity) | 清空输出并追加至多capacity个片元，返回是否有输出 |
| Rast | rasterize(triangle,width,height) | 使用游标收集完整Fragment列表，供独立调用 |
| Setup | prepare(triangle,width,height) | 返回可选TriangleSetup，统一绕序和计算包围盒 |
| Setup | edge(a,b,x,y)、top_left(a,b) | 有向边函数及共享边包含规则 |
| Interp | interpolate(triangle,weights,x,y) | 生成深度及透视校正属性 |

## 坐标与覆盖规则

屏幕原点左上、y向下，像素中心(x+0.5,y+0.5)。Setup将负面积绕序交换为
正面积，不改变覆盖；零面积或完全离开目标时没有输出。像素包围盒按
`ceil(min_coordinate-0.5)`和`floor(max_coordinate-0.5)`形成，再限制于
[0,width-1]、[0,height-1]。

有向边函数为 `(b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x)`，内部取正。
边上像素仅在该边满足 `dy<0 || (dy==0 && dx>0)` 时包含，这是当前坐标系
的top-left规则；共享边由一侧三角形负责，避免重复覆盖。输出按y再x递增。

## 属性插值

每个像素的三个重心权重为相对边函数值除以正三角形面积。设权重为λ，
ScreenVertex保存的倒数w为q：

```text
depth = Σ(λi * zi)
color = Σ(λi * qi * colori) / Σ(λi * qi)
uv    = Σ(λi * qi * uvi)    / Σ(λi * qi)
```

深度已经是视口映射后的窗口深度，采用屏幕线性插值；color/uv输入尚未
除w，只在Interp中做上述校正。边函数和插值累加使用double，结果保存为
float。返回Fragment的x/y是整数像素坐标。Cp随后调度纹理采样、Shader执行FS，再
交给ROP；光栅模块本身不做深度测试。

## 状态、边界与错误

Setup拒绝超过int表示范围的目标尺寸、非有限屏幕坐标、非有限或非正inv_w；
零宽/高返回空输出。Interp拒绝非有限或非正的透视分母。直接调用Interp的
调用者需提供有效顶点和对应重心权重；该接口不会检查权重是否来自三角形
内部，正常Rast路径通过覆盖判定提供有效权重。

Rast/Setup/Interp无持久流水状态。Cursor按值保存设置和下一候选像素，
调用者可交错使用多个游标；耗尽后反复next仍返回false并清空输出。capacity
必须为正。Cp按三角形依次消费，使用一个容量至多128的片元缓冲，没有tile缓存
或周期推进。begin仍在该三角形任何片元副作用之前验证全部屏幕位置/inv_w。

## 一次rasterize的执行过程

```text
ScreenTriangle + 目标宽高
  → Setup校验、统一正面积、形成TriangleSetup
  → 逐行逐列遍历像素中心
  → 三条边覆盖判定
  → 三个重心权重
  → Interp计算depth/color/uv
  → 单批Fragment返回CP，游标保留后续扫描位置
```

TriangleSetup持有调整后的顶点、正双面积和整数包围盒。每个候选像素的
三个边函数分别对应边(v1,v2)、(v2,v0)、(v0,v1)，即权重λ0/λ1/λ2。
某条边为负，或为零且该边不满足top-left时，立即跳过这个像素，不调用
Interp。只有三条边全部通过才计算完整输出。

算法按包围盒逐像素重新计算边函数，不采用增量边步进、tile分层剔除或
SIMD批量测试。若包围盒有B个候选像素、生成F个片元，总时间O(B)。
游标空间O(1)，整机片元缓冲O(128)；独立rasterize收集接口仍需O(F)空间。
片元坐标、属性、顺序与跨三角形批次边界均不因分批改变。
实现：[rast.cpp](../hw/model/func_model/rast/rast.cpp)、
[setup.cpp](../hw/model/func_model/rast/setup.cpp)。

## 透视插值的数值例子

屏幕顶点v0=(0,0)、v1=(4,0)、v2=(0,4)，面积16。像素(0,0)的中心为
(0.5,0.5)，对应λ=(3/4,1/8,1/8)。设窗口深度为(0.1,0.5,0.9)、
q=(1,1/2,1/4)，uv=((0,0),(1,0),(0,1))：

```text
depth = 3/4*0.1 + 1/8*0.5 + 1/8*0.9 = 0.25
D = 3/4*1 + 1/8*1/2 + 1/8*1/4 = 27/32
u = (1/8*1/2)/D = 2/27
v = (1/8*1/4)/D = 1/27
```

如果把uv误作屏幕线性插值，会得到(1/8,1/8)，与正确结果不同。
Interp先用double累积D、depth和各属性分子，检查D有限且>0，再除D并
转回float。深度不除D，否则会把已经完成透视除法的窗口深度再次校正。
实现：[interp.cpp](../hw/model/func_model/rast/interp.cpp)。

## 共享边为何只覆盖一次

两三角形共享一条方向相反的边；非水平边恰有一个方向满足dy<0，水平边
恰有一个方向满足dx>0。因此边函数恰为0的像素中心只属于一侧。
例如将4×4方形沿对角线拆成两个三角形，16个像素均应覆盖一次。
这里比较浮点计算结果的精确零，没有epsilon；当前坐标量化与未来RTL接入
条件统一见[图形数值契约](numerics.md)。

## 验证与限制

`rast_test_func`覆盖4×4共享边恰好一次覆盖、反向绕序、退化、目标裁界、
像素中心和独立透视插值公式；测试：
[`rast_test.cpp`](../hw/verif/tests/rast/rast_test.cpp)。
当前单样本三角形光栅，无MSAA、scissor状态、保守光栅或固定点精度保证。
跨实现必须选择同一数值配置，不得用属性容差掩盖覆盖像素差异。
固定种子测试额外检查64个1/16网格矩形的共享边、两种对角线和反向绕序，
独立func_resources_test以1/3/32/128/1024片元容量核对游标顺序、属性和单批上限。

## RTL 覆盖扫描（受限输入域）

[`helo_rast_coverage.v`](../hw/rtl/rast/helo_rast_coverage.v)实现
带`valid/ready`背压的Q4网格三角形扫描，只输出片元坐标与Q8边值/面积；
[`helo_rast_depth_q16.v`](../hw/rtl/rast/helo_rast_depth_q16.v)在受限绘制路径
将Q16顶点深度按边值线性插值并转为精确binary32，
[`helo_rast_uv_q16.v`](../hw/rtl/rast/helo_rast_uv_q16.v)以Q16倒数w对
Q16 UV做透视校正。UV插值结果舍入到最近Q16，半值远离零；
[`helo_rast_color_q8.v`](../hw/rtl/rast/helo_rast_color_q8.v)以相同倒数w对
逐顶点RGBA8做透视校正，逐字节四舍五入。仍不覆盖任意binary32顶点属性。
坐标域、空图元完成信号和后续阶段见
[图形RTL实施](rtl_graphics_plan.md)。独立`rast_coverage_compare_test_rtl`
在精确网格输入上对照本页C++ Rast的坐标与顺序；它不覆盖任意binary32
屏幕三角形，也不能替代完整`rast_test_func`。
