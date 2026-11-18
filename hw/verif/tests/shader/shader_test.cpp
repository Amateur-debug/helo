#include "shader_test.h"
#include <iostream>
#include <map>

namespace helo::verif {
namespace {
// 构造测试机器码程序，并通过标签回填分支与跳转偏移。
class Program {
public:
    // 将一条独立编码的指令追加到程序。
    void emit(std::uint64_t instruction) {
        m_words.push_back(instruction);
    }

    // 在当前地址定义标签并拒绝重复标签。
    void label(const std::string& name) {
        require(m_labels.emplace(name, m_words.size()).second, "duplicate program label");
    }

    // 记录条件分支，待所有标签定义后计算相对地址。
    void branch_to(unsigned rs1, unsigned rs2, unsigned condition, const std::string& target) {
        m_fixups.push_back({m_words.size(), target, rs1, rs2, condition, false});
        emit(0);
    }

    // 记录带链接或不带链接的跳转，避免测试维护裸字节偏移。
    void jump_to(unsigned rd, const std::string& target) {
        m_fixups.push_back({m_words.size(), target, rd, 0, 0, true});
        emit(0);
    }

    // 回填全部分支和跳转并检查编码范围。
    std::vector<std::uint64_t> words() const {
        auto words = m_words;
        for (const auto& fixup : m_fixups) {
            const int offset =
                (static_cast<int>(m_labels.at(fixup.m_target)) - static_cast<int>(fixup.m_index)) *
                8;
            require(offset >= -4096 && offset <= 4094, "test branch offset out of range");
            words[fixup.m_index] =
                fixup.m_jump ? jump(fixup.m_rs1, offset)
                             : branch(fixup.m_rs1, fixup.m_rs2, fixup.m_condition, offset);
        }
        return words;
    }

private:
    // 保存一条待回填分支或跳转的指令位置、目标与编码参数。
    struct Fixup {
        std::size_t m_index; // 指定待回填指令在程序中的索引。
        std::string m_target; // 保存跳转或分支目标的标签名。
        unsigned m_rs1; // 保存分支的第一源寄存器编号，跳转时复用为链接目的编号。
        unsigned m_rs2; // 保存分支的第二源寄存器编号，跳转时不使用。
        unsigned m_condition; // 保存条件分支的功能码，跳转时不使用。
        bool m_jump; // 标记该记录是否为 JAL 跳转而非条件分支。
    };

