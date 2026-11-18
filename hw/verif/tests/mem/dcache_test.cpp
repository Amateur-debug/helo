#include "dcache_test.h"
#include "common/graphics_checks.h"
#include "dut/memory_dut.h"

namespace helo::verif {
// 验证数据端口的宽度、地址规则及三个端口之间的统一内存可见性。
void dcache_test() {
    auto dut = create_memory_dut(16);
    dut->write(0, 4, 0xdeadbeef);
    graphics_check(dut->data_read(0, 4) == 0xdeadbeef, "data port observes memory writes");
    dut->data_write(0, 1, 0x12);
    dut->data_write(2, 2, 0x3456);
    graphics_check(dut->read(0, 4) == 0x3456be12, "memory observes data port narrow writes");
    graphics_check(
        dut->data_read(0, 1) == 0x12 && dut->data_read(2, 2) == 0x3456, "data port narrow reads");
    dut->data_write(4, 4, 0x789abcde);
    graphics_check(dut->fetch(0) == 0x789abcde3456be12ULL, "icache observes dcache stores");
    dut->data_write(12, 4, 0x01020304);
    dut->data_validate(12, 4);
    graphics_check(dut->data_read(12, 4) == 0x01020304, "last aligned word accepted");
    // 对每种失败请求比较整个内存，避免错误写入留下局部副作用。
    const auto reject_access = [&](std::uint32_t address, unsigned size) {
        const auto before0 = dut->fetch(0), before1 = dut->fetch(8);
        graphics_reject([&] { dut->data_read(address, size); }, "invalid dcache read");
        graphics_reject([&] { dut->data_write(address, size, 0); }, "invalid dcache write");
        graphics_reject([&] { dut->data_validate(address, size); }, "invalid dcache validation");
        graphics_check(dut->fetch(0) == before0 && dut->fetch(8) == before1,
            "invalid dcache request has no side effects");
    };
    reject_access(1, 2);
    reject_access(2, 4);
    reject_access(16, 1);
    reject_access(16, 4);
    reject_access(0xffffffffu, 1);
    reject_access(0xfffffffcu, 4);
    reject_access(0, 0);
    reject_access(0, 3);
    reject_access(0, 8);
}
}
