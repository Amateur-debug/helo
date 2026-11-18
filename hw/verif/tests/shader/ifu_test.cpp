#include "ifu_test.h"
#include "shader_test.h"

namespace helo::verif {
// 验证非零入口取指、指令空间边界以及跳转目标的取指错误。
void ifu_test() {
    auto dut = make_dut();
    dut->load_program({imm(0x02, 1, 0, 0, 9), EXIT}, 256);
    dut->launch(1, 256);
    require(dut->run().completed() && dut->read_register(0, 1) == 9, "nonzero entry fetch failed");
    dut->load_program({EXIT}, 65528);
    dut->launch(1, 65528);
    require(dut->run().completed(), "last aligned instruction fetch failed");
    bool rejected = false;
    try {
        dut->launch(1, 65536);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "out of bounds launch accepted");
    for (const auto word : {jump(0, 4), imm(0x0a, 0, 0, 1, 0)}) {
        dut->load_program({word});
        dut->launch(1);
        dut->write_register(0, 1, 2);
        require(!dut->run().m_fault.m_message.empty(), "misaligned control target accepted");
    }
}
}
