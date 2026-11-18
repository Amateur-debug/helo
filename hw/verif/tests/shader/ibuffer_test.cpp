#include "ibuffer_test.h"
#include "shader_test.h"

namespace helo::verif {
// 验证独立 warp 队列先进先出、空队列发射和复位冲刷。
void ibuffer_test() {
    auto dut = make_dut();
    dut->launch(64);
    const auto nop = imm(0x02, 0, 0, 0, 0);
    require(dut->queue_empty(0) && !dut->issue(0), "empty queue issued instruction");
    dut->enqueue(nop, 0, 1, 16);
    dut->enqueue(nop, 0, 1, 20);
    dut->enqueue(nop, 1, 1, 100);
    require(dut->queue_pc(0) == 16 && dut->queue_pc(1) == 100, "warp queues share storage");
    require(dut->issue(1) && dut->queue_empty(1) && dut->queue_pc(0) == 16,
        "issuing warp changed another queue");
    require(dut->issue(0) && dut->queue_pc(0) == 20, "FIFO order violated");
    dut->reset();
    require(dut->queue_empty(0), "reset retained queued instruction");
}
}
