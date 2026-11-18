#include "Vhelo_shader_lane.h"
#include "verilated.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {
// 以独立编码器生成R型指令。
std::uint64_t reg(unsigned opcode, unsigned funct, unsigned rd, unsigned rs1, unsigned rs2) {
    return (std::uint64_t(funct) << 40) | (std::uint64_t(rs2) << 24) | (std::uint64_t(rs1) << 16) |
           (std::uint64_t(rd) << 8) | opcode;
}

// 以独立编码器生成I型指令。
std::uint64_t imm(unsigned opcode, unsigned funct, unsigned rd, unsigned rs1, int value) {
    return (std::uint64_t(static_cast<std::uint32_t>(value) & 4095) << 32) |
           (std::uint64_t(funct) << 24) | (std::uint64_t(rs1) << 16) | (std::uint64_t(rd) << 8) |
           opcode;
}

// 逐条检查机器码译码与整数/控制单元之间的组合连接。
void run_test() {
    VerilatedContext context;
    Vhelo_shader_lane top(&context);
    top.pc = 0x100;
    top.src1 = 0x80000000;
    top.src2 = 0xffffffff;
    top.live_mask = 1;
    top.launch_mask = 1;
    top.barrier_pending = 0;
    top.barrier_pc = 0;
    top.data_rdata = 0;
    top.data_error = 0;

    top.encoding = reg(3, 4, 3, 1, 2);
    top.eval();
    if (!top.valid || !top.writeback || top.rd != 3 || top.rs1 != 1 || top.rs2 != 2 ||
        top.result != 0x80000000 || top.next_pc != 0x108) {
        throw std::runtime_error("MULDIV decode-to-IU execution failed");
    }
    top.encoding = imm(2, 0, 4, 3, -2048);
    top.src1 = 0x80000000;
    top.eval();
    if (!top.valid || top.immediate != 0xfffff800 || top.result != 0x7ffff800 || top.rd != 4 ||
        top.rs1 != 3 || top.next_pc != 0x108) {
        throw std::runtime_error("sign-extended immediate execution failed");
    }
    top.encoding = (std::uint64_t(12) << 32) | (std::uint64_t(0) << 24) | (std::uint64_t(2) << 16) |
                   (std::uint64_t(1) << 8) | 8;
    top.src1 = 9;
    top.src2 = 9;
    top.eval();
    if (!top.valid || top.writeback || top.next_pc != 0x118 || top.rs1 != 1 || top.rs2 != 2) {
        throw std::runtime_error("taken branch integration failed");
    }
    top.src2 = 8;
    top.eval();
    if (!top.valid || top.next_pc != 0x108) {
        throw std::runtime_error("untaken branch integration failed");
    }
    top.encoding = (std::uint64_t(8) << 32) | (std::uint64_t(6) << 8) | 9;
    top.eval();
    if (!top.valid || !top.writeback || top.rd != 6 || top.result != 0x108 ||
        top.next_pc != 0x110) {
        throw std::runtime_error("JAL link integration failed");
    }
    top.encoding = imm(10, 0, 7, 1, 8);
    top.src1 = 0x200;
    top.eval();
    if (!top.valid || !top.writeback || top.result != 0x108 || top.next_pc != 0x208) {
        throw std::runtime_error("JALR link integration failed");
    }
    top.encoding = imm(10, 0, 7, 1, 4);
    top.eval();
    if (top.valid || !top.fault || top.writeback) {
        throw std::runtime_error("misaligned target partially committed");
    }
    top.encoding = 0x0100000c;
    top.eval();
    if (!top.valid || !top.exit_thread || top.writeback) {
        throw std::runtime_error("EXIT integration failed");
    }
    top.encoding = 0x0400000c;
    top.eval();
    if (!top.valid || !top.barrier_arrive || top.next_pc != top.pc) {
        throw std::runtime_error("BARW arrival integration failed");
    }
    top.encoding = 0xdeadbeef;
    top.eval();
    if (top.valid || !top.fault || top.writeback || top.exit_thread || top.barrier_arrive) {
        throw std::runtime_error("illegal instruction produced effects");
    }
    top.encoding = imm(4, 4, 8, 1, 0);
    top.eval();
    if (!top.valid || top.unsupported || top.fault || !top.writeback || !top.data_valid ||
        top.data_write || top.data_size != 2 || top.data_addr != top.src1) {
        throw std::runtime_error("LSU request integration failed");
    }
    top.data_error = 1;
    top.eval();
    if (top.valid || !top.fault || top.writeback || top.next_pc != top.pc) {
        throw std::runtime_error("faulting LSU request produced writeback");
    }
    top.encoding = reg(6, 0, 8, 1, 2);
    top.eval();
    if (top.valid || !top.unsupported || top.fault || top.writeback) {
        throw std::runtime_error("unconnected FPU instruction silently succeeded");
    }
}
}

// 返回机器码单lane集成测试的执行状态。
int main() {
    try {
        run_test();
        std::cout << "shader lane: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
