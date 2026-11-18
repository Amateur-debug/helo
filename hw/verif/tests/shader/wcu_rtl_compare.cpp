#include "Vhelo_wcu.h"
#include "shader/wcu.h"
#include "verilated.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {
// 用稳定的种子构造控制路径输入。
std::uint32_t next_random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// 比较单个lane的分支目标、链接结果及协议故障。
void compare_case(Vhelo_wcu& top,
    const helo::func::Wcu& reference,
    helo::func::Operation operation,
    unsigned family,
    unsigned funct,
    std::uint32_t pc,
    std::uint32_t a,
    std::uint32_t b,
    std::uint32_t offset,
    std::uint32_t launch_mask,
    std::uint32_t live_mask,
    bool barrier_pending,
    std::uint32_t barrier_pc) {
    top.family = family;
    top.funct = funct;
    top.pc = pc;
    top.src1 = a;
    top.src2 = b;
    top.immediate = offset;
    top.launch_mask = launch_mask;
    top.live_mask = live_mask;
    top.barrier_pending = barrier_pending;
    top.barrier_pc = barrier_pc;
    top.eval();

    helo::func::Warp warp;
    warp.m_launch_mask = launch_mask;
    warp.m_live_mask = live_mask;
    warp.m_barrier_mask = barrier_pending ? 1 : 0;
    warp.m_barrier_pc = barrier_pc;
    helo::func::IssuedInstr issued;
    issued.m_decoded.m_path.m_mask = 1;
    issued.m_decoded.m_path.m_pc = pc;
    issued.m_decoded.m_instr.m_operation = operation;
    issued.m_decoded.m_instr.m_immediate = offset;
    issued.m_src1[0] = a;
    issued.m_src2[0] = b;
    helo::func::ExecutionResult expected;
    bool rejected = false;
    try {
        reference.validate(warp, issued);
        expected = reference.execute(issued);
    } catch (const std::exception&) {
        rejected = true;
    }
    if (bool(top.fault) != rejected || top.illegal) {
        std::cerr << "WCU fault mismatch family=" << family << " funct=" << funct << " pc=0x"
                  << std::hex << pc << " a=0x" << a << " b=0x" << b << " offset=0x" << offset
                  << std::dec << '\n';
        throw std::runtime_error("control-path fault differs from functional model");
    }
    if (!rejected && (top.next_pc != expected.m_next_pc[0] || top.link_value != pc + 8 ||
                         bool(top.link_write) != (operation == helo::func::Operation::JAL ||
                                                     operation == helo::func::Operation::JALR) ||
                         bool(top.exit_thread) != (expected.m_exit_mask != 0) ||
                         bool(top.barrier_arrive) != (operation == helo::func::Operation::BARW))) {
        throw std::runtime_error("control-path result differs from functional model");
    }
}

// 对全部分支条件和控制指令执行边界与随机比较。
void run_test() {
    VerilatedContext context;
    Vhelo_wcu top(&context);
    helo::func::Wcu reference;
    constexpr helo::func::Operation BRANCHES[]{helo::func::Operation::BEQ,
        helo::func::Operation::BNE,
        helo::func::Operation::BLT,
        helo::func::Operation::BGE,
        helo::func::Operation::BLTU,
        helo::func::Operation::BGEU};
    constexpr std::uint32_t EDGES[]{0, 1, 0x7fffffff, 0x80000000, 0xffffffff};
    for (unsigned op = 0; op < 6; ++op) {
        for (auto a : EDGES) {
            for (auto b : EDGES) {
                for (std::uint32_t offset : {0u, 4u, 8u, 0xfffffff8u}) {
                    compare_case(
                        top, reference, BRANCHES[op], 8, op, 0x100, a, b, offset, 1, 1, false, 0);
                }
            }
        }
    }
    std::uint32_t state = 0x57435531;
    for (unsigned trial = 0; trial < 16384; ++trial) {
        const auto pc = next_random(state) & ~7u;
        const auto a = next_random(state);
        const auto b = next_random(state);
        const auto offset = next_random(state);
        const auto op = trial % 11;
        if (op < 6) {
            compare_case(top, reference, BRANCHES[op], 8, op, pc, a, b, offset, 1, 1, false, 0);
        } else if (op == 6) {
            compare_case(
                top, reference, helo::func::Operation::JAL, 9, 0, pc, a, b, offset, 1, 1, false, 0);
        } else if (op == 7) {
            compare_case(top,
                reference,
                helo::func::Operation::JALR,
                10,
                0,
                pc,
                a,
                b,
                offset,
                1,
                1,
                false,
                0);
        } else {
            const auto operation = op == 8   ? helo::func::Operation::EXIT
                                   : op == 9 ? helo::func::Operation::TRAP
                                             : helo::func::Operation::BARW;
            const auto funct = op == 8 ? 1u : op == 9 ? 2u : 4u;
            const bool pending = (a & 1) != 0;
            compare_case(top,
                reference,
                operation,
                12,
                funct,
                pc,
                a,
                b,
                offset,
                3,
                (b & 1) ? 3u : 1u,
                pending,
                (b & 2) ? pc : pc + 8);
        }
    }
    top.family = 0xff;
    top.eval();
    if (!top.illegal || top.link_write || top.exit_thread || top.barrier_arrive) {
        throw std::runtime_error("unassigned control operation must not commit");
    }
}
}

// 向CTest传播控制单元的真实执行结果。
int main() {
    try {
        run_test();
        std::cout << "wcu: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
