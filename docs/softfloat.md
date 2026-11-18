# SoftFloat 源码来源

功能模型使用 `hw/model/func_model/common/softfloat/` 中自带的源码。
本副本从 Spike 的 SoftFloat 目录复制，只包含当前 HELO ISA FP32 后端的 C 文件及传递依赖头文件；不需要初始化或引用 Spike 子模块即可构建。

- 来源仓库：https://github.com/riscv-software-src/riscv-isa-sim
- 来源提交：`1e05ddac3a6c351bfc0aeed0cf3a68940e7200ab`
- 来源目录：`softfloat/`
- 数值库：Berkeley SoftFloat；具体版本和版权年份以各文件头为准。
- 修改：保留原始名称和全部版权、许可证声明。`platform.h` 的配置引用由 `config.h` 改为 `softfloat_config.h`，避免与其他模块冲突；其余源码保持原样。该配置由 CMake 在构建目录的 `softfloat_include/` 下生成，不复制 Spike 的配置文件。
- 许可证：各文件保留 Berkeley SoftFloat 的 BSD 三条款许可。发布源码时保留声明；发布二进制时须在随附文档或其他材料中提供对应版权声明、许可条件和免责声明。
- 更新：显式选择来源版本，更新下面的校验清单，并重新运行 Shader 回归；不自动跟随 Spike 子模块更新。

本次数值库也用于图形顶点与片元程序中的 FP32 指令；裁剪、视口、光栅、
插值、纹理过滤及混合目前采用宿主浮点运算，不属于 SoftFloat 逐位保证范围。
图形集成未更换来源版本或修改下列数值库源码。

以下 SHA-256 对应复制时的原始内容；`platform.h` 的本地配置头改名属于上面记录的补丁，因此当前文件不再匹配其原始校验值。

