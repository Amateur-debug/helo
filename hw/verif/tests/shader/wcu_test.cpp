#include "wcu_test.h"
#include "shader_test.h"

namespace helo::verif {
namespace {
// 验证发散循环后屏障通信、重复轮次、尾 warp 与错误恢复。
void check_barriers() {
    auto dut = make_dut();
    dut->load_program({imm(12, 200, 8, 0, 0x10),
        imm(2, 201, 0, 200, 1),
        imm(2, 201, 0, 201, -1),
        branch(201, 0, 1, -8),
        store(1, 200, 2),
        BARW,
        imm(4, 202, 4, 2, 0),
        BARW,
        imm(2, 203, 0, 203, 1),
        EXIT});
    dut->launch(35);
    for (unsigned thread = 0; thread < 35; ++thread) {
        const auto next = thread < 32 ? (thread + 1) % 32 : 32 + (thread - 32 + 1) % 3;
        dut->write_register(thread, 1, 4096 + thread * 4);
        dut->write_register(thread, 2, 4096 + next * 4);
        dut->write_memory(4096 + thread * 4, 4, 0xffffffffU);
    }
    require(dut->run().completed(), "divergent barrier program did not complete");
    for (unsigned thread = 0; thread < 35; ++thread) {
        require(dut->read_register(thread, 202) ==
                        (thread < 32 ? (thread + 1) % 32 : (thread - 32 + 1) % 3) &&
                    dut->read_register(thread, 203) == 1,
            "barrier released before peer store or duplicated release");
    }
    const std::vector<std::vector<std::uint64_t>> invalid = {
        {branch(1, 0, 0, 16), EXIT, BARW, EXIT},
        {branch(1, 0, 0, 24), BARW, EXIT, BARW, EXIT},
        {branch(1, 0, 0, 24), BARW, EXIT, EXIT}};
    for (const auto& words : invalid) {
        dut->load_program(words);
        dut->launch(32);
        for (unsigned thread = 0; thread < 32; ++thread) {
            dut->write_register(thread, 1, thread % 2);
        }
        require(!dut->run().m_fault.m_message.empty(), "invalid barrier protocol accepted");
        dut->reset();
        dut->load_program({BARW, BARW, EXIT});
        dut->launch(3);
        require(dut->run().completed(), "reset retained barrier wait state");
    }
    dut->load_program({branch(1, 0, 0, 16), jump(0, 0), BARW, EXIT});
    dut->launch(32);
    for (unsigned thread = 0; thread < 32; ++thread) {
        dut->write_register(thread, 1, thread % 2);
    }
    const auto stalled = dut->run(100);
    require(!stalled.completed() && stalled.m_status == RunStatus::BUDGET_EXHAUSTED &&
                stalled.m_fault.m_message.empty(),
        "nonarriving loop treated as barrier fault");
}
}

// 检查发散路径在共同 PC 汇合后每线程恰好执行一次，并验证循环与跳转链接。
void wcu_test() {
    check_barriers();
    auto dut = make_dut();
    dut->load_program({branch(1, 0, 0, 24),
        imm(0x02, 2, 0, 0, 10),
        jump(0, 16),
        imm(0x02, 2, 0, 0, 20),
        imm(0x02, 3, 0, 2, 1),
        imm(0x02, 4, 0, 4, 1),
        branch(4, 5, 2, -8),
        jump(6, 16),
        0xffffffffU,
        imm(0x0a, 7, 0, 8, 0),
        0xffffffffU,
        EXIT});
    dut->launch(47);
    for (unsigned thread = 0; thread < 47; ++thread) {
        dut->write_register(thread, 1, thread % 2);
        dut->write_register(thread, 5, 1 + thread % 4);
        dut->write_register(thread, 8, 88);
    }
    require(dut->run().completed(), "divergent program failed");
    for (unsigned thread = 0; thread < 47; ++thread) {
        require(dut->read_register(thread, 3) == (thread % 2 ? 11U : 21U),
            "divergent path result mismatch");
        require(
            dut->read_register(thread, 4) == 1 + thread % 4, "divergent loop termination mismatch");
        require(dut->read_register(thread, 6) == 64, "JAL return address mismatch");
        require(dut->read_register(thread, 7) == 80, "JALR return address mismatch");
    }
    dut->load_program({imm(0x0a, 1, 0, 1, 0), 0xffffffffU, EXIT});
    dut->launch(1);
    dut->write_register(0, 1, 16);
    require(dut->run().completed() && dut->read_register(0, 1) == 8,
        "JALR rd=rs1 did not use old source");
    dut->load_program({EXIT, 0xffffffffU, jump(1, -16)});
    dut->launch(1, 16);
    require(dut->run().completed() && dut->read_register(0, 1) == 24, "negative JAL offset failed");
    dut->load_program({branch(1, 0, 0, 4), EXIT});
    dut->launch(1);
    dut->write_register(0, 1, 1);
    require(dut->run().completed(), "untaken misaligned branch trapped");
    dut->launch(1);
    require(!dut->run().m_fault.m_message.empty(), "taken misaligned branch did not trap");
    const unsigned conditions[] = {0, 1, 2, 3, 4, 5};
    const bool taken[] = {false, true, true, false, false, true};
    for (unsigned index = 0; index < 6; ++index) {
        dut->load_program({branch(1, 2, conditions[index], 16), imm(0x02, 3, 0, 0, 1), EXIT});
        dut->launch(1);
        dut->write_register(0, 1, 0xffffffffU);
        dut->write_register(0, 2, 1);
        require(dut->run().completed(), "branch condition program failed");
        require(dut->read_register(0, 3) == (taken[index] ? 0U : 1U), "branch condition mismatch");
    }
}
}
