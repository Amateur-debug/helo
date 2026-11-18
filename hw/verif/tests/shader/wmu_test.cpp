#include "wmu_test.h"
#include "shader_test.h"
#include <limits>

namespace helo::verif {
namespace {
// 检查warp容量拒绝新任务时保留旧任务的寄存器、状态、内存及执行位置。
void check_rejected_launch() {
    DutConfig dut_config;
    dut_config.m_memory_bytes = 65536;
    dut_config.m_shader.m_max_warps = 2;
    auto dut = make_dut(dut_config);
    dut->load_program({imm(0x02, 1, 0, 1, 1), imm(0x02, 1, 0, 1, 2), EXIT});
    dut->launch(33);
    dut->write_register(0, 1, 10);
    dut->write_fcsr(0, 0x75);
    dut->write_memory(4096, 4, 0x12345678);
    require(dut->step(), "old task failed to start");
    for (const auto count : {std::size_t(65), std::numeric_limits<std::size_t>::max()}) {
        bool rejected = false;
        try {
            dut->launch(count);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "over-capacity launch accepted");
        require(dut->read_register(0, 1) == 11 && dut->read_fcsr(0) == 0x75,
            "failed launch destroyed registers");
        require(dut->read_memory(4096, 4) == 0x12345678, "failed launch destroyed memory");
    }
    require(dut->run().completed() && dut->read_register(0, 1) == 13,
        "failed launch changed old task PC");
    dut->launch(0);
    const auto empty = dut->run();
    require(empty.completed() && empty.m_steps == 0, "configured zero-thread task failed");
    dut->launch(1);
    require(dut->read_register(0, 1) == 0 && dut->read_fcsr(0) == 0,
        "relaunch retained register state");
}

// 通过真实访存、整数乘法和 FP32 转换验证增加warp数量后的全部驻留线程。
void check_large_residency() {
    DutConfig dut_config;
    dut_config.m_memory_bytes = 65536;
    dut_config.m_shader.m_max_warps = 16;
    dut_config.m_shader.m_max_threads = 512;
    dut_config.m_shader.m_regfile_bytes = 522240;
    auto dut = make_dut(dut_config);
    require(dut->resident_warp_capacity() == 16 && dut->resident_thread_capacity() == 512,
        "configured warp count not exposed");
    // r1 保存线程数据地址，r255 验证完整8位逻辑编号。
    const auto to_float = fp(16, 254, 255);
    const auto to_integer = fp(14, 255, 254);
    dut->load_program({imm(0x04, 255, 4, 1, 0),
        reg(0x03, 0, 255, 255, 255),
        to_float,
        to_integer,
        store(1, 255, 2, 4),
        EXIT});
    dut->launch(512);
    for (unsigned thread = 0; thread < 512; ++thread) {
        const auto address = 4096 + thread * 8;
        dut->write_register(thread, 1, address);
        dut->write_memory(address, 4, thread + 1);
    }
    const auto result = dut->run();
    require(result.completed(), "large resident compute program failed");
    for (unsigned thread = 0; thread < 512; ++thread) {
        const auto expected = (thread + 1) * (thread + 1);
        require(dut->read_memory(4096 + thread * 8 + 4, 4) == expected,
            "resident integer/FP/load/store result mismatch");
        require(dut->read_register(thread, 255) == expected && dut->read_fcsr(thread) == 0,
            "resident register/FP state mismatch");
    }
}
}

// 检查零线程、满容量与尾 warp 不活跃线程不会执行。
void wmu_test() {
    auto dut = make_dut();
    dut->load_program({imm(0x02, 1, 0, 1, 1), EXIT});
    dut->launch(0);
    const auto empty = dut->run();
    require(empty.completed() && empty.m_steps == 0, "empty task did not immediately complete");
    dut->launch(256);
    require(dut->run().completed(), "full capacity task failed");
    for (unsigned thread = 0; thread < 256; ++thread) {
        require(dut->read_register(thread, 1) == 1, "full capacity thread missing");
    }
    dut->launch(33);
    require(dut->run().completed(), "partial warp task failed");
    require(dut->read_register(32, 1) == 1 && dut->read_register(33, 1) == 0 &&
                dut->read_register(63, 1) == 0,
        "inactive tail threads executed");
    check_rejected_launch();
    check_large_residency();
}
}
