#include "icache_test.h"
#include "common/graphics_checks.h"
#include "dut/memory_dut.h"

namespace helo::verif {
// 验证八字节取指、完整范围检查以及与数据写入的即时一致性。
void icache_test() {
    auto dut = create_memory_dut(20);
    dut->load_instructions(0, {0x0123456789abcdefULL, 0xfedcba9876543210ULL});
    graphics_check(dut->fetch(0) == 0x0123456789abcdefULL, "full little-endian instruction fetch");
    graphics_check(dut->fetch(8) == 0xfedcba9876543210ULL, "second instruction fetch");
    dut->data_write(0, 4, 0xaabbccdd);
    dut->write(4, 4, 0x10203040);
    graphics_check(
        dut->fetch(0) == 0x10203040aabbccddULL, "instruction fetch observes both memory ports");
    for (std::uint32_t address = 1; address < 8; ++address) {
        graphics_reject([&] { dut->fetch(address); }, "eight-byte fetch alignment enforced");
    }
    dut->write(16, 4, 0x55667788);
    graphics_reject([&] { dut->fetch(16); }, "fetch rejects missing upper half");
    graphics_check(dut->read(16, 4) == 0x55667788, "failed fetch preserves lower half");
    graphics_reject([&] { dut->fetch(24); }, "fetch rejects complete out-of-range instruction");
    graphics_reject([&] { dut->fetch(0xfffffff8u); }, "highest aligned PC does not wrap");
    graphics_reject([&] { dut->fetch(0xfffffffcu); },
        "highest word-aligned PC still requires eight-byte alignment");
}
}
