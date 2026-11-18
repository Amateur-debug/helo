# Geom 几何模块

## 职责与层次

Geom由[Gpu](gpu.md)按值持有，负责顶点/索引获取、三角形装配、齐次裁剪、
视口和面剔除。它持有Vfetch、Pa、Clip、Vp四个class实例；Vfetch构造时
绑定与GPU其他模块相同的Mem，所有处理入口均为实例方法。
源码：[`geom/`](../hw/model/func_model/geom/)。

| 单元 | 输入 → 输出 | 职责 |
| --- | --- | --- |
| Vfetch | Mem地址/count/stride → Vertex或uint32索引列表 | 验证范围并装载记录 |
| Pa | Vertex列表及可选索引 → Triangle列表 | 检查数量及索引界限，按列表组装 |
| Clip | 齐次Triangle → 0个或多个Triangle | 六平面裁剪及属性插值 |
| Vp | Vertex和Viewport → ScreenVertex | 透视除法与视口映射 |
| Geom | 着色后Vertex/索引/GeometryState → ScreenTriangle列表 | 组合装配、裁剪、变换与剔除 |

## 接口与执行关系

`Geom(const Mem&)`构造时连接存储；`fetch(address,count,stride=40)`和
`fetch_indices(address,count)`通过Vfetch获取数据，`assemble(vertices,indices={})`
通过Pa验证/装配；`validate_assembly(count,indices)`仅校验而不分配图元，
`process(vertices,indices,state)`执行完整后段几何处理。

GPU内部Cp在fetch之后、process之前调度Shader执行顶点程序，Geom本身不持有Shader，
也不执行着色器程序。process接收的数据已经完成VS变换；关闭VS时使用
顶点流原始属性。Geom输出包含未除w的color/uv和inv_w，交给[Rast](rast.md)。

## 资源布局、裁剪与视口

顶点布局是小端 FP32：position.xyzw 在字节0–15，color.rgba 在16–31，
uv 在32–39。默认 stride=40，可声明更大、四字节对齐的跨度。索引是
小端 uint32，必须小于上传顶点数。非索引顶点数或索引数必须为3的倍数。

VS 在图元装配前对顶点流的每个记录执行一次，包括未被索引引用的记录；
复用索引不会重复执行该顶点。Draw 显式启用索引但 index_count=0 时为空绘制。
独立 `Pa/Geom` API 的空索引向量表示非索引列表。

裁剪输入为齐次位置，六个半空间为 `-w <= x,y,z <= w`。Clip 使用
Sutherland–Hodgman 裁剪并线性插值属性，然后扇形重新三角化。
无有效正 w 的退化图元不生成屏幕输出；非有限位置或属性报错。

视口将 NDC 的 x 映射到 `[x,x+width]`，y 反转映射到
`[y,y+height]`，NDC z 从 `[-1,1]` 映射到声明的 `[min_depth,max_depth]`。
视口大小必须有限且为正，深度区间有序且位于 `[0,1]`。
屏幕原点左上、y 向下，默认 NDC 逆时针为正面，支持不剔除/正面/背面剔除。

GeometryState默认不剔除、NDC CCW为正面，视口为(0,0,1,1)、深度[0,1]。
视口不会自动使用附件尺寸。Clip对位置、颜色、UV中的非有限值报错，交点
按齐次线性权重插值并锁定当前裁剪平面。裁剪得到的多边形按扇形三角化；
屏幕零面积三角形由Geom剔除。

## 状态和错误

Vfetch先验证起点、stride及整个有效范围：stride至少40且四字节对齐，
索引跨度固定4字节；count=0仍检查布局但不读内存。Pa拒绝不完整三角形
列表和越界索引。Vp拒绝非正w、非法视口、坐标或倒数w溢出；非法剔除枚举报错。
模块无跨命令状态、不写Mem，没有需要单独清理的执行队列。

## 分单元算法与数据流

### Vfetch：地址生成与属性装载

对count>0，先用64位中间值计算末端开区间
`end = address + (count-1)*stride + 40`。检查起点/stride对齐、end不超过
2^32，并检查首字与末字都在Mem容量内，再创建输出向量。顶点i的基址为
`address+i*stride`，依次读取10个32位字并保留位模式转换为float：