    std::vector<std::uint64_t> m_words; // 按指令顺序保存机器码，待回填位置先保存占位值。
    std::map<std::string, std::size_t> m_labels; // 将标签名映射到指令索引，计算字节偏移时乘以8。
    std::vector<Fixup> m_fixups; // 保存需要在导出程序时回填的分支与跳转记录。
};

constexpr unsigned INPUT_BASE = 4096;
constexpr unsigned OUTPUT_BASE = 8192;
constexpr unsigned THREADS = 65;
constexpr std::uint32_t SENTINEL = 0xdeadbeefU;

// 构造跨 warp 的整数、浮点、发散循环及结果访存程序。
std::vector<std::uint64_t> mixed_program() {
    Program program;
    program.emit(imm(0x04, 3, 4, 1, 0)); // LD32 r3, [r1+0]：n。
    program.emit(imm(0x02, 4, 0, 0, 3));
    program.emit(reg(0x03, 0, 3, 3, 4)); // MUL n*3。
    program.emit(reg(0x03, 4, 3, 3, 4)); // DIV 恢复 n。
    program.emit(imm(0x02, 4, 0, 0, 4));
    program.emit(reg(0x03, 6, 5, 3, 4)); // REM 得到循环次数的低两位。
    program.emit(imm(0x02, 5, 0, 5, 1));
    program.label("loop");
    program.emit(reg(0x01, 0, 6, 6, 5)); // 累加 k+(k-1)+...+1。
    program.emit(imm(0x02, 5, 0, 5, -1));
    program.branch_to(5, 0, 1, "loop");
    program.emit(imm(0x0c, 0, 7, 14, 1)); // CSRRW frm：每线程不同舍入模式。
    program.emit(fp(16, 7, 3, 0, 0)); // FCVT_F32_I32：整数转浮点。
    program.emit(imm(0x02, 8, 0, 0, 2));
    program.emit(fp(16, 8, 8, 0, 0));
    program.emit(imm(0x02, 9, 0, 0, 1));
    program.emit(fp(16, 9, 9, 0, 0));
    program.emit(fused(0, 7, 7, 8, 9)); // FMADD：2*n+1。
    program.emit(fp(0, 7, 7, 9, 7)); // FADD 动态舍入：2*n+2。
    program.emit(imm(0x02, 10, 0, 0, 70));
    program.emit(fp(16, 10, 10, 0, 0));
    program.emit(fp(11, 11, 7, 10, 0)); // FLT：n<34。
    program.emit(fp(14, 7, 7, 0, 0)); // 同一寄存器转回整数。
    program.branch_to(11, 0, 0, "high");
    program.emit(imm(0x02, 7, 0, 7, 100));
    program.jump_to(0, "join");
    program.label("high");
    program.emit(fp(3, 0, 9, 0, 0)); // FDIV 写 r0，仍设置 DZ。
    program.emit(imm(0x02, 7, 0, 7, 200));
    program.label("join");
    program.emit(reg(0x01, 0, 7, 7, 6));
    program.emit(imm(0x02, 0, 0, 7, 1)); // 整数写 r0 也必须丢弃。
    program.emit(store(2, 7, 2));
    program.emit(imm(0x0c, 12, 8, 0, 2)); // CSRRS 读取 fcsr。
    program.emit(store(2, 12, 2, 4));
    program.emit(EXIT);
    return program.words();
}

// 初始化输入及尾 warp 未启用线程的访存哨兵。
void prepare_mixed(Dut& dut, const std::vector<std::uint64_t>& words) {
    dut.load_program(words);
    dut.launch(THREADS);
    for (unsigned thread = 0; thread < 96; ++thread) {
        dut.write_register(thread, 1, INPUT_BASE + thread * 4);
        dut.write_register(thread, 2, OUTPUT_BASE + thread * 8);
        dut.write_register(thread, 14, thread % 5);
        dut.write_memory(INPUT_BASE + thread * 4, 4, thread + 1);
        dut.write_memory(OUTPUT_BASE + thread * 8, 4, SENTINEL);
        dut.write_memory(OUTPUT_BASE + thread * 8 + 4, 4, SENTINEL);
    }
}

// 用纯整数公式校验端到端结果、浮点状态和未激活线程哨兵。
void verify_mixed(const Dut& dut) {
    for (unsigned thread = 0; thread < 96; ++thread) {
        const unsigned n = thread + 1;
        const unsigned k = n % 4 + 1;
        const auto expected = 2 * n + 2 + k * (k + 1) / 2 + (n < 34 ? 100 : 200);
        const auto flags = ((thread % 5) << 5) | (n < 34 ? 0U : 8U);
        require(dut.read_memory(INPUT_BASE + thread * 4, 4) == n, "mixed program modified input");
        require(dut.read_memory(OUTPUT_BASE + thread * 8, 4) ==
                    (thread < THREADS ? expected : SENTINEL),
            "mixed result or inactive store mismatch");
        require(dut.read_memory(OUTPUT_BASE + thread * 8 + 4, 4) ==
                    (thread < THREADS ? flags : SENTINEL),
            "mixed fcsr store mismatch");
        require(dut.read_register(thread, 0) == 0, "mixed program changed r0");
        require(dut.read_register(thread, 7) == (thread < THREADS ? expected : 0),
            "mixed register result mismatch");
        require(dut.read_fcsr(thread) == (thread < THREADS ? flags : 0),
            "mixed per-thread fcsr mismatch");
        if (thread >= THREADS) {
            require(dut.read_register(thread, 3) == 0, "inactive tail thread performed load");
        }
    }
}

// 比较单次运行与多次小预算续跑的完整寄存器和内存结果。
void check_mixed_program() {
    const auto words = mixed_program();
    auto whole = make_dut();
    auto sliced = make_dut();
    prepare_mixed(*whole, words);
    prepare_mixed(*sliced, words);
    const auto result = whole->run();
    require(result.completed() && result.m_fault.m_message.empty(), "mixed whole program failed");
    std::size_t total_steps = 0;
    bool completed = false;
    for (unsigned slice = 0; slice < 10000 && !completed; ++slice) {
        const auto part = sliced->run(slice % 7 + 1);
        require(part.m_steps <= slice % 7 + 1, "mixed program exceeded slice budget");
        total_steps += part.m_steps;
        completed = part.completed();
        if (completed) {
            require(part.m_fault.m_message.empty(), "completed sliced program retained error");
        } else {
            require(part.m_steps > 0 && part.m_status == RunStatus::BUDGET_EXHAUSTED &&
                        part.m_fault.m_message.empty(),
                "sliced program trapped instead of exhausting budget");
        }
    }
    require(completed && total_steps == result.m_steps,
        "sliced program failed or changed instruction-group count");
    verify_mixed(*whole);
    verify_mixed(*sliced);
    for (unsigned thread = 0; thread < 96; ++thread) {
        for (unsigned index = 0; index < 256; ++index) {
            require(whole->read_register(thread, index) == sliced->read_register(thread, index),
                "sliced register state differs");
        }
    }
    require(!whole->step() && !sliced->step(), "completed mixed program executed again");
    std::cout << "mixed_program threads=65 warps=3 words=" << words.size()
              << " steps=" << result.m_steps << " sliced_steps=" << total_steps << '\n';
}

// 验证子程序返回、存储后加载和整数浮点共用寄存器，并重新启动同一对象。
void check_call_program() {
    Program program;
    program.emit(imm(0x04, 3, 4, 1, 0));
    program.jump_to(31, "function");
    program.emit(store(2, 3, 2));
    program.emit(imm(0x04, 4, 4, 2, 0));
    program.emit(imm(0x02, 4, 0, 4, 7));
    program.emit(store(2, 4, 2, 4));
    program.emit(EXIT);
    program.label("function");
    program.emit(imm(0x02, 3, 0, 3, 1));
    program.emit(fp(16, 3, 3, 0, 0));
    program.emit(fp(0, 3, 3, 3, 0));
    program.emit(fp(14, 3, 3, 0, 0));
    program.emit(imm(0x0a, 0, 0, 31, 0));
    const auto words = program.words();
    auto dut = make_dut();
    dut->load_program(words, 512);
    for (unsigned pass = 0; pass < 2; ++pass) {
        dut->launch(35, 512);
        for (unsigned thread = 0; thread < 35; ++thread) {
            for (unsigned index = 0; index < 256; ++index) {
                require(dut->read_register(thread, index) == 0, "relaunch retained register state");
            }
            require(dut->read_fcsr(thread) == 0, "relaunch retained fcsr");
            dut->write_register(thread, 1, INPUT_BASE + thread * 4);
            dut->write_register(thread, 2, OUTPUT_BASE + thread * 8);
            dut->write_memory(INPUT_BASE + thread * 4, 4, thread + pass * 100);
            dut->write_fcsr(thread, 0x20);
        }
        unsigned steps = 0;
        while (steps < 1000 && dut->step()) {
            ++steps;
        }
        require(steps < 1000 && !dut->step(), "call program failed to terminate");
        for (unsigned thread = 0; thread < 35; ++thread) {
            const auto expected = 2 * (thread + pass * 100 + 1);
            require(
                dut->read_memory(OUTPUT_BASE + thread * 8, 4) == expected, "call result mismatch");
            require(dut->read_memory(OUTPUT_BASE + thread * 8 + 4, 4) == expected + 7,
                "store/load dependency mismatch");
            require(dut->read_register(thread, 31) == 528, "JAL return address mismatch");
            require(dut->read_register(thread, 0) == 0 && dut->read_fcsr(thread) == 0x20,
                "call corrupted r0 or fcsr");
        }
        std::cout << "call_program threads=35 words=" << words.size() << " pass=" << pass
                  << " steps=" << steps << '\n';
    }
}

// 验证成功指令之后的访存异常不会部分提交，并检查锁存、复位与重新执行。
void check_fault_program() {
    const std::vector<std::uint64_t> words = {imm(0x02, 3, 0, 0, 9),
        fp(16, 3, 3, 0, 0),
        fp(14, 3, 3, 0, 0),
        store(1, 3, 2),
        imm(0x02, 3, 0, 3, 1),
        store(2, 3, 2),
        imm(0x02, 4, 0, 0, 77),
        store(1, 4, 2, 4),
        EXIT};
    auto dut = make_dut();
    dut->load_program(words);
    for (unsigned pass = 0; pass < 2; ++pass) {
        if (pass) {
            dut->reset();
        }
        dut->launch(32);
        for (unsigned thread = 0; thread < 32; ++thread) {
            dut->write_register(thread, 1, INPUT_BASE + thread * 8);
            dut->write_register(thread, 2, OUTPUT_BASE + thread * 4);
            dut->write_memory(INPUT_BASE + thread * 8 + 4, 4, SENTINEL);
            dut->write_memory(OUTPUT_BASE + thread * 4, 4, SENTINEL);
            if (pass) {
                require(dut->read_memory(INPUT_BASE + thread * 8, 4) == 9,
                    "reset cleared prior memory");
            }
        }
        if (!pass) {
            dut->write_register(31, 2, 65536);
        }
        const auto result = dut->run();
        require(result.completed() == (pass != 0), "fault/recovery completion mismatch");
        if (!pass) {
            require(!result.m_fault.m_message.empty() && result.m_steps == 5,
                "fault failed to preserve successful prefix");
            const auto again = dut->run();
            require(!again.completed() && again.m_steps == 0 &&
                        again.m_fault.m_message == result.m_fault.m_message,
                "fault was not latched");
            bool threw = false;
            try {
                dut->step();
            } catch (const std::exception&) {
                threw = true;
            }
            require(threw, "step ignored latched fault");
        }
        for (unsigned thread = 0; thread < 32; ++thread) {
            require(dut->read_memory(INPUT_BASE + thread * 8, 4) == 9, "fault lost earlier store");
            require(dut->read_register(thread, 3) == 10, "fault lost earlier register result");
            require(dut->read_memory(OUTPUT_BASE + thread * 4, 4) == (pass ? 10 : SENTINEL),
                "faulting store partially committed");
            require(dut->read_register(thread, 4) == (pass ? 77U : 0U),
                "fault executed later instruction");
            require(dut->read_memory(INPUT_BASE + thread * 8 + 4, 4) == (pass ? 77U : SENTINEL),
                "fault executed later store");
        }
        if (pass) {
            require(!dut->step(), "recovered program did not terminate");
        }
        std::cout << "fault_program threads=32 words=" << words.size() << " recovery=" << pass
                  << " steps=" << result.m_steps << '\n';
    }
}
}

// 验证通路配置改变实际执行批次数，并保持跨warp和尾warp的运算结果。
void check_execution_counts() {
    for (const auto config : {ExecutionConfig{1, 1, 1, 1},
             ExecutionConfig{3, 3, 3, 3},
             ExecutionConfig{8, 8, 8, 8},
             ExecutionConfig{16, 16, 16, 16},
             ExecutionConfig{32, 32, 32, 32},
             ExecutionConfig{7, 3, 5, 2}}) {
        DutConfig dut_config;
        dut_config.m_memory_bytes = 65536;
        dut_config.m_shader.m_max_warps = 8;
        dut_config.m_shader.m_max_threads = 256;
        dut_config.m_shader.m_regfile_bytes = 261120;
        dut_config.m_shader.m_execution = config;
        auto dut = make_dut(dut_config);
        dut->load_program({imm(2, 1, 0, 0, 2),
            fp(16, 2, 1),
            fp(0, 3, 2, 2),
            fp(14, 4, 3),
            store(5, 4, 2),
            imm(4, 6, 4, 5, 0),
            BARW,
            EXIT});
        dut->launch(65, 0, 0, 8);
        for (unsigned thread = 0; thread < 65; ++thread) {
            dut->write_register(thread, 5, 4096 + thread * 4);
        }
        const auto result = dut->run();
        require(
            result.completed() && result.m_steps == 24, "execution width changed group progress");
        for (unsigned thread = 0; thread < 65; ++thread) {
            require(dut->read_register(thread, 6) == 4 &&
                        dut->read_memory(4096 + thread * 4, 4) == 4 && dut->read_fcsr(thread) == 0,
                "execution batching changed result");
        }
        const auto stats = dut->execution_stats();
        // 两个完整warp与一个单lane尾warp分别消耗对应批次。
        const auto expected = [](unsigned count) { return 2 * ((32 + count - 1) / count) + 1; };
        require(stats.m_iu_batches == expected(config.m_iu_count) &&
                    stats.m_fpu_batches == 3 * expected(config.m_fpu_count) &&
                    stats.m_lsu_batches == 2 * expected(config.m_lsu_count) &&
                    stats.m_wcu_batches == 2 * expected(config.m_wcu_count),
            "execution batch counts mismatch");
        dut->reset();
        const auto cleared = dut->execution_stats();
        require(cleared.m_iu_batches == 0 && cleared.m_fpu_batches == 0 &&
                    cleared.m_lsu_batches == 0 && cleared.m_wcu_batches == 0,
            "reset retained batch statistics");
    }
    for (unsigned field = 0; field < 4; ++field) {
        for (const auto count : {0U, 33U}) {
            ExecutionConfig config;
            switch (field) {
            case 0:
                config.m_iu_count = count;
                break;
            case 1:
                config.m_fpu_count = count;
                break;
            case 2:
                config.m_lsu_count = count;
                break;
            case 3:
                config.m_wcu_count = count;
                break;
            }
            bool rejected = false;
            try {
                DutConfig invalid_config;
                invalid_config.m_memory_bytes = 65536;
                invalid_config.m_shader.m_max_warps = 8;
                invalid_config.m_shader.m_max_threads = 256;
                invalid_config.m_shader.m_regfile_bytes = 261120;
                invalid_config.m_shader.m_execution = config;
                auto invalid = make_dut(invalid_config);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "invalid execution count accepted");
        }
    }
}

// 检查后续批次的访存、舍入和跳转错误不会提交前序批次的副作用。
void check_batch_faults() {
    DutConfig dut_config;
    dut_config.m_memory_bytes = 65536;
    dut_config.m_shader.m_max_warps = 8;
    dut_config.m_shader.m_max_threads = 256;
    dut_config.m_shader.m_regfile_bytes = 261120;
    dut_config.m_shader.m_execution.m_iu_count = 4;
    dut_config.m_shader.m_execution.m_fpu_count = 4;
    dut_config.m_shader.m_execution.m_lsu_count = 4;
    dut_config.m_shader.m_execution.m_wcu_count = 4;
    auto dut = make_dut(dut_config);
    for (unsigned kind = 0; kind < 3; ++kind) {
        const auto instruction = kind == 0   ? store(1, 2, 2)
                                 : kind == 1 ? fp(0, 3, 1, 2, 7)
                                             : imm(10, 3, 0, 1, 0);
        dut->load_program({instruction, EXIT});
        dut->launch(32, 0, 0, 4);
        for (unsigned thread = 0; thread < 32; ++thread) {
            dut->write_register(thread,
                1,
                kind == 0   ? 4096 + thread * 4
                : kind == 1 ? 0x3f800000U
                            : 8);
            dut->write_register(thread, 2, 0x3f800000U);
            dut->write_register(thread, 3, 99);
            dut->write_memory(4096 + thread * 4, 4, 77);
            dut->write_fcsr(thread, 1);
        }
        if (kind == 0) {
            dut->write_register(31, 1, 65536);
        }
        if (kind == 1) {
            dut->write_fcsr(31, 5 << 5);
        }
        if (kind == 2) {
            dut->write_register(31, 1, 12);
        }
        const auto result = dut->run();
        require(!result.m_fault.m_message.empty() && result.m_steps == 0,
            "late batch fault was committed");
        for (unsigned thread = 0; thread < 32; ++thread) {
            require(
                dut->read_register(thread, 3) == 99, "late batch fault wrote earlier GPR results");
            require(
                dut->read_memory(4096 + thread * 4, 4) == 77, "late batch fault partially stored");
            require(dut->read_fcsr(thread) == (kind == 1 && thread == 31 ? 5U << 5 : 1U),
                "late batch fault changed flags");
        }
        const auto stats = dut->execution_stats();
        require(stats.m_lsu_batches == 0 && stats.m_fpu_batches == 0 && stats.m_wcu_batches == 0,
            "failed instruction counted as committed batches");
    }
}

// 明确区分可续跑预算暂停和锁存故障，并验证诊断字段不依赖字符串解析。
static void check_run_status() {
    auto dut = make_dut();
    dut->load_program({NOP, EXIT});
    dut->launch(3);
    const auto zero = dut->run(0);
    require(zero.m_status == RunStatus::BUDGET_EXHAUSTED && zero.m_steps == 0 &&
                zero.m_fault.m_code == FaultCode::NONE && zero.m_fault.m_message.empty(),
        "zero shader budget must pause without fault");
    const auto paused = dut->run(1);
    require(paused.m_status == RunStatus::BUDGET_EXHAUSTED && paused.m_steps == 1 &&
                paused.m_fault.m_message.empty(),
        "shader pause reported as fault");
    require(dut->run().m_status == RunStatus::COMPLETED, "paused shader did not resume");
    require(dut->run(0).completed(), "completed shader with zero budget regressed");
    dut->load_program({0}, 64);
    dut->launch(3, 64);
    const auto fault = dut->run();
    require(fault.m_status == RunStatus::FAULT &&
                fault.m_fault.m_code == FaultCode::SHADER_EXECUTION && fault.m_fault.m_pc == 64 &&
                fault.m_fault.m_warp == 0 && fault.m_fault.m_mask == 7 &&
                fault.m_fault.m_instruction == 0,
        "shader fault lost structured context");
    const auto repeated = dut->run(0);
    require(repeated.m_status == RunStatus::FAULT && repeated.m_fault.m_pc == fault.m_fault.m_pc &&
                repeated.m_fault.m_message == fault.m_fault.m_message,
        "zero budget bypassed latched shader fault");
    dut->reset();
    dut->launch(0);
    require(dut->run(0).completed(), "reset retained structured fault");
}

// 验证跨 warp 的任务分配、部分 warp、依赖链、预算与复位。
void shader_test() {
    check_run_status();
    check_execution_counts();
    check_batch_faults();
    check_mixed_program();
    check_call_program();
    check_fault_program();
    auto dut = make_dut();
    dut->load_program({imm(0x02, 2, 0, 1, 7), imm(0x02, 3, 0, 2, -3), EXIT});
    dut->launch(65);
    for (unsigned thread = 0; thread < 65; ++thread) {
        dut->write_register(thread, 1, thread);
    }
    const auto paused = dut->run(1);
    require(!paused.completed(), "one step unexpectedly completed multiple warps");
    require(dut->run().completed(), "multiwarp execution failed");
    for (unsigned thread = 0; thread < 65; ++thread) {
        require(dut->read_register(thread, 3) == thread + 4, "multiwarp input/result mismatch");
    }
    dut->reset();
    dut->launch(1);
    require(dut->read_register(0, 3) == 0, "launch did not clear registers");
    require(dut->run().completed(), "reset and relaunch failed");
    bool rejected = false;
    try {
        dut->launch(257);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "over-capacity task accepted");
    dut->load_program({jump(0, 0)});
    dut->launch(1);
    require(!dut->run(20).completed(), "infinite loop bypassed run budget");
}
}
