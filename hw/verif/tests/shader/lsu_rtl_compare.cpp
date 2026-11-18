#include "Vhelo_shader_lane_memory.h"
#include "mem/dcache.h"
#include "mem/mem.h"
#include "shader/lsu.h"
#include "verilated.h"
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {
constexpr std::array<helo::func::Operation, 8> OPERATIONS{helo::func::Operation::LB,
    helo::func::Operation::LH,
    helo::func::Operation::LW,
    helo::func::Operation::LBU,
    helo::func::Operation::LHU,
    helo::func::Operation::SB,
    helo::func::Operation::SH,
    helo::func::Operation::SW};

// 生成固定种子随机输入，保证失败可重现。
std::uint32_t next_random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// 用I/S格式独立编码器生成完整机器码。
std::uint64_t encode(unsigned operation, int offset) {
    const bool store = operation >= 5;
    const auto funct = store ? operation - 5 : operation;
    return (std::uint64_t(static_cast<unsigned>(offset) & 4095) << 32) |
           (std::uint64_t(funct) << 24) | (std::uint64_t(1) << 16) |
           (std::uint64_t(store ? 2 : 3) << 8) | (store ? 5 : 4);
}

// 推进一个时钟沿，并在再次访问存储前恢复低电平。
void tick(Vhelo_shader_lane_memory& top) {
    top.clk = 0;
    top.eval();
    top.clk = 1;
    top.eval();
    top.clk = 0;
    top.eval();
}

// 通过主机端口读取一个字，检查未提交的非法存储也不会改变字节。
void check_word(Vhelo_shader_lane_memory& top, const helo::func::Mem& reference, unsigned addr) {
    top.encoding = 12;
    top.host_valid = 1;
    top.host_write = 0;
    top.host_addr = addr;
    top.host_size = 4;
    top.eval();
    if (top.host_error || top.host_rdata != reference.read(addr, 4)) {
        throw std::runtime_error("RTL shared memory differs from functional model");
    }
    top.host_valid = 0;
    top.eval();
}

// 比较一条机器码在参考LSU和Verilated单lane内存上的执行结果。
void compare_case(Vhelo_shader_lane_memory& top,
    helo::func::Lsu& lsu,
    const helo::func::Mem& reference,
    unsigned operation,
    std::uint32_t base,
    int offset,
    std::uint32_t value,
    std::uint32_t pc,
    unsigned sample_addr) {
    helo::func::IssuedInstr instruction;
    instruction.m_decoded.m_instr.m_operation = OPERATIONS.at(operation);
    instruction.m_decoded.m_instr.m_immediate = static_cast<std::uint32_t>(offset);
    instruction.m_decoded.m_path.m_mask = 1;
    instruction.m_decoded.m_path.m_pc = pc;
    instruction.m_src1[0] = base;
    instruction.m_src2[0] = value;

    top.host_valid = 0;
    top.encoding = encode(operation, offset);
    top.pc = pc;
    top.src1 = base;
    top.src2 = value;
    top.eval();

    bool expected_fault = false;
    std::uint32_t expected_result = 0;
    try {
        expected_result = lsu.execute(instruction).m_values[0];
    } catch (const std::exception&) {
        expected_fault = true;
    }
    const bool store = operation >= 5;
    if (top.fault != expected_fault || top.valid == expected_fault || top.unsupported ||
        top.writeback != (!expected_fault && !store) ||
        top.next_pc != (expected_fault ? pc : pc + 8) ||
        (!expected_fault && !store && top.result != expected_result) || top.rd != (store ? 0 : 3) ||
        top.rs1 != 1 || top.rs2 != (store ? 2 : 0)) {
        std::cerr << "LSU mismatch op=" << operation << " base=0x" << std::hex << base
                  << " offset=" << offset << " value=0x" << value << " pc=0x" << pc
                  << " fault=" << expected_fault << " rtl_fault=" << int(top.fault) << std::dec
                  << '\n';
        throw std::runtime_error("LSU execution differs from functional model");
    }
    tick(top);
    check_word(top, reference, sample_addr);
}

// 差分覆盖所有宽度、符号扩展、负偏移、回绕、对齐和读写时序。
void run_test() {
    VerilatedContext context;
    Vhelo_shader_lane_memory top(&context);
    helo::func::Mem memory(4096);
    helo::func::Dcache cache(memory);
    helo::func::Lsu lsu(cache);
    top.clk = 0;
    top.capacity_bytes = 4096;
    top.encoding = 12;
    top.live_mask = 1;
    top.launch_mask = 1;
    top.barrier_pending = 0;
    top.barrier_pc = 0;
    top.fetch_valid = 0;
    top.host_valid = 1;
    top.host_write = 1;
    top.host_size = 4;
    std::uint32_t seed = 0x4c535530;
    for (unsigned address = 0; address < 4096; address += 4) {
        top.host_addr = address;
        top.host_wdata = next_random(seed);
        top.eval();
        tick(top);
        memory.write(address, 4, top.host_wdata);
    }
    top.host_valid = 0;

    constexpr std::array<std::uint32_t, 16> EDGES{
        0, 1, 2, 3, 4, 8, 127, 1024, 2047, 2048, 4091, 4092, 4093, 4094, 4095, 0xffffffff};
    constexpr std::array<int, 8> OFFSETS{0, 1, 2, 3, 4, -1, -4, -2048};
    for (unsigned operation = 0; operation < OPERATIONS.size(); ++operation) {
        for (const auto base : EDGES) {
            for (const auto offset : OFFSETS) {
                const auto address = base + static_cast<std::uint32_t>(offset);
                const auto sample = address < 4096 ? address & ~3u : 0u;
                compare_case(top,
                    lsu,
                    memory,
                    operation,
                    base,
                    offset,
                    next_random(seed),
                    0xfffffff8,
                    sample);
            }
        }
    }
    for (unsigned trial = 0; trial < 8192; ++trial) {
        const auto operation = next_random(seed) % OPERATIONS.size();
        const auto base = next_random(seed) % 4120;
        const auto offset = static_cast<int>(next_random(seed) % 4096) - 2048;
        const auto address = base + static_cast<std::uint32_t>(offset);
        const auto sample = address < 4096 ? address & ~3u : next_random(seed) % 1024 * 4;
        compare_case(top,
            lsu,
            memory,
            operation,
            base,
            offset,
            next_random(seed),
            next_random(seed),
            sample);
    }
    top.encoding = encode(7, 0) | (std::uint64_t(1) << 63);
    top.src1 = 256;
    top.eval();
    if (top.valid || !top.fault || top.writeback) {
        throw std::runtime_error("illegal store encoding was accepted");
    }
    tick(top);
    check_word(top, memory, 256);

    top.encoding = encode(7, 0);
    top.src1 = 256;
    top.src2 = 0x12345678;
    top.eval();
    if (!top.valid || top.fault) {
        throw std::runtime_error("fetch visibility store failed");
    }
    tick(top);
    memory.write(256, 4, 0x12345678);
    top.encoding = 12;
    top.fetch_valid = 1;
    top.fetch_addr = 256;
    top.eval();
    const auto expected_fetch =
        std::uint64_t(memory.read(256, 4)) | (std::uint64_t(memory.read(260, 4)) << 32);
    if (top.fetch_error || top.fetch_instr != expected_fetch) {
        throw std::runtime_error("store not visible at instruction fetch port");
    }
}
}

// 将差分验证失败传递给CTest。
int main() {
    try {
        run_test();
        std::cout << "shader LSU shared-memory compare: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
