#include "isu_test.h"
#include "shader_test.h"

namespace helo::verif {
// 验证发射自动登记写依赖以及第二源操作数的阻塞和恢复。
void isu_test() {
    auto dut = make_dut();
    dut->launch(32);
    const auto producer = imm(0x02, 5, 0, 0, 1);
    const auto consumer = reg(0x01, 0, 6, 0, 5);
    dut->enqueue(producer, 0, 1, 0);
    dut->enqueue(consumer, 0, 1, 4);
    require(dut->issue(0), "ready producer did not issue");
    require(!dut->dependency_ready(consumer, 0, 1), "issue did not reserve destination");
    require(!dut->issue(0) && dut->queue_pc(0) == 4, "rs2 hazard did not block issue");
    std::array<std::uint32_t, 32> values{};
    values[0] = 42;
    dut->writeback(producer, 0, 1, values);
    require(dut->issue(0) && dut->queue_empty(0), "completed rs2 dependency did not resume");
}
}
