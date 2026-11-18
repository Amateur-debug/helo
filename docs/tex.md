# Tex 纹理采样模块

## 职责与连接

Tex是[Gpu](gpu.md)持有的class，`Tex(Mem&)`构造时绑定统一存储。
`sample(texture,u,v)`读取RGBA8并返回浮点Color，`validate(texture)`检查
资源与状态。`prepare(texture)`生成不可由调用者改写描述符的Sampler，
后续`Sampler.sample(u,v)`复用验证结果。句柄不得比所属Tex存活更久。源码：[`tex.h`](../hw/model/func_model/tex/tex.h)、
[`tex.cpp`](../hw/model/func_model/tex/tex.cpp)。

GPU内部Cp将[Rast](rast.md)插值后的uv传给Tex，在启动片元Shader前完成采样，
再将采样结果写入r5–r8，详见[图形ABI](graphics_abi.md)。没有Shader发起的
TEX请求、采样回调或SFU纹理路径；Shader可以使用该输入参与普通FP32计算。

## TextureDesc与存储布局

| 字段 | 默认值及含义 |
| --- | --- |
| m_address | 0，RGBA8首纹素字节地址，要求4字节对齐 |
| m_width/m_height | 0，启用时必须为正 |
| m_filter | NEAREST，可选BILINEAR |
| m_wrap_u/m_wrap_v | CLAMP，各轴可独立选择REPEAT |

纹素紧密按行存储，(x,y)地址为address+4*(y*width+x)，字节依次为RGBA。
输出各分量为字节值除以255，不作sRGB转换。Cp处理关闭纹理状态时自己提供白色
采样输入，不调用Tex。纹理内容在每次sample时读取，模块没有缓存副本。

## 寻址与过滤

归一化uv先处理：CLAMP限于[0,1]，REPEAT取`value-floor(value)`，随后乘
对应尺寸得到s/t。整数纹素坐标再按clamp或数学正模repeat映射到有效范围。

- NEAREST读取(floor(s),floor(t))；clamp下u=1会映射到最后一列，repeat下u=1回到第一列。
- BILINEAR以纹素中心为基准：x0=floor(s-0.5)、y0=floor(t-0.5)，读取四个邻居，用小数部分做两轴线性插值。
- 边界上的四邻域分别执行wrap规则，因此repeat过滤可跨首尾纹素，clamp则复制边缘。

数值计算采用宿主浮点，不由Shader的FCSR舍入状态控制。

## 状态与错误

独立sample或prepare先检查完整纹理范围、4字节基址对齐、非零尺寸、合法filter/wrap
枚举和32位地址溢出；u/v为NaN或无穷时报错。大有限uv在扩大为整数坐标前
先归一化，避免坐标转换溢出。Tex只读Mem，不保存跨命令状态，不需要reset。

## 逐次采样算法与数据流

```text
TextureDesc + 透视校正uv
  → 验证完整资源范围
  → 归一化/包裹坐标
  → 生成1个或4个整数纹素坐标
  → 各坐标独立执行clamp/repeat
  → 计算地址并通过Mem读取RGBA8
  → 解包为float颜色
  → 最近点直返或双线性混合 → Color返回CP
```

纹理地址开区间为`end=address+4*width*height`，宽高乘法使用64位值；
检查像素数量、end<=2^32、首字4字节对齐及末字节在Mem内之后才采样。
所有非法字段都在读取纹素前拒绝。独立sample先prepare，CP则每次绘制只prepare一次；
句柄仅保存描述符快照，不缓存纹素。每次坐标仍校验，内存更新对后续采样
立即可见；四邻域即使映射到相同边界纹素，也按四次读取处理。

令归一化后的坐标为u'/v'：

```text
s=u'*width; t=v'*height
x0=floor(s-0.5); y0=floor(t-0.5)
fx=s-0.5-x0; fy=t-0.5-y0
A=texel(x0,y0); B=texel(x0+1,y0)
C=texel(x0,y0+1); D=texel(x0+1,y0+1)
result=(A*(1-fx)+B*fx)*(1-fy)+(C*(1-fx)+D*fx)*fy
```

该公式独立作用于RGBA四个分量，alpha也参与过滤。整数repeat索引用
`((index % size)+size)%size`，负邻居因此可映射至最后一列/行。
算法每次采样O(1)，输出不保留纹素引用。
实现：[tex.cpp](../hw/model/func_model/tex/tex.cpp)。

## 边界和颜色例子

2×2纹理的上行为红/绿、下行为蓝/白，在u=v=0.5处做双线性过滤，
x0=y0=0、fx=fy=0.5，结果是(0.5,0.5,0.5,1)。相同位置使用NEAREST
则读取(1,1)，结果为白色。

u=0且BILINEAR时横向邻居是-1和0：CLAMP将二者都映射到0；REPEAT
将-1映射到width-1，与首列各占一半。u=-0.25使用REPEAT先得到0.75，
不能直接将负坐标截成0。没有sRGB解码，因此插值作用于当前归一化分量，
不等同于对sRGB素材自动进行线性色彩空间过滤。

## 验证与限制

`tex_test_func`独立检查nearest/bilinear、clamp/repeat、边界与非法资源；
测试：[`tex_test.cpp`](../hw/verif/tests/tex/tex_test.cpp)。
当前只有二维RGBA8单层纹理，无mipmap/LOD、导数、各向异性、压缩格式、
比较采样或动态TEX指令。

浮点及输出比较规则统一见[图形数值契约](numerics.md)。

## RTL当前范围

[`helo_tex_nearest_q16.v`](../hw/rtl/tex/helo_tex_nearest_q16.v)将有符号
Q16归一化UV映射到RGBA8最近点纹素地址，分别支持clamp/repeat；
`helo_flat_draw.v`在图元入口检查启用的纹理范围，并将纹素读取与ROP
串行共享同一数据端口。可选Q16透视UV插值后再采样。此路径将纹素与
平面或透视插值RGBA8颜色按整数UNORM8相乘，不是FP32片元Shader的采样输入。
未实现双线性、任意FP32 UV和图形命令/Shader接线；受限路径的像素
差分由`flat_draw_compare_test_rtl`覆盖。
