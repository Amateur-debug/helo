#include "idu_test.h"
#include "shader_test.h"

namespace helo::verif {
// 检查各格式保留位、操作约束、未知组合与错误锁存。
void idu_test() {
    auto dut = make_dut();
    const std::vector<std::uint64_t> invalid = {0,
        0xff,
        reg(1, 11, 2, 1, 3),
        reg(3, 8, 2, 1, 3),
        reg(6, 18, 2, 1, 3),
        reg(1, 0, 2, 1, 3) | (1ULL << 32),
        reg(1, 0, 2, 1, 3) | (1ULL << 56),
        reg(1, 0, 2, 1, 3) | (1ULL << 48),
        imm(2, 2, 0, 1, 1) | (1ULL << 44),
        store(1, 2, 2) | (1ULL << 44),
        branch(1, 2, 0, 8) | (1ULL << 44),
        upper(2, 0, 1) | (1ULL << 52),
        upper(2, 0, 1) | (1ULL << 24),
        jump(2, 8) | (1ULL << 52),
        jump(2, 8) | (1ULL << 24),
        reg(1, 10, 2, 1, 3),
        fp(4, 2, 1, 3),
        fp(13, 2, 1, 3),
        fp(14, 2, 1, 3),
        fp(5, 2, 1, 3, 1),
        fp(0, 2, 1, 3, 5),
        fp(0, 2, 1, 3, 8),
        fused(4, 2, 1, 3, 4),
        fused(0, 2, 1, 3, 4) | (1ULL << 56),
        imm(2, 2, 4, 1, 32),
        imm(4, 2, 5, 1, 0),
        store(1, 2, 3),
        branch(1, 2, 6, 8),
        NOP | (1ULL << 8),
        EXIT | (1ULL << 16),
        FENCE | (1ULL << 32),
        BARW | (1ULL << 8),
        imm(12, 2, 5, 0, 0),
        imm(12, 2, 10, 32, 0),
        imm(12, 2, 8, 0, 0xfff),
        imm(12, 0, 2, 0, 4095)};
    for (const auto word : invalid) {
        dut->load_program({word, EXIT});
        dut->launch(32);
        for (unsigned thread = 0; thread < 32; ++thread) {
            dut->write_register(thread, 2, 0xdeadbeefU);
        }
        const auto result = dut->run();
        require(!result.completed() && !result.m_fault.m_message.empty(),
            "illegal instruction silently accepted");
        for (unsigned thread = 0; thread < 32; ++thread) {
            require(dut->read_register(thread, 2) == 0xdeadbeefU,
                "illegal instruction partially committed");
        }
    }
    dut->load_program({0});
    dut->launch(1);
    const auto trapped = dut->run();
    dut->load_program({EXIT});
    const auto locked = dut->run();
    require(!locked.completed() && locked.m_fault.m_message == trapped.m_fault.m_message,
        "fault was not latched after instruction repair");
    dut->reset();
    dut->launch(1);
    require(dut->run().completed(), "reset did not clear fault");
    dut->load_program({NOP, FENCE, EXIT});
    dut->launch(1);
    require(dut->run().completed(), "NOP or FENCE rejected");
    require(dut->read_memory(0, 4) == 12 && dut->read_memory(4, 4) == 0,
        "instruction little-endian encoding mismatch");
    bool rejected = false;
    try {
        dut->launch(1, 4);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "four-byte aligned instruction entry accepted");
}
}
