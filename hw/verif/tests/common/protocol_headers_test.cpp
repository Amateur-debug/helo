#include "dut/dut.h"
#include "dut/geom_dut.h"
#include "dut/gpu_dut.h"
#include "dut/memory_dut.h"
#include "dut/rast_dut.h"
#include "dut/rop_dut.h"
#include "dut/tex_dut.h"
#include "helo/graphics_abi.h"

// 本翻译单元仅提供公共协议路径，编译失败即可发现DUT对模型私有头的依赖。
static_assert(helo::COMMAND_BYTES == 256, "command packet size changed");
