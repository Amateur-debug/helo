#include "fpu_test.h"
#include "shader_test.h"

namespace helo::verif {
namespace {
// 通过共用整数寄存器与控制寄存器读取验证浮点运算的结果和异常标志。
void check_fp(std::uint64_t instruction,
    std::uint32_t a,
    std::uint32_t b,
    std::uint32_t c,
    std::uint32_t expected,
    std::uint32_t flags = 0,
    unsigned frm = 0) {
    auto dut = make_dut();
    dut->load_program({instruction, imm(0x0c, 12, 8, 0, 0), EXIT});
    dut->launch(35);
    for (unsigned thread = 0; thread < 35; ++thread) {
        dut->write_register(thread, 1, a);
        dut->write_register(thread, 2, b);
        dut->write_register(thread, 3, c);
        dut->write_fcsr(thread, frm << 5);
    }
    const auto result = dut->run();
    if (!result.completed()) {
        throw std::runtime_error("FP program failed: " + result.m_fault.m_message);
    }
    for (unsigned thread = 0; thread < 35; ++thread) {
        const auto actual = dut->read_register(thread, 4);
        if (actual != expected || dut->read_register(thread, 12) != flags) {
            throw std::runtime_error("FP mismatch instruction=" + std::to_string(instruction) +
                                     " actual=" + std::to_string(actual) +
                                     " expected=" + std::to_string(expected) +
                                     " flags=" + std::to_string(dut->read_register(thread, 12)));
        }
    }
}

// 检查启动状态 CSR、只读权限、零源抑制以及完整8位寄存器源。
void check_csr_state() {
    auto dut = make_dut();
    std::vector<std::uint64_t> program;
    for (unsigned state = 0x10; state <= 0x16; ++state) {
        program.push_back(imm(12, 200 + state - 0x10, 8, 0, state));
    }
    program.push_back(EXIT);
    dut->load_program(program);
    dut->launch(35, 0, 0xabcdef00U);
    for (unsigned thread = 0; thread < 35; ++thread) {
        for (unsigned index = 0; index < 256; ++index) {
            require(dut->read_register(thread, index) == 0, "launch synthesized GPR state");
        }
    }
    require(dut->run().completed(), "launch CSR program failed");
    for (unsigned thread = 0; thread < 35; ++thread) {
        const unsigned expected[] = {thread % 32, thread / 32, thread, 35, 32, 0xabcdef00U, 1};
        for (unsigned index = 0; index < 7; ++index) {
            require(dut->read_register(thread, 200 + index) == expected[index],
                "launch CSR value mismatch");
        }
    }
    for (unsigned funct : {8U, 9U, 11U, 12U}) {
        dut->load_program({imm(12, 255, funct, 0, 0x14), EXIT});
        dut->launch(1);
        require(dut->run().completed() && dut->read_register(0, 255) == 32,
            "zero-source readonly CSR access rejected");
    }
    const std::vector<std::uint64_t> invalid = {imm(12, 0, 7, 0, 0x12),
        imm(12, 0, 10, 0, 0x12),
        imm(12, 255, 8, 200, 0x12),
        imm(12, 255, 9, 200, 0x12),
        imm(12, 255, 11, 1, 0x12),
        imm(12, 255, 12, 1, 0x12),
        imm(12, 0, 8, 0, 0xfff)};
    for (const auto instruction : invalid) {
        dut->load_program({instruction, EXIT});
        dut->launch(32);
        for (unsigned thread = 0; thread < 32; ++thread) {
            dut->write_register(thread, 255, 77);
        }
        require(!dut->run().m_fault.m_message.empty(), "readonly or missing CSR accepted");
        for (unsigned thread = 0; thread < 32; ++thread) {
            require(
                dut->read_register(thread, 255) == 77, "CSR permission fault partially committed");
        }
    }
    dut->load_program({imm(12, 200, 7, 200, 2), imm(12, 201, 8, 0, 2), EXIT});
    dut->launch(1);
    dut->write_register(0, 200, 0x63);
    dut->write_fcsr(0, 0x21);
    require(dut->run().completed() && dut->read_register(0, 200) == 0x21 &&
                dut->read_register(0, 201) == 0x63,
        "CSR overlapping high register source lost old value");
    dut->load_program({fused(0, 255, 200, 201, 202), reg(1, 10, 254, 255), EXIT});
    dut->launch(1);
    dut->write_register(0, 200, 0x40000000);
    dut->write_register(0, 201, 0x40400000);
    dut->write_register(0, 202, 0x3f800000);
    require(dut->run().completed() && dut->read_register(0, 254) == 0x40e00000,
        "high rs3 or MOV register truncated");
}

// 验证整数与浮点共用寄存器的双向依赖、第三源依赖及线程掩码。
void check_dependencies() {
    auto dut = make_dut();
    dut->launch(35);
    const auto producer = fp(0, 3, 1, 2, 0);
    const auto consumer = fused(0, 4, 1, 2, 3);
    dut->reserve(producer, 0, 0x80000001U);
    require(!dut->dependency_ready(consumer, 0, 1), "HELO FP32 rs3 RAW dependency missed");
    require(
        !dut->dependency_ready(imm(0x02, 5, 0, 3, 1), 0, 1), "FP-to-integer RAW dependency missed");
    require(
        !dut->dependency_ready(imm(0x02, 3, 0, 0, 1), 0, 1), "FP-to-integer WAW dependency missed");
    require(!dut->dependency_ready(producer, 0, 1), "FP WAW dependency missed");
    require(dut->dependency_ready(consumer, 0, 2) && dut->dependency_ready(consumer, 1, 1),
        "FP dependency leaked across threads or warps");
    dut->enqueue(consumer, 0, 1, 100);
    require(!dut->issue(0), "FMA issued with busy rs3");
    std::array<std::uint32_t, 32> values{};
    values.fill(0x3f800000U);
    dut->writeback(producer, 0, 1, values);
    require(dut->read_register(0, 3) == 0x3f800000U && dut->read_register(31, 3) == 0,
        "FP masked writeback failed");
    require(
        dut->dependency_ready(consumer, 0, 1) && !dut->dependency_ready(consumer, 0, 0x80000000U),
        "FP partial release failed");
    require(dut->issue(0), "FMA remained blocked after rs3 completion");
    dut->reset();
    dut->launch(1);
    dut->reserve(imm(0x02, 1, 0, 0, 1), 0, 1);
    require(
        !dut->dependency_ready(fp(0, 4, 1, 2, 0), 0, 1), "integer-to-FP rs1 RAW dependency missed");
    require(
        !dut->dependency_ready(fp(0, 4, 2, 1, 0), 0, 1), "integer-to-FP rs2 RAW dependency missed");
    require(!dut->dependency_ready(fused(0, 4, 2, 3, 1), 0, 1),
        "integer-to-FP rs3 RAW dependency missed");
    require(!dut->dependency_ready(fp(0, 1, 2, 3, 0), 0, 1), "integer-to-FP WAW dependency missed");
    dut->reset();
    dut->launch(1);
    dut->reserve(fp(5, 0, 1, 2, 0), 0, 1);
    require(
        dut->dependency_ready(imm(0x02, 4, 0, 0, 1), 0, 1), "FP writer incorrectly reserved r0");
    require(!dut->dependency_ready(fp(5, 4, 0, 0, 0), 0, 1),
        "FP writer lost conservative fcsr reservation");
    dut->writeback(fp(5, 0, 1, 2, 0), 0, 1, values);
    require(dut->read_register(0, 0) == 0, "FP writeback changed r0");
    require(dut->dependency_ready(fp(5, 4, 0, 0, 0), 0, 1), "FP writeback did not release fcsr");
    dut->reserve(imm(0x02, 0, 0, 1, 1), 0, 1);
    require(dut->dependency_ready(fp(5, 4, 0, 0, 0), 0, 1),
        "integer writer incorrectly blocked FP r0 sources");
    dut->reset();
    dut->launch(1);
    dut->reserve(imm(0x0c, 10, 7, 2, 1), 0, 1);
    require(
        !dut->dependency_ready(fp(0, 4, 1, 2, 7), 0, 1), "dynamic rounding missed frm dependency");
    dut->reset();
    dut->launch(35);
    const auto flag_writer = imm(0x0c, 0, 7, 2, 0);
    dut->reserve(flag_writer, 0, 1);
    require(!dut->dependency_ready(imm(0x0c, 3, 8, 0, 0), 0, 1), "fflags RAW dependency missed");
    require(!dut->dependency_ready(flag_writer, 0, 1), "fflags WAW dependency missed");
    require(
        !dut->dependency_ready(fp(0, 4, 1, 2, 0), 0, 1), "FP flags update missed CSR dependency");
    require(dut->dependency_ready(flag_writer, 0, 2) && dut->dependency_ready(flag_writer, 1, 1),
        "CSR hazard leaked between threads or warps");
}

// 验证同一寄存器在整数与浮点指令间传递，并覆盖零寄存器及原地写回。
void check_shared_registers() {
    auto dut = make_dut();
    dut->load_program({imm(0x02, 1, 0, 0, 3),
        fp(16, 1, 1, 0, 0),
        fp(0, 1, 1, 1, 0),
        fp(14, 1, 1, 0, 0),
        imm(0x02, 1, 0, 1, 1),
        fp(16, 2, 1, 0, 0),
        fused(0, 2, 2, 2, 2),
        fp(14, 3, 2, 0, 0),
        EXIT});
    dut->launch(35);
    require(dut->run().completed(), "mixed integer/FP program failed");
    for (unsigned thread = 0; thread < 35; ++thread) {
        require(dut->read_register(thread, 1) == 7 &&
                    dut->read_register(thread, 2) == 0x42600000U &&
                    dut->read_register(thread, 3) == 56,
            "shared register or overlapping FMA operands failed");
    }
    dut->load_program({fp(13, 4, 0, 0, 0),
        fp(0, 5, 0, 1, 0),
        fused(0, 6, 1, 1, 0),
        fp(3, 0, 1, 0, 0),
        imm(0x0c, 12, 8, 0, 0),
        EXIT});
    dut->launch(35);
    for (unsigned thread = 0; thread < 35; ++thread) {
        dut->write_register(thread, 0, 0xffffffffU);
        dut->write_register(thread, 1, 0x3f800000U);
    }
    require(dut->run().completed(), "HELO FP32 r0 program failed");
    for (unsigned thread = 0; thread < 35; ++thread) {
        require(dut->read_register(thread, 0) == 0 && dut->read_register(thread, 4) == 16 &&
                    dut->read_register(thread, 5) == 0x3f800000U &&
                    dut->read_register(thread, 6) == 0x3f800000U &&
                    dut->read_register(thread, 12) == 8,
            "r0 read, discarded FP result or accrued flags failed");
    }
}

// 验证浮点访存保留原始位模式，并在任一活跃线程错误时禁止部分提交。
void check_memory() {
    auto dut = make_dut();
    dut->load_program({imm(0x04, 3, 4, 1, -4), store(1, 3, 2, 0), EXIT});
    dut->launch(35);
    for (unsigned thread = 0; thread < 35; ++thread) {
        dut->write_register(thread, 1, 1028 + thread * 8);
        dut->write_memory(1024 + thread * 8, 4, 0x7f800001U + thread);
    }
    require(dut->run().completed(), "LW/SW program failed");
    for (unsigned thread = 0; thread < 35; ++thread) {
        require(dut->read_memory(1028 + thread * 8, 4) == 0x7f800001U + thread &&
                    dut->read_register(thread, 3) == 0x7f800001U + thread &&
                    dut->read_fcsr(thread) == 0,
            "FP load/store changed NaN payload or flags");
    }
    for (const auto address : {1025U, 65536U}) {
        dut->load_program({imm(0x04, 3, 4, 1, 0), EXIT});
        dut->launch(32);
        for (unsigned thread = 0; thread < 32; ++thread) {
            dut->write_register(thread, 1, 2048 + thread * 4);
            dut->write_register(thread, 3, 0xdeadbeefU);
        }
        dut->write_register(31, 1, address);
        require(!dut->run().m_fault.m_message.empty(), "faulting LW accepted");
        for (unsigned thread = 0; thread < 32; ++thread) {
            require(
                dut->read_register(thread, 3) == 0xdeadbeefU, "faulting LW partially committed");
        }
        dut->load_program({store(1, 3, 2), EXIT});
        dut->launch(32);
        for (unsigned thread = 0; thread < 32; ++thread) {
            dut->write_register(thread, 1, 2048 + thread * 4);
            dut->write_register(thread, 3, 99);
            dut->write_memory(2048 + thread * 4, 4, 7);
        }
        dut->write_register(31, 1, address);
        require(!dut->run().m_fault.m_message.empty(), "faulting SW accepted");
        for (unsigned thread = 0; thread < 31; ++thread) {
            require(dut->read_memory(2048 + thread * 4, 4) == 7, "faulting SW partially committed");
        }
    }
}
}

// 覆盖 HELO FP32 的全部计算类别、舍入、异常、特殊值及流水线边界。
void fpu_test() {
    check_fp(fp(0, 4, 1, 2, 0), 0x3fc00000, 0x40100000, 0, 0x40700000);
    check_fp(fp(1, 4, 1, 2, 0), 0x3fc00000, 0x40100000, 0, 0xbf400000);
    check_fp(fp(2, 4, 1, 2, 0), 0x3fc00000, 0x40000000, 0, 0x40400000);
    check_fp(fp(3, 4, 1, 2, 0), 0x40c00000, 0x40000000, 0, 0x40400000);
    check_fp(fp(4, 4, 1, 0, 0), 0x40800000, 0, 0, 0x40000000);
    for (const auto opcode : {0U, 1U, 2U, 3U}) {
        const std::uint32_t expected = opcode == 0   ? 0x40e00000
                                       : opcode == 1 ? 0x40a00000
                                       : opcode == 2 ? 0xc0a00000
                                                     : 0xc0e00000;
        check_fp(fused(opcode, 4, 1, 2, 3), 0x40000000, 0x40400000, 0x3f800000, expected);
    }
    check_fp(fused(0, 4, 1, 2, 3), 0x3f800001, 0x3f7ffffe, 0xbf800000, 0xa8800000);
    for (unsigned mode = 0; mode < 3; ++mode) {
        check_fp(fp(5 + mode, 4, 1, 2, 0),
            0x3fc00000,
            0xc0000000,
            0,
            mode == 1 ? 0x3fc00000 : 0xbfc00000);
    }
    check_fp(fp(8, 4, 1, 2, 0), 0, 0x80000000, 0, 0x80000000);
    check_fp(fp(9, 4, 1, 2, 0), 0, 0x80000000, 0, 0);
    check_fp(fp(8, 4, 1, 2, 0), 0x7fc12345, 0x3f800000, 0, 0x3f800000);
    check_fp(fp(8, 4, 1, 2, 0), 0x7f800001, 0x3f800000, 0, 0x3f800000, 16);
    check_fp(fp(9, 4, 1, 2, 0), 0x7fc12345, 0x7fcabcde, 0, 0x7fc00000);
    check_fp(fp(5, 4, 1, 2, 0), 0x7f800001, 0x80000000, 0, 0xff800001);
    check_fp(fp(10, 4, 1, 2, 0), 0, 0x80000000, 0, 1, 0);
    check_fp(fp(11, 4, 1, 2, 0), 0xbf800000, 0x3f800000, 0, 1, 0);
    check_fp(fp(12, 4, 1, 2, 0), 0x3f800000, 0x3f800000, 0, 1, 0);
    check_fp(fp(10, 4, 1, 2, 0), 0x7fc00000, 0, 0, 0, 0);
    check_fp(fp(11, 4, 1, 2, 0), 0x7fc00000, 0, 0, 0, 16);
    check_fp(fp(10, 4, 1, 2, 0), 0x7f800001, 0, 0, 0, 16);
    const std::uint32_t classes[] = {0xff800000,
        0xbf800000,
        0x80000001,
        0x80000000,
        0,
        1,
        0x3f800000,
        0x7f800000,
        0x7f800001,
        0x7fc00000};
    for (unsigned index = 0; index < 10; ++index) {
        check_fp(fp(13, 4, 1, 0, 0), classes[index], 0, 0, 1U << index, 0);
    }
    for (unsigned mode = 0; mode < 5; ++mode) {
        const auto expected = mode == 3 || mode == 4 ? 0x3f800001U : 0x3f800000U;
        check_fp(fp(0, 4, 1, 2, mode), 0x3f800000, 0x33800000, 0, expected, 1);
        check_fp(fp(0, 4, 1, 2, 7), 0x3f800000, 0x33800000, 0, expected, 1, mode);
        check_fp(fp(14, 4, 1, 0, mode), 0x40200000, 0, 0, mode == 3 || mode == 4 ? 3 : 2, 1);
        check_fp(fp(14, 4, 1, 0, mode),
            0xc0200000,
            0,
            0,
            mode == 2 || mode == 4 ? 0xfffffffdU : 0xfffffffeU,
            1);
    }
    check_fp(fp(16, 4, 1, 0, 0), 0xffffffff, 0, 0, 0xbf800000);
    check_fp(fp(17, 4, 1, 0, 0), 0xffffffff, 0, 0, 0x4f800000, 1);
    check_fp(fp(15, 4, 1, 0, 0), 0x40700000, 0, 0, 4, 1);
    check_fp(fp(14, 4, 1, 0, 0), 0x7fc00000, 0, 0, 0x7fffffff, 16);
    check_fp(fp(15, 4, 1, 0, 0), 0xbf800000, 0, 0, 0, 16);
    check_fp(fp(15, 4, 1, 0, 0), 0x7f800000, 0, 0, 0xffffffff, 16);
    check_fp(fp(3, 4, 1, 2, 0), 0x3f800000, 0, 0, 0x7f800000, 8);
    check_fp(fp(3, 4, 1, 2, 0), 0, 0, 0, 0x7fc00000, 16);
    check_fp(fp(4, 4, 1, 0, 0), 0xbf800000, 0, 0, 0x7fc00000, 16);
    check_fp(fp(4, 4, 1, 0, 0), 0x80000000, 0, 0, 0x80000000);
    check_fp(fp(0, 4, 1, 2, 0), 0x7f800001, 0, 0, 0x7fc00000, 16);
    check_fp(fp(0, 4, 1, 2, 0), 0x7fc12345, 0, 0, 0x7fc00000);
    check_fp(fp(2, 4, 1, 2, 0), 0x7f7fffff, 0x40000000, 0, 0x7f800000, 5);
    check_fp(fp(2, 4, 1, 2, 0), 1, 0x3f000000, 0, 0, 3);
    check_fp(fp(0, 4, 1, 2, 0), 1, 1, 0, 2);
    // 无界指数舍入后仍 tiny 的值，即使最终结果为 normal，也必须报告 UF。
    check_fp(fp(2, 4, 1, 2, 0), 0x00800000, 0x3f7fffff, 0, 0x00800000, 3);
    // 此乘积只比最小 normal 小 2^-172，无界指数舍入已成为 normal。
    check_fp(fp(2, 4, 1, 2, 0), 0x00800001, 0x3f7ffffe, 0, 0x00800000, 1);
    check_fp(fused(0, 4, 1, 2, 3), 0, 0x7f800000, 0x7fc00000, 0x7fc00000, 16);
    auto dut = make_dut();
    dut->load_program({imm(0x0c, 4, 7, 1, 2),
        imm(0x0c, 5, 11, 1, 0),
        imm(0x0c, 6, 12, 1, 0),
        imm(0x0c, 7, 8, 0, 2),
        imm(0x0c, 8, 10, 3, 1),
        imm(0x0c, 9, 9, 2, 2),
        EXIT});
    dut->launch(1);
    dut->write_register(0, 1, 0x42);
    dut->write_register(0, 2, 2);
    require(dut->run().completed(), "FP CSR program failed");
    require(dut->read_register(0, 4) == 0 && dut->read_register(0, 5) == 2 &&
                dut->read_register(0, 6) == 3 && dut->read_register(0, 7) == 0x42 &&
                dut->read_register(0, 8) == 2 && dut->read_register(0, 9) == 0x62 &&
                dut->read_fcsr(0) == 0x60,
        "FP CSR read-modify-write semantics failed");
    dut->load_program({imm(0x0c, 0, 7, 1, 2), imm(0x0c, 4, 8, 0, 2), EXIT});
    dut->launch(1);
    dut->write_register(0, 1, 0xffffff42);
    require(dut->run().completed() && dut->read_register(0, 4) == 0x42,
        "fcsr retained reserved upper bits");
    dut->load_program({fp(0, 4, 1, 2, 7), EXIT});
    dut->launch(35);
    for (unsigned thread = 0; thread < 35; ++thread) {
        dut->write_register(thread, 1, 0x3f800000);
        dut->write_register(thread, 2, 0x33800000);
        dut->write_fcsr(thread, (thread % 5) << 5);
    }
    require(dut->run().completed(), "per-thread dynamic rounding program failed");
    for (unsigned thread = 0; thread < 35; ++thread) {
        require(dut->read_register(thread, 4) == (thread % 5 >= 3 ? 0x3f800001U : 0x3f800000U) &&
                    dut->read_fcsr(thread) == (((thread % 5) << 5) | 1),
            "rounding mode leaked between threads");
    }
    dut->load_program({fp(3, 4, 1, 2, 0), fp(2, 5, 3, 3, 0), EXIT});
    dut->launch(1);
    dut->write_register(0, 1, 0x3f800000);
    dut->write_register(0, 2, 0);
    dut->write_register(0, 3, 0x7f7fffff);
    dut->write_fcsr(0, 16);
    require(
        dut->run().completed() && dut->read_fcsr(0) == 29, "floating exceptions did not accrue");
    dut->reset();
    dut->launch(1);
    require(
        dut->read_fcsr(0) == 0 && dut->read_register(0, 4) == 0, "reset retained floating state");
    dut->load_program({branch(10, 0, 0, 16), EXIT, fp(3, 4, 1, 2, 0), EXIT});
    dut->launch(35);
    for (unsigned thread = 0; thread < 35; ++thread) {
        dut->write_register(thread, 10, thread % 2);
        dut->write_register(thread, 1, 0x3f800000);
        dut->write_register(thread, 2, 0);
        dut->write_register(thread, 4, 0x12345678);
    }
    require(dut->run().completed(), "divergent FP program failed");
    for (unsigned thread = 0; thread < 35; ++thread) {
        require(dut->read_register(thread, 4) == (thread % 2 ? 0x12345678U : 0x7f800000U) &&
                    dut->read_fcsr(thread) == (thread % 2 ? 0U : 8U),
            "inactive thread FP state changed");
    }
    for (unsigned mode : {5U, 6U}) {
        dut->load_program({fp(0, 4, 1, 2, 7), EXIT});
        dut->launch(32);
        for (unsigned thread = 0; thread < 32; ++thread) {
            dut->write_register(thread, 4, 99);
        }
        dut->write_fcsr(31, mode << 5);
        require(!dut->run().m_fault.m_message.empty(), "reserved dynamic rounding accepted");
        for (unsigned thread = 0; thread < 32; ++thread) {
            require(dut->read_register(thread, 4) == 99 &&
                        dut->read_fcsr(thread) == (thread == 31 ? mode << 5 : 0),
                "faulting FP operation partially committed");
        }
    }
    check_dependencies();
    check_memory();
    check_shared_registers();
    check_csr_state();
}
}
