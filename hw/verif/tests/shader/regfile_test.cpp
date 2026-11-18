#include "regfile_test.h"
#include "shader_test.h"

namespace helo::verif {
namespace {
// 检查任务寄存器声明独立限制编号范围，失败指令不得部分提交。
void check_register_bounds() {
    auto dut = make_dut();
    const std::vector<std::uint64_t> instructions = {imm(2, 8, 0, 0, 7),
        reg(1, 0, 2, 8, 1),
        reg(1, 0, 2, 1, 8),
        fused(0, 2, 1, 3, 8),
        store(1, 8, 2),
        imm(12, 2, 7, 8, 2)};
    for (const auto instruction : instructions) {
        dut->load_program({instruction, EXIT});
        dut->launch(32, 0, 0, 8);
        for (unsigned thread = 0; thread < 32; ++thread) {
            dut->write_register(thread, 1, 4096);
            dut->write_register(thread, 2, 99);
            dut->write_fcsr(thread, 0x21);
        }
        dut->write_memory(4096, 4, 0x12345678);
        require(!dut->run().m_fault.m_message.empty(), "undeclared logical register accepted");
        for (unsigned thread = 0; thread < 32; ++thread) {
            require(dut->read_register(thread, 2) == 99 && dut->read_fcsr(thread) == 0x21,
                "register bounds fault partially committed");
        }
        require(dut->read_memory(4096, 4) == 0x12345678, "register bounds fault changed memory");
    }
    dut->load_program({imm(12, 1, 10, 31, 0), EXIT});
    dut->launch(1, 0, 0, 2);
    require(
        dut->run().completed() && dut->read_fcsr(0) == 31, "CSR zimm mistaken for undeclared GPR");
    bool rejected = false;
    try {
        dut->read_register(0, 2);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "host read accepted undeclared register");
    rejected = false;
    try {
        dut->write_register(0, 2, 1);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "host write accepted undeclared register");
}

// 检查三个独立硬件容量及任务寄存器用量共同决定驻留上限。
void check_resources() {
    DutConfig dut_config;
    dut_config.m_memory_bytes = 65536;
    dut_config.m_shader.m_max_warps = 8;
    dut_config.m_shader.m_max_threads = 256;
    dut_config.m_shader.m_regfile_bytes = 8192;
    auto dut = make_dut(dut_config);
    dut->load_program({imm(2, 1, 0, 1, 1), imm(2, 1, 0, 1, 2), EXIT});
    dut->launch(64, 0, 0, 32);
    require(dut->resident_warp_capacity() == 2 && dut->resident_thread_capacity() == 64,
        "GPR budget not limiting residency");
    require(dut->regfile_capacity_bytes() == 8192, "physical GPR budget changed with task");
    dut->write_register(0, 1, 10);
    require(dut->step(), "resource test task did not start");
    for (const auto count : {0U, 257U}) {
        bool rejected = false;
        try {
            dut->launch(1, 0, 0, count);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && dut->read_register(0, 1) == 11,
            "invalid register declaration destroyed active task");
    }
    bool rejected = false;
    try {
        dut->launch(65, 0, 0, 32);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && dut->read_register(0, 1) == 11, "over-budget launch destroyed active task");
    require(
        dut->run().completed() && dut->read_register(0, 1) == 13, "failed launch changed old PC");
    dut->launch(0, 0, 0, 16);
    require(dut->resident_warp_capacity() == 4 && dut->resident_thread_capacity() == 128,
        "smaller task register count did not improve residency");
    dut->launch(128, 0, 0, 16);
    require(dut->run().completed(), "small-register full-capacity task failed");
    dut->launch(0, 0, 0, 256);
    require(dut->resident_warp_capacity() == 0, "insufficient full-register budget not reported");
    rejected = false;
    try {
        dut->launch(1, 0, 0, 256);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "one thread bypassed whole-warp register allocation");
    DutConfig thread_limited_config;
    thread_limited_config.m_memory_bytes = 65536;
    thread_limited_config.m_shader.m_max_warps = 8;
    thread_limited_config.m_shader.m_max_threads = 95;
    thread_limited_config.m_shader.m_regfile_bytes = 261120;
    auto thread_limited = make_dut(thread_limited_config);
    thread_limited->load_program({EXIT});
    thread_limited->launch(64);
    require(thread_limited->resident_thread_capacity() == 64,
        "thread slots did not round down by warp");
    rejected = false;
    try {
        thread_limited->launch(65);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "hardware thread limit ignored");
    DutConfig warp_limited_config;
    warp_limited_config.m_memory_bytes = 65536;
    warp_limited_config.m_shader.m_max_warps = 2;
    warp_limited_config.m_shader.m_max_threads = 256;
    warp_limited_config.m_shader.m_regfile_bytes = 261120;
    auto warp_limited = make_dut(warp_limited_config);
    warp_limited->load_program({EXIT});
    warp_limited->launch(64, 0, 0, 2);
    require(warp_limited->resident_thread_capacity() == 64, "max_warps limit ignored");
    DutConfig zero_config;
    zero_config.m_memory_bytes = 65536;
    zero_config.m_shader.m_max_warps = 2;
    zero_config.m_shader.m_max_threads = 64;
    zero_config.m_shader.m_regfile_bytes = 0;
    auto zero = make_dut(zero_config);
    zero->load_program({NOP, BARW, EXIT});
    zero->launch(33, 0, 0, 1);
    require(zero->run().completed() && zero->read_register(32, 0) == 0,
        "r0-only task consumed GPR budget");
    require(zero->resident_warp_capacity() == 2 && zero->regfile_capacity_bytes() == 0,
        "r0-only capacity mismatch");
    for (const auto bytes : {124U, 128U}) {
        DutConfig exact_config;
        exact_config.m_memory_bytes = 65536;
        exact_config.m_shader.m_max_warps = 1;
        exact_config.m_shader.m_max_threads = 32;
        exact_config.m_shader.m_regfile_bytes = bytes;
        auto exact = make_dut(exact_config);
        exact->load_program({EXIT});
        rejected = false;
        try {
            exact->launch(1, 0, 0, 2);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected == (bytes == 124), "one-register warp byte boundary mismatch");
    }
    for (const auto threads : {0U, 31U}) {
        rejected = false;
        try {
            DutConfig invalid_config;
            invalid_config.m_memory_bytes = 65536;
            invalid_config.m_shader.m_max_warps = 8;
            invalid_config.m_shader.m_max_threads = threads;
            invalid_config.m_shader.m_regfile_bytes = 8192;
            auto invalid = make_dut(invalid_config);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "sub-warp hardware thread configuration accepted");
    }
    rejected = false;
    try {
        DutConfig invalid_config;
        invalid_config.m_memory_bytes = 65536;
        invalid_config.m_shader.m_max_warps = 0;
        auto invalid = make_dut(invalid_config);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "zero hardware warp slots accepted");
    rejected = false;
    try {
        DutConfig invalid_config;
        invalid_config.m_memory_bytes = 65536;
        invalid_config.m_shader.m_max_warps = 8;
        invalid_config.m_shader.m_max_threads = 256;
        invalid_config.m_shader.m_regfile_bytes = 3;
        auto invalid = make_dut(invalid_config);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "unaligned register byte budget accepted");
}
}

// 检查零寄存器、完整8位编号、线程隔离和动态任务资源。
void regfile_test() {
    auto dut = make_dut();
    dut->load_program({imm(2, 0, 0, 0, 123), EXIT});
    dut->launch(33);
    dut->write_register(0, 0, 42);
    require(dut->read_register(0, 0) == 0, "r0 accepted host write");
    for (unsigned reg_index = 1; reg_index < 256; ++reg_index) {
        dut->write_register(0, reg_index, reg_index + 100);
        dut->write_register(32, reg_index, reg_index + 200);
    }
    for (unsigned reg_index = 1; reg_index < 256; ++reg_index) {
        require(dut->read_register(0, reg_index) == reg_index + 100 &&
                    dut->read_register(32, reg_index) == reg_index + 200 &&
                    dut->read_register(1, reg_index) == 0,
            "logical registers alias across threads");
    }
    bool rejected = false;
    try {
        dut->write_register(0, 256, 1);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "invalid 8-bit register index accepted");
    require(dut->run().completed() && dut->read_register(0, 0) == 0, "instruction wrote r0");
    dut->launch(1, 0, 0, 32);
    dut->write_register(0, 31, 77);
    dut->launch(1, 0, 0, 256);
    require(dut->read_register(0, 31) == 0 && dut->read_register(0, 255) == 0,
        "register layout relaunch retained state");
    // 相同布局再次launch仍须清零GPR/FCSR；显式reset还须保留外部内存。
    dut->launch(33, 0, 0, 32);
    dut->write_register(32, 31, 0x1234);
    dut->write_fcsr(32, 0xff);
    dut->write_memory(4096, 4, 0xabcdef01U);
    dut->launch(33, 0, 0, 32);
    require(dut->read_register(32, 31) == 0 && dut->read_fcsr(32) == 0,
        "same-layout launch retained register state");
    dut->write_register(0, 1, 77);
    dut->write_fcsr(0, 31);
    dut->reset();
    require(dut->read_register(0, 1) == 0 && dut->read_fcsr(0) == 0,
        "reset failed to clear architectural state");
    require(dut->read_memory(4096, 4) == 0xabcdef01U, "reset cleared resource memory");
    check_register_bounds();
    check_resources();
}
}
