#include "memory_compare_test.h"
#include "Vhelo_memory.h"
#include "common/graphics_checks.h"
#include "dut/memory_dut.h"
#include "mem/icache.h"
#include "mem/mem.h"
#include "verilated.h"
#include <array>
#include <cstdint>
#include <exception>

namespace helo::verif {
namespace {
// 生成固定序列，保证失败轨迹可以复现。
std::uint32_t next_random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// 直接驱动两个写端口，检查冲突优先级及非法请求的无副作用边界。
void port_collision_test() {
    VerilatedContext context;
    Vhelo_memory top(&context);
    top.clk = 0;
    top.capacity_bytes = 16;
    top.host_valid = 1;
    top.host_write = 1;
    top.host_addr = 0;
    top.host_size = 4;
    top.host_wdata = 0x11223344;
    top.data_valid = 1;
    top.data_write = 1;
    top.data_addr = 2;
    top.data_size = 2;
    top.data_wdata = 0xaabb;
    top.fetch_valid = 1;
    top.fetch_addr = 0;
    top.eval();
    graphics_check(!top.host_error && !top.data_error && !top.fetch_error,
        "both write ports accept valid requests");
    graphics_check(top.fetch_instr == 0, "same-cycle fetch observes old memory");
    top.clk = 1;
    top.eval();
    top.clk = 0;
    top.host_valid = 0;
    top.data_valid = 0;
    top.eval();
    graphics_check(top.fetch_instr == 0xaabb3344ULL, "data port wins overlapping bytes");

    top.host_valid = 1;
    top.host_addr = 15;
    top.host_size = 4;
    top.data_valid = 1;
    top.data_addr = 4;
    top.data_size = 4;
    top.data_wdata = 0x12345678;
    top.eval();
    graphics_check(top.host_error && !top.data_error, "port errors remain independent");
    top.clk = 1;
    top.eval();
    top.clk = 0;
    top.host_valid = 0;
    top.data_valid = 0;
    top.eval();
    graphics_check(top.fetch_instr == 0x12345678aabb3344ULL,
        "invalid host write does not suppress valid data write");
}
}

// 逐步比较主机、数据与取指端口的返回值、拒绝状态和完整存储内容。
void memory_compare_test() {
    constexpr std::uint32_t BYTES = 257;
    auto rtl = create_memory_dut(BYTES);
    func::Mem reference(BYTES);
    func::Icache icache(reference);
    rtl->load_instructions(0, {0x0123456789abcdefULL, 0xfedcba9876543210ULL});
    reference.load_instructions(0, {0x0123456789abcdefULL, 0xfedcba9876543210ULL});

    // 每个操作分别捕获两端异常，任何成功值偏差都立即报告。
    const auto compare = [&](auto model_op, auto rtl_op) {
        std::uint64_t expected = 0;
        std::uint64_t actual = 0;
        bool model_rejected = false;
        bool rtl_rejected = false;
        try {
            expected = model_op();
        } catch (const std::exception&) {
            model_rejected = true;
        }
        try {
            actual = rtl_op();
        } catch (const std::exception&) {
            rtl_rejected = true;
        }
        graphics_check(model_rejected == rtl_rejected, "RTL and model rejection differ");
        if (!model_rejected) {
            graphics_check(expected == actual, "RTL and model values differ");
        }
    };

    constexpr std::array<unsigned, 6> SIZES{0, 1, 2, 3, 4, 8};
    std::uint32_t state = 0x9b7ac1e3;
    for (unsigned step = 0; step < 4096; ++step) {
        const auto random = next_random(state);
        const auto address =
            (random & 15) == 0 ? 0xfffffffcu : ((random & 15) == 1 ? BYTES : random % (BYTES + 16));
        const auto size = SIZES[next_random(state) % SIZES.size()];
        const auto value = next_random(state);
        switch (step % 6) {
        case 0:
            compare(
                [&] {
                    reference.write(address, size, value);
                    return 0ULL;
                },
                [&] {
                    rtl->write(address, size, value);
                    return 0ULL;
                });
            break;
        case 1:
            compare([&] { return std::uint64_t(reference.read(address, size)); },
                [&] { return std::uint64_t(rtl->read(address, size)); });
            break;
        case 2:
            compare(
                [&] {
                    reference.write(address, size, value);
                    return 0ULL;
                },
                [&] {
                    rtl->data_write(address, size, value);
                    return 0ULL;
                });
            break;
        case 3:
            compare([&] { return std::uint64_t(reference.read(address, size)); },
                [&] { return std::uint64_t(rtl->data_read(address, size)); });
            break;
        case 4:
            compare([&] { return icache.fetch(address); }, [&] { return rtl->fetch(address); });
            break;
        default:
            compare(
                [&] {
                    reference.validate(address, size);
                    return 0ULL;
                },
                [&] {
                    rtl->data_validate(address, size);
                    return 0ULL;
                });
            break;
        }
        if (step % 256 == 0) {
            for (std::uint32_t i = 0; i < BYTES; ++i) {
                graphics_check(
                    rtl->read(i, 1) == reference.read(i, 1), "RTL and model memory diverged");
            }
        }
    }
    for (std::uint32_t i = 0; i < BYTES; ++i) {
        graphics_check(rtl->read(i, 1) == reference.read(i, 1), "final memory state differs");
    }
    port_collision_test();
}
}
