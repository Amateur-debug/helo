#include "Vhelo_iu.h"
#include "shader/iu.h"
#include "verilated.h"
#include <array>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
// 保持与RTL操作码顺序独立的功能模型映射。
constexpr std::array<helo::func::Operation, 23> OPERATIONS{helo::func::Operation::LUI,
    helo::func::Operation::AUIPC,
    helo::func::Operation::ADD,
    helo::func::Operation::SUB,
    helo::func::Operation::SLL,
    helo::func::Operation::SLT,
    helo::func::Operation::SLTU,
    helo::func::Operation::XOR,
    helo::func::Operation::SRL,
    helo::func::Operation::SRA,
    helo::func::Operation::OR,
    helo::func::Operation::AND,
    helo::func::Operation::MUL,
    helo::func::Operation::MULHU,
    helo::func::Operation::MULH,
    helo::func::Operation::MULHSU,
    helo::func::Operation::DIVU,
    helo::func::Operation::REMU,
    helo::func::Operation::DIV,
    helo::func::Operation::REM,
    helo::func::Operation::MOV,
    helo::func::Operation::NOP,
    helo::func::Operation::FENCE};

// 生成可复现的操作数序列。
std::uint32_t next_random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// 比较一个已译码整数操作的结果与非法操作标志。
void compare_case(Vhelo_iu& top,
    const helo::func::Iu& reference,
    unsigned op,
    std::uint32_t a,
    std::uint32_t b,
    std::uint32_t immediate,
    std::uint32_t pc,
    bool immediate_operand) {
    helo::func::IssuedInstr issued;
    issued.m_decoded.m_path.m_mask = 1;
    issued.m_decoded.m_path.m_pc = pc;
    issued.m_decoded.m_instr.m_operation = OPERATIONS.at(op);
    issued.m_decoded.m_instr.m_immediate_operand = immediate_operand;
    issued.m_decoded.m_instr.m_immediate = immediate;
    issued.m_src1[0] = a;
    issued.m_src2[0] = b;
    const auto expected = reference.execute(issued).m_values[0];

    top.operation = op;
    top.src1 = a;
    top.src2 = b;
    top.immediate = immediate;
    top.immediate_operand = immediate_operand;
    top.pc = pc;
    top.eval();
    if (top.illegal || top.result != expected) {
        std::cerr << "IU mismatch op=" << op << " a=0x" << std::hex << a << " b=0x" << b
                  << " imm=0x" << immediate << " pc=0x" << pc << " immed=" << immediate_operand
                  << " expected=0x" << expected << " actual=0x" << top.result << std::dec << '\n';
        throw std::runtime_error("integer datapath differs from functional model");
    }
}

// 对全部整数操作覆盖边界笛卡尔积与固定种子随机输入。
void run_test() {
    VerilatedContext context;
    Vhelo_iu top(&context);
    helo::func::Iu reference;
    constexpr std::array<std::uint32_t, 11> EDGES{
        0, 1, 2, 31, 32, 33, 0x7fffffff, 0x80000000, 0x80000001, 0xfffffffe, 0xffffffff};
    for (unsigned op = 0; op < OPERATIONS.size(); ++op) {
        for (const auto a : EDGES) {
            for (const auto b : EDGES) {
                compare_case(top, reference, op, a, b, b, 0xfffffff8u, false);
                compare_case(top, reference, op, a, b, b, 0x12345678u, true);
            }
        }
    }
    std::uint32_t seed = 0x48454c4f;
    for (unsigned trial = 0; trial < 16384; ++trial) {
        const auto op = next_random(seed) % OPERATIONS.size();
        const auto a = next_random(seed);
        const auto b = next_random(seed);
        const auto immediate = next_random(seed);
        const auto pc = next_random(seed);
        compare_case(top, reference, op, a, b, immediate, pc, trial & 1);
    }
    top.operation = 31;
    top.eval();
    if (!top.illegal || top.result != 0) {
        throw std::runtime_error("unassigned IU operation must be rejected");
    }
}
}

// 把差分结果传递给CTest并报告真实失败状态。
int main() {
    try {
        run_test();
        std::cout << "iu datapath: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
