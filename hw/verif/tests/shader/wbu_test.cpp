#include "wbu_test.h"
#include "shader_test.h"

namespace helo::verif {
// 验证高位线程写掩码、分批写回释放及无目标指令不改寄存器。
void wbu_test() {
    auto dut = make_dut();
    dut->launch(32);
    const auto producer = imm(0x02, 5, 0, 0, 1);
    const auto consumer = imm(0x02, 6, 0, 5, 1);
    dut->reserve(producer, 0, 0x80000001U);
    std::array<std::uint32_t, 32> values{};
    values.fill(99);
    values[31] = 0xdeadbeefU;
    dut->writeback(producer, 0, 1, values);
    require(
        dut->dependency_ready(consumer, 0, 1) && !dut->dependency_ready(consumer, 0, 0x80000000U),
        "partial completion released other thread dependency");
    require(dut->read_register(0, 5) == 99 && dut->read_register(31, 5) == 0,
        "partial writeback modified another thread");
    dut->writeback(producer, 0, 0x80000000U, values);
    require(dut->read_register(31, 5) == 0xdeadbeefU && dut->read_register(30, 5) == 0,
        "highest thread write mask failed");
    require(dut->dependency_ready(consumer, 0, 0xffffffffU), "completed writes still blocked");
    dut->writeback(store(0, 0, 2), 0, 0xffffffffU, values);
    require(dut->read_register(0, 5) == 99, "non-register instruction changed state");
    dut->writeback(imm(0x02, 0, 0, 0, 1), 0, 0xffffffffU, values);
    require(dut->read_register(31, 0) == 0, "writeback changed r0");
}
}
