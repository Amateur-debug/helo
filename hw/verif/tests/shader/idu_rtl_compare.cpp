#include "Vhelo_idu.h"
#include "shader/idu.h"
#include "verilated.h"
#include <array>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
// 生成确定性64位机器码字段。
std::uint32_t next_random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// 将参考模型整数操作转换为RTL整数单元的操作序号。
int iu_id(helo::func::Operation operation) {
    constexpr std::array<helo::func::Operation, 23> OPS{helo::func::Operation::LUI,
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
    for (unsigned i = 0; i < OPS.size(); ++i) {
        if (OPS[i] == operation) {
            return static_cast<int>(i);
        }
    }
    throw std::runtime_error("reference IU operation has no RTL mapping");
}

// 检查合法性及所有架构可见的译码字段。
void compare_word(Vhelo_idu& top, const helo::func::Idu& reference, std::uint64_t word) {
    top.encoding = word;
    top.eval();
    bool expected_legal = true;
    helo::func::Instr expected;
    try {
        expected = reference.decode(word);
        // RTL译码器实现基础ISA；软件纹理与工作组扩展仍须报告非法。
        const auto opcode = word & 255U;
        expected_legal = opcode != 0x0d && opcode != 0x0e;
    } catch (const std::exception&) {
        expected_legal = false;
    }
    if (bool(top.legal) != expected_legal) {
        std::cerr << "IDU legal mismatch word=0x" << std::hex << word << std::dec << '\n';
        throw std::runtime_error("instruction acceptance differs from functional model");
    }
    if (!expected_legal) {
        return;
    }
    const bool matching =
        top.unit == static_cast<unsigned>(expected.m_unit) && top.rd == expected.m_rd &&
        top.rs1 == expected.m_rs1 && top.rs2 == expected.m_rs2 && top.rs3 == expected.m_rs3 &&
        top.immediate == expected.m_immediate && top.rounding_mode == expected.m_rounding_mode &&
        bool(top.uses_rs1) == expected.m_uses_rs1 && bool(top.uses_rs2) == expected.m_uses_rs2 &&
        bool(top.uses_rs3) == expected.m_uses_rs3 && bool(top.uses_fcsr) == expected.m_uses_fcsr &&
        bool(top.writes_rd) == expected.m_writes_rd &&
        bool(top.immediate_operand) == expected.m_immediate_operand &&
        (expected.m_unit != helo::func::ExecutionUnit::IU ||
            top.iu_operation == iu_id(expected.m_operation));
    if (!matching) {
        std::cerr << "IDU fields mismatch word=0x" << std::hex << word << " expected immediate=0x"
                  << expected.m_immediate << " actual immediate=0x" << top.immediate << std::dec
                  << '\n';
        throw std::runtime_error("instruction fields differ from functional model");
    }
}

// 按每种物理格式产生有效操作数和立即数。
std::uint64_t valid_word(unsigned opcode, unsigned funct, std::uint32_t& state) {
    const auto rd = next_random(state) & 255;
    const auto a = next_random(state) & 255;
    const auto b = next_random(state) & 255;
    const auto c = next_random(state) & 255;
    const auto imm12 = next_random(state) & 4095;
    const auto imm20 = next_random(state) & 0xfffff;
    const auto r = [&](unsigned src2, unsigned ctrl, unsigned src3) {
        return (std::uint64_t(src3) << 32) | (std::uint64_t(funct) << 40) |
               (std::uint64_t(ctrl) << 48) | (std::uint64_t(src2) << 24) |
               (std::uint64_t(a) << 16) | (std::uint64_t(rd) << 8) | opcode;
    };
    const auto i = [&](unsigned dst, unsigned src, unsigned immediate) {
        return (std::uint64_t(immediate) << 32) | (std::uint64_t(funct) << 24) |
               (std::uint64_t(src) << 16) | (std::uint64_t(dst) << 8) | opcode;
    };
    switch (opcode) {
    case 1:
        return r(funct == 10 ? 0 : b, 0, 0);
    case 2:
        return i(rd, a, funct >= 4 && funct <= 6 ? imm12 & 31 : imm12);
    case 3:
        return r(b, 0, 0);
    case 4:
        return i(rd, a, imm12);
    case 5:
        return i(b, a, imm12);
    case 6: {
        const bool unary = funct == 4 || funct == 13 || (funct >= 14 && funct <= 17);
        const bool rounded = funct <= 4 || (funct >= 14 && funct <= 17);
        return r(unary ? 0 : b, rounded ? (c % 6 == 5 ? 7 : c % 5) : 0, 0);
    }
    case 7:
        return r(b, c % 6 == 5 ? 7 : c % 5, c);
    case 8:
        return i(a, b, imm12);
    case 9:
    case 11:
        return (std::uint64_t(imm20) << 32) | (std::uint64_t(funct) << 16) |
               (std::uint64_t(rd) << 8) | opcode;
    case 10:
        return i(rd, a, imm12);
    case 12:
        if (funct <= 4) {
            return i(0, 0, funct == 2 ? imm12 : 0);
        }
        return i(rd, funct >= 10 ? a & 31 : a, imm12);
    default:
        throw std::runtime_error("unknown instruction family");
    }
}

// 对每个分配功能码与大量非法字逐项比较。
void run_test() {
    VerilatedContext context;
    Vhelo_idu top(&context);
    helo::func::Idu reference;
    constexpr std::array<unsigned, 13> MAX_FUNCT{0, 10, 8, 7, 4, 2, 17, 3, 5, 0, 0, 1, 12};
    std::uint32_t seed = 0x49445531;
    unsigned valid_count = 0;
    for (unsigned opcode = 1; opcode <= 12; ++opcode) {
        for (unsigned funct = 0; funct <= MAX_FUNCT[opcode]; ++funct) {
            if (opcode == 12 && (funct == 5 || funct == 6)) {
                continue;
            }
            for (unsigned repeat = 0; repeat < 24; ++repeat) {
                const auto word = valid_word(opcode, funct, seed);
                compare_word(top, reference, word);
                if (!top.legal) {
                    throw std::runtime_error("valid instruction generator produced illegal word");
                }
                ++valid_count;
                for (unsigned bit : {31U, 44U, 52U, 56U, 63U}) {
                    compare_word(top, reference, word ^ (1ULL << bit));
                }
            }
        }
    }
    // 使用功能模型可执行的两个扩展字，显式检查RTL对各族的拒绝。
    for (const std::uint64_t word : {0x000005040302010dULL, 0x000004000000000eULL}) {
        (void)reference.decode(word);
        compare_word(top, reference, word);
    }
    for (unsigned trial = 0; trial < 32768; ++trial) {
        const std::uint64_t word = (std::uint64_t(next_random(seed)) << 32) | next_random(seed);
        compare_word(top, reference, word);
    }
    std::cout << "IDU valid encodings: " << valid_count << ", random words: 32768\n";
}
}

// 向CTest报告真实译码差分结果。
int main() {
    try {
        run_test();
        std::cout << "idu: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