| 文件 | SHA-256 |
| --- | --- |
| `f32_add.c` | `5d7ec31485aa2d5f74415afbca0eb5f46124e01562b4df3bb96719becec92432` |
| `f32_classify.c` | `965ba6ea7d8ce55dc89b341902d363788598d98bbce181cadc25eeab3d8ee85f` |
| `f32_div.c` | `05eeaf6c6ea726b5c15ec49768682f49814234a941bb447c18ac2608660724e8` |
| `f32_eq.c` | `7d345d305b94de0bf66a36a9217d3a014fb7b493f90f3d2c33e1488305deeaf3` |
| `f32_isSignalingNaN.c` | `1572b5b1d2296ae9ebfe9acc153e58926a2863a4e38bbdaa9830bf8c51cbf5ab` |
| `f32_le.c` | `5eabe6bfcc8a947ee4dedf0ef2a384bd6210a784348f0ae40eb8c3060cc4d10e` |
| `f32_le_quiet.c` | `1cb6dc73d1cce42bc5426aefa4282740b8ceac1210c813546ac2dd9dabfa7855` |
| `f32_lt.c` | `fa9a743ebe826e734df8c4b32568b6d8af064144482ac4b6b5e789581b1c64c8` |
| `f32_lt_quiet.c` | `1da6e93f8981c9c33e33b8a89a3eb5a59b6b003b31a52cfe7aad87e5da89ae29` |
| `f32_mul.c` | `ddd925d4a83ccede935d4095aee4da5c90bac2ea98c9c8763da055b3ba4aedd7` |
| `f32_mulAdd.c` | `0c55f1cccb4661ac76f5379e1ae6ab71f0b9744ef62319716ef2dcaad3c04b85` |
| `f32_sqrt.c` | `d13ab78712ce3a7b484fc157f12d037f1e3d06d664b9e08c7e7b1e61b0d86cf6` |
| `f32_sub.c` | `f7ad4a2949b8157195230525c76712eb79bdb02ce659be401698c0b025646733` |
| `f32_to_i32.c` | `31116d33b59cc24c503c45327f7761c1fb0a40e0c5db5c94a1b121d39474abb1` |
| `f32_to_ui32.c` | `c7f7dd61e2d3f8f5900323c9f7820f92e250cb5c19c6beddd901721ab66f8c55` |
| `i32_to_f32.c` | `e96652c17547ed1d0621707f041aeed593fc9dee5631dd06c3996b732aee7f75` |
| `internals.h` | `3ca1f1a2daedcc6962bafa418b99aa3698a83afd02180bcbc4ca53450eb97514` |
| `platform.h` | `a9577a04b6d9b519d6aaadffa2d49a902a40d8f2f6b94c19c78256d3586115bd` |
| `primitiveTypes.h` | `1c7b2313e819c06d3398ea4e42017a23ca9e66006f701687f35c020642347f47` |
| `primitives.h` | `759da4086b9da9fb1d757f93c3bd4c2115f633cf4b4b37912621ea127eebf3e4` |
| `s_addMagsF32.c` | `22c9177a17834ed4761e3769e047a8e1ca46ae854c9fdbb985e230a16618b32f` |
| `s_approxRecipSqrt32_1.c` | `fe4fe879c75806a2d8a6311a8744cfeb4193270a87610f40b6930c3b7c58832f` |
| `s_approxRecipSqrt_1Ks.c` | `c857b99d64557ba57c78cad839a2bf8f7ce86c66e0dceff30b0395c88ebc42fe` |
| `s_countLeadingZeros32.c` | `9f6f7889dad9dd5c39f9c8b9ee780c8e0c5f5863bf0e7986312c7fa4e697be46` |
| `s_countLeadingZeros64.c` | `312c74aebb8b9454f0a460306b15bcd2bde73e17824f2e229f2a7b71d75ce1db` |
| `s_countLeadingZeros8.c` | `ddcc24ab97647d2806800e81c14f1a8216c71dcf4863637a04413fec429ba6fa` |
| `s_mulAddF32.c` | `a8d11382c02b274958ee8f5d7a0bbe5c840acd878f2f78836fadda1b6e847f3e` |
| `s_normRoundPackToF32.c` | `b55d160b1d64d30d26b38876b81a67161bbecd1b84a0e68618475328d0cb92c1` |
| `s_normSubnormalF32Sig.c` | `e1c4ea7a1809c60779aceb6ab31eb466efc821e635adc61b25a6f2144cf5e31c` |
| `s_propagateNaNF32UI.c` | `4ad3b1a533cfa78e92c08794723705bbafd85c553a5a5657633e6ed260249940` |
| `s_roundPackToF32.c` | `4cf6c727fd5fc56383a8bc21a3b5426619dea8ca76035462e3c1ec1f5790cbff` |
| `s_roundToI32.c` | `4f892d8616b25da3994440d2827359e6e62db613ddc574177771ba866b8bede2` |
| `s_roundToUI32.c` | `d70b5a9a986a9291798848f5adbefe814c16d8474a19553127e065fd61fcfec4` |
| `s_subMagsF32.c` | `2e0d23b78992a4a0b1f731c82c442eabaafdc7953fa1bb912d7baeb39fd2c331` |
| `softfloat.h` | `3e245303268e7ad1d5e1ef4bf114c8add7a9dab07ed14f9f6d9bb5ddfc04f919` |
| `softfloat_raiseFlags.c` | `57ec93b3ba971593d7c9b4d1f05c62914de91936286e84745a18300b4692fd03` |
| `softfloat_state.c` | `e4c73f25255b4dd1fab8165150c8c7cbffb8edb68e4a023cfa44468a00627650` |
| `softfloat_types.h` | `d295eed445f7c1371c1129f0bc6f4c19a8612fd47d1c6b1f0666d3c5a406bc22` |
| `specialize.h` | `669cd5af36c7faa1051e0b8a349724f6c66655212f1bce340b269ac967592911` |
| `ui32_to_f32.c` | `76b5c87c61a6435034e5d2099d5ea0b2a2fcbc723d67123459fddeca8b1abb4f` |
