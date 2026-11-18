#include "scoreboard_test.h"
#include "shader_test.h"

namespace helo::verif {
// 检查 RAW/WAW、线程与 warp 隔离、队头阻塞以及写回掩码释放。
void scoreboard_test() {
    auto dut = make_dut();
    dut->launch(64);
    const auto producer = imm(0x02, 5, 0, 0, 7);
    const auto consumer = imm(0x02, 6, 0, 5, 1);
    dut->reserve(producer, 0, 1);
    require(!dut->dependency_ready(consumer, 0, 1), "RAW dependency missed");
    require(!dut->dependency_ready(producer, 0, 1), "WAW dependency missed");
    require(dut->dependency_ready(consumer, 0, 2), "disjoint thread incorrectly blocked");
    require(dut->dependency_ready(consumer, 1, 1), "independent warp incorrectly blocked");
    dut->enqueue(consumer, 0, 1, 100);
    dut->enqueue(producer, 0, 2, 104);
    require(!dut->issue(0) && dut->queue_pc(0) == 100, "blocked queue head was removed");
    std::array<std::uint32_t, 32> values{};
    values.fill(77);
    dut->writeback(producer, 0, 1, values);
    require(dut->read_register(0, 5) == 77 && dut->read_register(1, 5) == 0,
        "writeback ignored issue mask");
    require(dut->dependency_ready(consumer, 0, 1), "writeback did not release dependency");
    require(dut->issue(0) && dut->queue_pc(0) == 104, "instruction order incorrect");
    require(dut->issue(0) && dut->queue_empty(0), "queue did not drain");
    dut->reserve(imm(0x02, 0, 0, 0, 1), 0, 0xffffffffU);
    require(dut->dependency_ready(imm(0x02, 1, 0, 0, 1), 0, 0xffffffffU), "r0 created dependency");
    dut->reset();
    require(dut->queue_empty(0), "reset did not flush queue");
    require(dut->dependency_ready(consumer, 0, 0xffffffffU), "reset did not clear scoreboard");
}
}
