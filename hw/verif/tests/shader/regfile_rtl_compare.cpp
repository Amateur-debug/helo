#include "Vhelo_regfile.h"
#include "shader/regfile.h"
#include "verilated.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {
// 推进一次寄存器文件写入时钟，并撤销上一次请求。
void tick(Vhelo_regfile& top) {
    top.clk = 1;
    top.eval();
    top.clk = 0;
    top.configure_valid = 0;
    top.write_valid = 0;
    top.write_fcsr_valid = 0;
    top.clear = 0;
    top.eval();
}

// 返回稳定可复现的寄存器输入。
std::uint32_t next_random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// 比较一个线程的GPR、r0和FCSR视图。
void compare_slot(Vhelo_regfile& top,
    const helo::func::Regfile& reference,
    unsigned warp,
    unsigned lane,
    unsigned reg) {
    top.read_valid = 1;
    top.read_warp = warp;
    top.read_lane = lane;
    top.read_reg = reg;
    top.eval();
    if (top.read_error || top.fcsr_read_error ||
        top.read_data != reference.read_gpr(warp, lane, reg) ||
        top.read_fcsr != reference.read_fcsr(warp, lane)) {
        throw std::runtime_error("RTL register contents differ from functional model");
    }
}

// 比较任务配置结果，失败配置必须保留原状态。
void configure(
    Vhelo_regfile& top, helo::func::Regfile& reference, unsigned warps, unsigned registers) {
    bool rejected = false;
    try {
        reference.configure(warps, registers);
    } catch (const std::exception&) {
        rejected = true;
    }
    top.configure_valid = 1;
    top.configure_warps = warps;
    top.configure_registers = registers;
    top.eval();
    if (bool(top.configure_error) != rejected) {
        throw std::runtime_error("RTL and model resource admission differ");
    }
    tick(top);
    if (!rejected && (top.active_warps != warps || top.register_count != registers)) {
        throw std::runtime_error("accepted RTL layout was not installed");
    }
}

// 覆盖容量接纳、隔离、非法请求及清零边界。
void run_test() {
    VerilatedContext context;
    Vhelo_regfile top(&context);
    helo::func::Regfile reference(2, 8192);
    top.clk = 0;
    top.reset = 1;
    top.configure_valid = 0;
    top.write_valid = 0;
    top.write_fcsr_valid = 0;
    top.read_valid = 0;
    top.clear = 0;
    top.eval();
    tick(top);
    top.reset = 0;
    top.eval();
    configure(top, reference, 2, 33);
    compare_slot(top, reference, 0, 0, 0);
    compare_slot(top, reference, 1, 31, 32);

    std::uint32_t state = 0x52454746;
    for (unsigned trial = 0; trial < 8192; ++trial) {
        const unsigned warp = next_random(state) % 2;
        const unsigned lane = next_random(state) % 32;
        const unsigned reg = next_random(state) % 33;
        const auto value = next_random(state);
        top.read_valid = 0;
        top.write_valid = 1;
        top.write_warp = warp;
        top.write_lane = lane;
        top.write_reg = reg;
        top.write_data = value;
        top.write_fcsr_valid = (trial % 5 == 0);
        top.write_fcsr_data = value;
        top.eval();
        if (top.write_error || top.fcsr_write_error) {
            throw std::runtime_error("valid register write rejected");
        }
        reference.write_scalar(warp, lane, reg, value);
        if (trial % 5 == 0) {
            reference.write_fcsr(warp, lane, value);
        }
        tick(top);
        compare_slot(top, reference, warp, lane, reg);
    }
    for (unsigned warp = 0; warp < 2; ++warp) {
        for (unsigned lane = 0; lane < 32; ++lane) {
            for (unsigned reg = 0; reg < 33; ++reg) {
                compare_slot(top, reference, warp, lane, reg);
            }
        }
    }

    top.read_valid = 1;
    top.read_warp = 2;
    top.read_reg = 1;
    top.eval();
    if (!top.read_error || !top.fcsr_read_error) {
        throw std::runtime_error("out-of-range warp read accepted");
    }
    top.read_warp = 0;
    top.read_reg = 33;
    top.eval();
    if (!top.read_error || top.fcsr_read_error) {
        throw std::runtime_error("out-of-range register read accepted");
    }
    top.read_valid = 0;
    top.write_valid = 1;
    top.write_warp = 0;
    top.write_lane = 0;
    top.write_reg = 33;
    top.write_data = 0xdeadbeef;
    top.eval();
    if (!top.write_error) {
        throw std::runtime_error("out-of-range register write accepted");
    }
    tick(top);
    configure(top, reference, 2, 34);
    configure(top, reference, 3, 1);
    configure(top, reference, 1, 0);
    configure(top, reference, 1, 257);
    compare_slot(top, reference, 0, 0, 1);
    compare_slot(top, reference, 1, 31, 32);

    configure(top, reference, 2, 33);
    for (unsigned warp = 0; warp < 2; ++warp) {
        for (unsigned lane = 0; lane < 32; ++lane) {
            for (unsigned reg = 0; reg < 33; ++reg) {
                compare_slot(top, reference, warp, lane, reg);
            }
        }
    }
    configure(top, reference, 2, 1);
    compare_slot(top, reference, 1, 31, 0);
    top.read_valid = 1;
    top.read_warp = 0;
    top.read_reg = 1;
    top.eval();
    if (!top.read_error) {
        throw std::runtime_error("r0-only layout exposed undeclared register");
    }
}
}

// 向CTest报告寄存器文件差分结果。
int main() {
    try {
        run_test();
        std::cout << "regfile: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
