#include "mem_test.h"
#include "common/graphics_checks.h"
#include "dut/memory_dut.h"
#include <array>

namespace helo::verif {
// 验证统一内存的小端布局、合法访问和装载失败原子性。
void mem_test() {
    auto dut = create_memory_dut(32);
    for (std::uint32_t i = 0; i < 32; ++i) {
        graphics_check(dut->read(i, 1) == 0, "memory initially zero");
    }
    dut->write(0, 4, 0x89abcdef);
    const std::array<std::uint32_t, 4> bytes{0xef, 0xcd, 0xab, 0x89};
    for (std::uint32_t i = 0; i < 4; ++i) {
        graphics_check(dut->read(i, 1) == bytes[i], "little-endian byte order");
    }
    graphics_check(
        dut->read(0, 2) == 0xcdef && dut->read(2, 2) == 0x89ab, "little-endian halfwords");
    dut->write(1, 1, 0x12345678);
    dut->write(2, 2, 0xaabb1234);
    graphics_check(dut->read(0, 4) == 0x123478ef, "narrow stores truncate and preserve neighbors");
    dut->write(31, 1, 0x77);
    dut->write(28, 2, 0x3322);
    graphics_check(
        dut->read(31, 1) == 0x77 && dut->read(28, 2) == 0x3322, "last legal narrow accesses");
    // 检查错误地址或宽度的读写与纯验证都被拒绝且不改变内容。
    const auto reject_access = [&](std::uint32_t address, unsigned size) {
        std::array<std::uint32_t, 8> before{};
        for (unsigned i = 0; i < 8; ++i) {
            before[i] = dut->read(i * 4, 4);
        }
        graphics_reject([&] { dut->read(address, size); }, "invalid read rejected");
        graphics_reject([&] { dut->write(address, size, 0); }, "invalid write rejected");
        graphics_reject([&] { dut->validate(address, size); }, "invalid validation rejected");
        for (unsigned i = 0; i < 8; ++i) {
            graphics_check(dut->read(i * 4, 4) == before[i], "failed access preserves memory");
        }
    };
    reject_access(1, 2);
    reject_access(2, 4);
    reject_access(32, 1);
    reject_access(32, 4);
    reject_access(0xffffffffu, 1);
    reject_access(0xfffffffcu, 4);
    reject_access(0, 0);
    reject_access(0, 3);
    reject_access(0, 8);
    dut->load_words(8, {0x76543210, 0xfedcba98});
    graphics_check(
        dut->read(8, 4) == 0x76543210 && dut->read(12, 4) == 0xfedcba98, "data word load");
    dut->load_instructions(16, {0x0123456789abcdefULL, 0xfedcba9876543210ULL});
    graphics_check(dut->read(16, 4) == 0x89abcdef && dut->read(20, 4) == 0x01234567,
        "instruction low and high word layout");
    graphics_check(dut->read(24, 4) == 0x76543210 && dut->read(28, 4) == 0xfedcba98,
        "instruction sequence layout");
    std::array<std::uint32_t, 8> snapshot{};
    for (unsigned i = 0; i < 8; ++i) {
        snapshot[i] = dut->read(i * 4, 4);
    }
    graphics_reject([&] { dut->load_words(28, {1, 2}); }, "whole word load range checked");
    graphics_reject(
        [&] { dut->load_instructions(24, {1, 2}); }, "whole instruction load range checked");
    graphics_reject([&] { dut->load_words(2, {1}); }, "word load alignment");
    graphics_reject([&] { dut->load_instructions(4, {1}); }, "instruction load alignment");
    graphics_reject([&] { dut->load_words(0xfffffffcu, {1, 2}); }, "word address overflow");
    graphics_reject(
        [&] { dut->load_instructions(0xfffffff8u, {1, 2}); }, "instruction address overflow");
    for (unsigned i = 0; i < 8; ++i) {
        graphics_check(dut->read(i * 4, 4) == snapshot[i], "failed loads never partially commit");
    }
    dut->load_words(32, {});
    dut->load_instructions(32, {});
    auto empty = create_memory_dut(0);
    empty->load_words(0, {});
    empty->load_instructions(0, {});
    graphics_reject([&] { empty->read(0, 1); }, "empty memory rejects data");
}
}
