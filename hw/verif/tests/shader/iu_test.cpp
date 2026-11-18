#include "iu_test.h"
#include "shader_test.h"

namespace helo::verif {
// 检查整数运算溢出、符号比较与移位低五位语义。
void iu_test() {
    auto dut = make_dut();
    dut->load_program({reg(0x01, 0, 3, 1, 2),
        reg(0x01, 1, 4, 1, 2),
        reg(0x01, 8, 5, 1, 2),
        reg(0x01, 9, 6, 1, 2),
        reg(0x01, 5, 7, 1, 2),
        reg(0x01, 6, 8, 1, 2),
        reg(0x01, 7, 9, 1, 2),
        reg(0x01, 4, 10, 1, 2),
        reg(0x01, 3, 11, 1, 2),
        reg(0x01, 2, 12, 1, 2),
        imm(0x02, 13, 0, 1, -1),
        imm(0x02, 14, 7, 1, 0),
        imm(0x02, 15, 8, 1, -1),
        imm(0x02, 16, 3, 1, -1),
        imm(0x02, 17, 2, 0, -1),
        imm(0x02, 18, 1, 1, 255),
        imm(0x02, 19, 4, 1, 31),
        imm(0x02, 20, 5, 1, 31),
        imm(0x02, 21, 6, 1, 31),
        upper(22, 0, 0x12345),
        upper(23, 1, 1),
        EXIT});
    dut->launch(32);
    for (unsigned thread = 0; thread < 32; ++thread) {
        dut->write_register(thread, 1, 0x80000001U);
        dut->write_register(thread, 2, 33);
    }
    require(dut->run().completed(), "integer program failed");
    const std::uint32_t expected[] = {0x80000022U,
        0x7fffffe0U,
        1,
        0,
        2,
        0x40000000U,
        0xc0000000U,
        0x80000020U,
        0x80000021U,
        1,
        0x80000000U,
        1,
        1,
        0x7ffffffeU,
        0xffffffffU,
        1,
        0x80000000U,
        1,
        0xffffffffU,
        0x12345000U,
        0x10a0U};
    for (unsigned thread = 0; thread < 32; ++thread) {
        for (unsigned index = 0; index < 21; ++index) {
            require(dut->read_register(thread, index + 3) == expected[index],
                "integer result mismatch");
        }
    }

    // 使用手工预期值覆盖乘除的符号、高半结果和特殊边界。
    struct Case {
        std::uint32_t m_a; // 保存乘除测试的第一输入操作数。
        std::uint32_t m_b; // 保存乘除测试的第二输入操作数。
        std::array<std::uint32_t, 8> m_expected; // 按功能码0至7保存乘除与取余的预期结果。
    };

    const Case cases[] = {
        {0xfffffff9U,
            3,
            {0xffffffebU, 0xffffffffU, 0xffffffffU, 2, 0xfffffffeU, 0x55555553U, 0xffffffffU, 0}},
        {0x80000000U,
            0xffffffffU,
            {0x80000000U, 0, 0x80000000U, 0x7fffffffU, 0x80000000U, 0, 0, 0x80000000U}},
        {0xffffffffU, 0xffffffffU, {1, 0, 0xffffffffU, 0xfffffffeU, 1, 1, 0, 0}},
        {0x80000000U, 0, {0, 0, 0, 0, 0xffffffffU, 0xffffffffU, 0x80000000U, 0x80000000U}},
        {7, 3, {21, 0, 0, 0, 2, 2, 1, 1}},
        {0, 0, {0, 0, 0, 0, 0xffffffffU, 0xffffffffU, 0, 0}}};
    std::vector<std::uint64_t> program;
    for (unsigned op = 0; op < 8; ++op) {
        program.push_back(reg(0x03, op, op + 3, 1, 2));
    }
    program.push_back(reg(0x03, 0, 0, 1, 2));
    program.push_back(EXIT);
    dut->load_program(program);
    dut->launch(35);
    for (unsigned thread = 0; thread < 35; ++thread) {
        const auto& test = cases[thread % 6];
        dut->write_register(thread, 1, test.m_a);
        dut->write_register(thread, 2, test.m_b);
    }
    require(dut->run().completed(), "M extension program failed");
    for (unsigned thread = 0; thread < 35; ++thread) {
        for (unsigned op = 0; op < 8; ++op) {
            require(dut->read_register(thread, op + 3) == cases[thread % 6].m_expected[op],
                "M extension result mismatch");
        }
        require(dut->read_register(thread, 0) == 0, "M extension overwrote r0");
    }
}
}
