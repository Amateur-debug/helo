# Mem 存储模块

## 职责与实际所有权

Mem由[Gpu](gpu.md)按值持有，使用零初始化的`std::vector<uint8_t>`保存
统一小端地址空间。构造函数`Mem(bytes)`指定容量；所有访存地址为32位
字节地址，代码、顶点、纹理和附件共用这一地址空间。
源码：[`mem.h`](../hw/model/func_model/mem/mem.h)。

Icache和Dcache源码也位于mem目录，但实例由[Shader](shader.md)持有，
各自引用Gpu的同一个Mem；Mem不反向拥有它们。

```text
主机上传 / Cp包读取 / Geom.Vfetch / Tex / Rop ─────────→ Gpu.Mem
Shader.Ifu → Shader.Icache ────────────────→ Gpu.Mem
Shader.Lsu → Shader.Dcache ───────────────→ Gpu.Mem
```

## Mem接口与数据语义

| 接口 | 语义 |
| --- | --- |
| read(address,size) | size=1/2/4，返回零扩展uint32值 |
| write(address,size,value) | 写入低size字节，按小端排列 |
| validate(address,size) | 检查合法宽度、自然对齐、容量和32位地址边界 |
| load_words(address,words) | 装载uint32列表，起点4字节对齐 |
| load_instructions(address,instructions) | 装载uint64机器码列表，起点8字节对齐 |

例如写入32位0x12345678后，递增地址的字节是78、56、34、12。
read不做有符号扩展，指令所需符号扩展由LSU实现。没有单次8字节Mem读写，
指令装载和取指用两个32位字组合。

两种批量装载先检查完整范围，失败不部分写入；空列表也检查起点对齐和
起点不超过容量。单次读写按size自然对齐，不允许未对齐访问或地址回绕。
非法宽度抛invalid_argument，未对齐读写抛runtime_error，越界抛out_of_range；
批量装载布局或范围错误统一拒绝。

## Icache和Dcache

`Icache(Mem&)`的`fetch(pc)`要求8字节对齐，先检查整个八字节范围，再按
低32位加高32位移位拼成HELO机器码。接口见
[`icache.h`](../hw/model/func_model/mem/icache.h)。
`Dcache(Mem&)`的read/write/validate直通到同一Mem，接口见
[`dcache.h`](../hw/model/func_model/mem/dcache.h)。

两者都是class适配端口，没有缓存行、替换、命中、延迟或带宽模型。
通过任一端口成功写入的数据立即对后续其他端口读取可见；取指不会保留
旧代码副本。全局访存排序与线程组提交规则由Shader协调，见[Shader](shader.md)。

## 生命周期与错误边界

Gpu先构造Mem再绑定子模块，销毁顺序相反。Gpu.reset及其内部Shader.reset均不清空Mem；
需要初始化附件时在command buffer中编码CLEAR包，更新资源时显式上传。Mem没有内存分配协议、
虚拟地址、访问权限或资源所有权登记；调用者负责布局和不希望出现的别名。
容量不表示物理显存带宽或硬件配置，同一对象并发访问需调用者同步。

## 访存算法与字节数据流

```text
read/write(address,size)
  → size属于{1,2,4}
  → address % size == 0
  → 用64位计算address+size，检查容量和2^32界限
  → 逐字节拼接读取或拆分写入
```

读取值为`Σ(bytes[address+i] << (8*i))`，i从0到size-1；写入字节为
`(value >> (8*i)) & 0xff`。循环最多4次，单次访问成本O(1)。
完整访存范围检查在循环前完成，因此失败不能留下低地址字节已经写入的
部分结果；该保证不自动扩展到多个独立write调用。

load_words先验证`address%4==0`、address<=capacity，以及
`words.size() <= (capacity-address)/4`和32位地址剩余空间，再逐字调用
write。load_instructions同理以8为单位先检查范围，再把每个64位指令
拆为低/高两个32位字。采用“剩余容量除记录宽度”比较，避免先乘数量溢出。
空列表没有循环，但仍检查基址和对齐。
实现：[mem.cpp](../hw/model/func_model/mem/mem.cpp)。

## 取指、数据访问和CP读包的具体过程

Icache.fetch(pc)先检查pc%8==0，再分别validate(pc,4)、validate(pc+4,4)，
最后返回`low | (uint64(high)<<32)`。例如指令0x1122334455667788在低地址
按88 77 66 55 44 33 22 11存储。不存在先返回低半指令再等待高半的状态。
Dcache的三个入口只转发到Mem，没有额外缓存一致性动作。

CP的PacketReader会对同一个Mem调用64次read，形成当前256字节包快照；
这64次读取在当前单线程同步执行中连续完成，并非模型实现了256字节总线
原子事务。Geometric Vfetch、Tex和Rop也直接使用同一Mem；Shader的LSU则
通过Dcache，并由LSU在第一批写入前预验证整线程组地址。

若Dcache先在A写入0x12345678，后续Mem.read(A,4)与对应Icache范围读取
会看到相同字节；无需flush/invalidate，因为没有缓存副本。不同线程对同
地址的顺序由Shader执行模型决定，Mem不自行合并访问或提供原子操作。

## 验证

`mem_test_func`覆盖大小端布局、宽度/对齐/边界和批量装载原子性；
`icache_test_func`覆盖64位取指和更新可见性；`dcache_test_func`覆盖窄访问
和共享底层一致性。共用MemoryDut测试位于
[`tests/mem/`](../hw/verif/tests/mem/)。

## RTL 内存端口（当前实现）

[`helo_memory.v`](../hw/rtl/mem/helo_memory.v)实现独立主机/数据1、2、4字节
读写端口和8字节取指端口，三者共享同一小端字节存储。请求的`valid`使能
组合结果及错误信号；有效写请求在时钟上升沿提交，无效写请求不写入。
同周期两个有效写端口对同一字节写入时数据端口优先；取指在时钟沿前
看到旧数据，时钟沿后看到新数据。存储仿真从零初始化，不提供清空端口。

`MAX_BYTES`是编译期存储容量，`capacity_bytes`是当前实例允许访问的容量；
请求必须同时满足两者。现有Verilator共用测试把`MAX_BYTES`设为默认
4096字节，并以不同运行期容量验证边界，不能把它当成已经连接16MiB
整机存储的实现。批量装载范围和失败原子性由测试适配器在发起多次单字
写入前检查，RTL只负责单次访问。尚未实现GPU总线、DMA、仲裁等待、
实体SRAM宏及完整整机访存连接。

`helo_shader_lane_memory`验证机器码译码到LSU的直接访存连接，
`helo_shader_core`复用相同内存模块执行单线程程序。两者不提供多lane
事务队列或整组写入预验证，不能把单次访问无副作用错误推广为SIMT组原子性。
