#include "wsu_test.h"
#include "shader_test.h"

namespace helo::verif {
// 检查无限循环 warp 不会阻止其他 warp 的有效指令执行。
void wsu_test() {
    auto dut = make_dut();
    dut->load_program({branch(1, 0, 0, 0), imm(0x02, 2, 0, 0, 77), EXIT});
    dut->launch(64);
    for (unsigned thread = 32; thread < 64; ++thread) {
        dut->write_register(thread, 1, 1);
    }
    const auto result = dut->run(100);
    require(!result.completed(), "live infinite warp unexpectedly completed");
    for (unsigned thread = 32; thread < 64; ++thread) {
        require(dut->read_register(thread, 2) == 77, "runnable warp starved");
    }
    for (unsigned thread = 0; thread < 32; ++thread) {
        require(dut->read_register(thread, 2) == 0, "looping thread escaped control flow");
    }
}
}
