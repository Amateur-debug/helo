#include "lsu_test.h"
#include "shader_test.h"

namespace helo::verif {
// 验证大小端、全部标量访存宽度、独立线程地址及访存异常。
void lsu_test() {
    auto dut = make_dut();
    dut->load_program({store(1, 2, 2),
        imm(0x04, 3, 0, 1, 0),
        imm(0x04, 4, 1, 1, 0),
        imm(0x04, 5, 2, 1, 0),
        imm(0x04, 6, 3, 1, 0),
        imm(0x04, 7, 4, 1, 0),
        store(1, 2, 0, 4),
        store(1, 2, 1, 6),
        EXIT});
    dut->launch(35);
    for (unsigned thread = 0; thread < 35; ++thread) {
        dut->write_register(thread, 1, 1024 + thread * 8);
        dut->write_register(thread, 2, 0x123480feU);
    }
    require(dut->run().completed(), "load/store execution failed");
    for (unsigned thread = 0; thread < 35; ++thread) {
        require(dut->read_register(thread, 3) == 0xfffffffeU, "LB sign extension");
        require(dut->read_register(thread, 4) == 254, "LBU zero extension");
        require(dut->read_register(thread, 5) == 0xffff80feU, "LH sign extension");
        require(dut->read_register(thread, 6) == 0x80fe, "LHU zero extension");
        require(dut->read_register(thread, 7) == 0x123480feU, "LW mismatch");
        require(dut->read_memory(1024 + thread * 8 + 1, 1) == 0x80, "memory is not little endian");
        require(dut->read_memory(1024 + thread * 8 + 4, 1) == 254, "SB mismatch");
        require(dut->read_memory(1024 + thread * 8 + 6, 2) == 0x80fe, "SH mismatch");
    }
    for (const auto address : {1025U, 65536U}) {
        dut->load_program({imm(0x04, 0, 4, 1, 0), EXIT});
        dut->launch(1);
        dut->write_register(0, 1, address);
        const auto result = dut->run();
        require(!result.completed() && !result.m_fault.m_message.empty(),
            "faulting load to r0 did not report error");
    }
    dut->load_program({store(1, 2, 2), EXIT});
    dut->launch(32);
    for (unsigned thread = 0; thread < 32; ++thread) {
        dut->write_register(thread, 1, 2048 + thread * 4);
        dut->write_register(thread, 2, 99);
        dut->write_memory(2048 + thread * 4, 4, 7);
    }
    dut->write_register(31, 1, 65536);
    require(!dut->run().m_fault.m_message.empty(), "last-thread store fault was missed");
    for (unsigned thread = 0; thread < 31; ++thread) {
        require(
            dut->read_memory(2048 + thread * 4, 4) == 7, "faulting warp store partially committed");
    }
    dut->load_program({store(1, 2, 2, -4), imm(0x04, 3, 4, 1, -4), EXIT});
    dut->launch(1);
    dut->write_register(0, 1, 4096);
    dut->write_register(0, 2, 0xabcdef01U);
    require(dut->run().completed() && dut->read_register(0, 3) == 0xabcdef01U,
        "negative memory offset failed");
    dut->load_program({store(1, 2, 2), EXIT});
    dut->launch(1);
    dut->write_register(0, 1, 65536);
    require(!dut->run().m_fault.m_message.empty(), "out-of-range store accepted");
}
}