```text
Mem[base+0..12]  → Vertex.position[0..3]
Mem[base+16..28] → Vertex.color[0..3]
Mem[base+32..36] → Vertex.uv[0..1]
```

stride大于40时跳过尾部填充，不把填充解释成属性。索引流用相同范围验证
机制，但跨度/记录长度均为4。算法时间O(vertex_count)，输出空间同阶。
对应实现：[vfetch.cpp](../hw/model/func_model/geom/vfetch.cpp)。

### Pa：列表装配

有索引时先确认索引数为3的倍数，依次检查索引小于vertices.size，再取
`vertices[indices[3k+j]]`组成第k个Triangle；无索引时用`vertices[3k+j]`。
Pa生成顶点记录的三元组，不修改原始顶点/索引流。CP在VS之前只调用
validate_assembly检查数量及索引；VS之后由Geom.process装配着色后的记录，
不再创建并丢弃一套着色前的三角形。
对应实现：[pa.cpp](../hw/model/func_model/geom/pa.cpp)。

### Clip：Sutherland–Hodgman逐平面裁剪

先检查全部position/color/uv有限。初始polygon为三个输入顶点，随后按
以下顺序处理六个平面，距离d>=0视为内部（边界包含）：

| 顺序 | 平面 | 带符号距离d |
| --- | --- | --- |
| 0 / 1 | 左 / 右 | w+x / w-x |
| 2 / 3 | 下 / 上 | w+y / w-y |
| 4 / 5 | 近 / 远 | w+z / w-z |

每一轮令previous为当前多边形末顶点，顺序遍历current，按闭合边
previous→current向一个新列表追加输出：

| previous / current | 输出 |
| --- | --- |
| 内 / 内 | current |
| 内 / 外 | 交点 |
| 外 / 内 | 交点，然后current |
| 外 / 外 | 无 |

跨越平面的交点参数为`t=d_previous/(d_previous-d_current)`，每个位置、
颜色和uv分量都用`(1-t)*previous+t*current`插值。位置当前裁剪轴再精确
设为+w或-w，抑制舍入误差将交点放到本平面之外。处理完一个平面后用新
列表替换旧polygon，空列表可提前结束。最后输出扇形三角形
`(polygon[0],polygon[i],polygon[i+1])`，包含非正w顶点的扇形跳过。
不承诺对不同输入顶点顺序生成逐位相同的交点。

例如右平面w-x=0：A=(0,0,0,1)、B=(2,0,0,1)，距离分别1和-1，t=1/2，
交点为(1,0,0,1)。若两端颜色为红/蓝，则交点颜色为(0.5,0,0.5,1)。
透视除法在裁剪之后执行，不能先除w再线性插值这些原始属性。
对应实现：[clip.cpp](../hw/model/func_model/geom/clip.cpp)。

### Vp与Geom：视口映射、退化与面剔除

令q=1/w，视口为(vx,vy,vw,vh,zmin,zmax)，逐顶点计算：

```text
screen.x = vx + (clip.x*q + 1)*vw/2
screen.y = vy + (1 - clip.y*q)*vh/2
screen.z = zmin + (clip.z*q + 1)*(zmax-zmin)/2
screen.inv_w = q
screen.color/uv = 原始未除w的属性
```

Geom对每个裁剪结果计算屏幕有向双面积
`A=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)`。
A=0直接丢弃；屏幕y轴反向，因此默认NDC CCW正面对应A<0。
BACK剔除非正面，FRONT剔除正面，NONE保留两种朝向。剩余ScreenTriangle
按装配、裁剪输出顺序交给CP，由CP调度Rast。
实现：[vp.cpp](../hw/model/func_model/geom/vp.cpp)、
[geom.cpp](../hw/model/func_model/geom/geom.cpp)。

## 验证与限制

`geom_test_func`通过GeomDut覆盖装配、六平面裁剪和属性、视口映射、
朝向、退化、非法属性和顶点读取边界。测试：
[`geom_test.cpp`](../hw/verif/tests/geom/geom_test.cpp)。
当前只支持固定position4/color4/uv2布局与三角形列表，没有点/线拓扑、
几何着色器或细分。图形ABI见[Shader图形ABI](graphics_abi.md)。

浮点表示、舍入及跨实现容差统一见[图形数值契约](numerics.md)。
