#include "cp_test.h"
#include "tests/gpu/gpu_test.h"
#include <algorithm>
#include <iostream>
#include <random>

namespace helo::verif {
// 随机破坏三类包的保留字，验证失败地址、成功前缀与后续隔离。
static void check_random_packets() {
    auto dut = make_gpu_dut();
    helo::ClearCommand red;
    red.m_framebuffer = test_framebuffer();
    red.m_color = {1, 0, 0, 1};
    auto green = red;
    green.m_color = {0, 1, 0, 1};
    std::mt19937 random(0x43504255U);
    for (unsigned trial = 0; trial < 96; ++trial) {
        try {
            dut->reset();
            const unsigned kind = random() % 3;
            const Command command = kind == 0   ? Command(green)
                                    : kind == 1 ? Command(test_draw(red.m_framebuffer))
                                                : Command(helo::ComputeCommand{});
            const unsigned first_reserved = kind == 0 ? 19 : kind == 1 ? 63 : 11;
            const auto address = dut->submit_commands({red, command, green});
            const auto word = first_reserved + random() % (64 - first_reserved);
            dut->write(address + 256 + word * 4, 4, random() | 1U);
            const auto result = dut->run();
            require(result.m_status == RunStatus::FAULT &&
                        result.m_fault.m_code == FaultCode::COMMAND &&
                        result.m_fault.m_command_address == address + 256 && result.m_commands == 1,
                "random packet lost failure boundary");
            require(dut->read(red.m_framebuffer.m_color_address, 4) == 0xff0000ffU,
                "random malformed packet changed framebuffer");

        } catch (const std::exception& error) {
            throw std::runtime_error(
                "seed=0x43504255 trial=" + std::to_string(trial) + ": " + error.what());
        }
    }
    dut->reset();
    helo::ComputeCommand compute;
    compute.m_entry = 0x2000;
    compute.m_thread_count = 3;
    dut->upload_program(compute.m_entry, {0});
    const auto address = dut->submit_commands({compute});
    const auto fault = dut->run();
    require(
        fault.m_status == RunStatus::FAULT && fault.m_fault.m_code == FaultCode::SHADER_EXECUTION &&
            fault.m_fault.m_command_address == address && fault.m_fault.m_pc == compute.m_entry &&
            fault.m_fault.m_mask == 7 && fault.m_fault.m_instruction == 0,
        "CP dropped nested shader fault context");
}

// 用非整warp大小的组和尾组验证编号、共享寻址、屏障和原子加的旧值。
static void check_workgroups() {
    GpuConfig config;
    config.m_shader.m_max_warps = 2;
    config.m_shader.m_execution.m_lsu_count = 3;
    auto dut = make_gpu_dut(config);
    constexpr unsigned GROUP = 37;
    constexpr unsigned THREADS = 83;
    constexpr std::uint32_t OUTPUT = 0x5000;
    ComputeCommand command;
    command.m_entry = 0x2000;
    command.m_param_base = OUTPUT;
    command.m_thread_count = THREADS;
    command.m_group_size = GROUP;
    command.m_shared_bytes = 4 + GROUP * 4;
    const auto barrier = reg(0x0e, 4, 0, 0);
    dut->upload_program(command.m_entry,
        {imm(0x0c, 1, 8, 0, 0x12), // 全局线程编号。
            imm(0x0c, 2, 8, 0, 0x18), // 组内线程编号。
            imm(0x0c, 3, 8, 0, 0x17), // 工作组编号。
            imm(0x0c, 4, 8, 0, 0x19), // 本组实际线程数。
            imm(0x0c, 5, 8, 0, 0x13), // 整包线程总数。
            imm(0x0c, 6, 8, 0, 0x11), // 全局warp编号。
            imm(0x0c, 7, 8, 0, 0x15),
            imm(0x02, 8, 0, 0, 1),
            imm(0x02, 9, 4, 2, 2),
            imm(0x02, 9, 0, 9, 4),
            reg(0x0e, 1, 0, 9, 1), // 将全局线程编号写入本组独有的槽位。
            imm(0x02, 10, 0, 0, 32),
            branch(2, 10, 4, 32), // 末warp延迟到达原子加与组屏障。
            NOP,
            NOP,
            NOP,
            reg(0x0e, 2, 11, 0, 8),
            barrier,
            reg(0x0e, 0, 12, 0), // 全组贡献必须均已可见。
            reg(0x01, 1, 10, 4, 2),
            imm(0x02, 10, 4, 10, 2),
            reg(0x0e, 0, 13, 10), // 读取本组倒序配对线程的数据。
            barrier,
            reg(0x0e, 3, 14, 7, 8), // 全包线程争用同一全局计数器。
            imm(0x02, 10, 4, 1, 5),
            reg(0x01, 0, 10, 7, 10),
            store(10, 2, 2, 4),
            store(10, 3, 2, 8),
            store(10, 5, 2, 12),
            store(10, 6, 2, 16),
            store(10, 11, 2, 20),
            store(10, 12, 2, 24),
            store(10, 13, 2, 28),
            store(10, 14, 2, 32),
            EXIT});
    dut->submit(command);
    complete(*dut);
    require(dut->read(OUTPUT, 4) == THREADS, "global atomic add lost increments");
    std::vector<bool> global_tickets(THREADS);
    std::vector<bool> shared_tickets(THREADS);
    for (unsigned i = 0; i < THREADS; ++i) {
        const auto base = OUTPUT + 32 * i;
        const auto local = i % GROUP;
        const auto group = i / GROUP;
        const auto group_begin = group * GROUP;
        const auto size = std::min(GROUP, THREADS - group_begin);
        require(dut->read(base + 4, 4) == local && dut->read(base + 8, 4) == group &&
                    dut->read(base + 12, 4) == THREADS &&
                    dut->read(base + 16, 4) == group * 2 + local / 32,
            "compute ID CSRs incorrect");
        const auto shared_ticket = dut->read(base + 20, 4);
        const auto global_ticket = dut->read(base + 32, 4);
        require(shared_ticket < size && !shared_tickets[group_begin + shared_ticket] &&
                    global_ticket < THREADS && !global_tickets[global_ticket],
            "atomic increments did not return unique old values");
        shared_tickets[group_begin + shared_ticket] = true;
        global_tickets[global_ticket] = true;
        require(dut->read(base + 24, 4) == size &&
                    dut->read(base + 28, 4) == group_begin + size - 1 - local,
            "workgroup shared memory visibility or isolation incorrect");
    }
    // 每批仅容纳64线程，预算涵盖整个包；第二批必须保持全局编号及故障上下文。
    dut->reset();
    command.m_group_size = 0;
    command.m_shared_bytes = 0;
    command.m_step_budget = 4;
    dut->upload_program(command.m_entry, {NOP, EXIT});
    dut->submit(command);
    const auto budget = dut->run();
    require(budget.m_fault.m_code == FaultCode::SHADER_BUDGET &&
                dut->counters().m_shader_steps == 4 && !dut->counters().m_dispatches,
        "compute budget reset at a resident batch boundary");
    dut->reset();
    command.m_step_budget = 100;
    dut->upload_program(command.m_entry,
        {imm(0x0c, 1, 8, 0, 0x12),
            imm(0x02, 2, 0, 0, 64),
            branch(1, 2, 4, 16),
            imm(0x0c, 0, 2, 0, 1),
            EXIT});
    dut->submit(command);
    const auto late = dut->run();
    require(late.m_fault.m_code == FaultCode::SHADER_EXECUTION && late.m_fault.m_warp == 2 &&
                late.m_fault.m_mask == ((1u << (THREADS - 64)) - 1),
        "later batch used resident slots as architectural IDs");
}

// 检查形状拒绝、共享内存越界、屏障参与错误和原子指令失败前的全掩码验证。
static void check_workgroup_faults() {
    GpuConfig config;
    config.m_memory_bytes = 65536;
    config.m_shader.m_max_warps = 2;
    config.m_shader.m_execution.m_lsu_count = 1;
    auto dut = make_gpu_dut(config);
    ComputeCommand command;
    command.m_entry = 0x200;
    command.m_thread_count = 37;
    const auto check_fault = [&](const std::vector<std::uint64_t>& program, FaultCode code) {
        dut->reset();
        dut->upload_program(command.m_entry, program);
        dut->submit(command);
        const auto failure = dut->run();
        require(failure.m_status == RunStatus::FAULT && failure.m_fault.m_code == code &&
                    !dut->counters().m_dispatches,
            "invalid workgroup command or instruction succeeded");
        return failure;
    };
    command.m_group_size = 65;
    check_fault({EXIT}, FaultCode::COMMAND);
    command.m_group_size = 0;
    command.m_shared_bytes = 4;
    check_fault({EXIT}, FaultCode::COMMAND);
    command.m_group_size = 37;
    command.m_shared_bytes = 65540;
    check_fault({EXIT}, FaultCode::COMMAND);
    command.m_shared_bytes = 7;
    check_fault({EXIT}, FaultCode::COMMAND);
    command.m_shared_bytes = 4;
    check_fault({imm(0x02, 1, 0, 0, 4), reg(0x0e, 1, 0, 1, 1), EXIT}, FaultCode::SHADER_EXECUTION);
    const auto barrier = reg(0x0e, 4, 0, 0);
    check_fault({imm(0x0c, 1, 8, 0, 0x11), branch(1, 0, 1, 16), barrier, EXIT},
        FaultCode::SHADER_EXECUTION);
    const auto mismatch =
        check_fault({imm(0x0c, 1, 8, 0, 0x11), branch(1, 0, 1, 16), barrier, barrier, EXIT},
            FaultCode::SHADER_EXECUTION);
    require(mismatch.m_fault.m_warp == 1 && mismatch.m_fault.m_mask == 0x1f &&
                mismatch.m_fault.m_pc == command.m_entry + 24 &&
                mismatch.m_fault.m_instruction == barrier,
        "different BARG sites lost the offending tail warp context");
    // 同warp两条发散路径等待不同种类的屏障，两种抵达次序都必须立即报错。
    for (const auto first : {BARW, barrier}) {
        const auto other = first == BARW ? barrier : BARW;
        const auto mixed =
            check_fault({imm(0x0c, 1, 8, 0, 0x10), branch(1, 0, 0, 16), first, other, EXIT},
                FaultCode::SHADER_EXECUTION);
        require(mixed.m_fault.m_instruction == first || mixed.m_fault.m_instruction == other,
            "mixed barriers failed outside the offending instruction");
    }
    // 首warp先到BARG，尾warp仍可在自己的BARW释放后加入同一个BARG。
    dut->reset();
    dut->upload_program(
        command.m_entry, {imm(0x0c, 1, 8, 0, 0x11), branch(1, 0, 0, 16), BARW, barrier, EXIT});
    dut->submit(command);
    complete(*dut);
    command.m_group_size = 0;
    command.m_shared_bytes = 0;
    check_fault({barrier, EXIT}, FaultCode::SHADER_EXECUTION);
    command.m_thread_count = 32;
    command.m_param_base = 65536 - 31 * 4;
    for (unsigned i = 0; i < 31; ++i) {
        dut->write(command.m_param_base + i * 4, 4, 99);
    }
    check_fault({imm(0x0c, 1, 8, 0, 0x15),
                    imm(0x0c, 2, 8, 0, 0x10),
                    imm(0x02, 2, 4, 2, 2),
                    reg(0x01, 0, 1, 1, 2),
                    reg(0x0e, 3, 0, 1, 1),
                    EXIT},
        FaultCode::SHADER_EXECUTION);
    for (unsigned i = 0; i < 31; ++i) {
        require(dut->read(command.m_param_base + i * 4, 4) == 99,
            "failed global atomic instruction committed a lane");
    }
}

// 检查命令预算、快照语义、错误隔离、复位与真实计算调度。
void cp_test() {
    check_random_packets();
    check_workgroups();
    check_workgroup_faults();
    auto dut = make_gpu_dut();
    const auto framebuffer = test_framebuffer();
    helo::ClearCommand clear;
    clear.m_framebuffer = framebuffer;
    clear.m_color = {1, 0, 0, 1};
    dut->submit(clear);
    clear.m_color = {0, 1, 0, 1};
    dut->submit(clear);
    const auto zero = dut->run(0);
    require(zero.m_status == RunStatus::BUDGET_EXHAUSTED && zero.m_fault.m_message.empty() &&
                zero.m_commands == 0 && dut->pending() == 2,
        "zero command budget advanced queue");
    const auto first = dut->run(1);
    require(!first.completed() && first.m_commands == 1 && dut->pending() == 1,
        "command budget did not pause");
    require(dut->read(framebuffer.m_color_address, 4) == 0xff0000ffU,
        "submitted command did not preserve snapshot");
    complete(*dut);
    require(dut->read(framebuffer.m_color_address, 4) == 0xff00ff00U,
        "resumed command order incorrect");

    helo::ComputeCommand compute;
    compute.m_entry = 0x2000;
    compute.m_param_base = 0x5000;
    compute.m_thread_count = 65;
    dut->upload_program(compute.m_entry,
        {imm(0x0c, 1, 8, 0, 0x12),
            imm(0x0c, 2, 8, 0, 0x15),
            imm(0x02, 3, 4, 1, 2),
            reg(0x01, 0, 2, 2, 3),
            imm(0x02, 4, 0, 1, 17),
            store(2, 4, 2),
            EXIT});
    for (unsigned i = 0; i < 96; ++i) {
        dut->write(compute.m_param_base + i * 4, 4, 0xdeadbeefU);
    }
    dut->submit(compute);
    complete(*dut);
    for (unsigned i = 0; i < 96; ++i) {
        require(dut->read(compute.m_param_base + i * 4, 4) == (i < 65 ? i + 17 : 0xdeadbeefU),
            "compute dispatch or tail mask incorrect");
    }
    require(dut->counters().m_dispatches == 1, "compute counter incorrect");

    auto bad = test_draw(framebuffer);
    bad.m_vertex_address = 16 * 1024 * 1024 - 4;
    bad.m_vertex_count = 3;
    dut->submit(bad);
    clear.m_color = {0, 0, 1, 1};
    dut->submit(clear);
    const auto failed = dut->run();
    require(!failed.m_fault.m_message.empty() && !failed.completed() && failed.m_commands == 0 &&
                dut->pending() == 2,
        "command error not latched");
    const auto repeated = dut->run();
    require(repeated.m_fault.m_message == failed.m_fault.m_message && repeated.m_commands == 0,
        "latched command was retried");
    require(dut->read(framebuffer.m_color_address, 4) == 0xff00ff00U,
        "commands after failure modified framebuffer");
    dut->reset();
    require(dut->pending() == 0 && dut->counters().m_dispatches == 0,
        "reset did not clear control state");
    require(dut->read(compute.m_param_base, 4) == 17, "reset destroyed resources");
    dut->submit(clear);
    complete(*dut);
    require(dut->read(framebuffer.m_color_address, 4) == 0xffff0000U,
        "reset did not recover command processing");

    compute.m_step_budget = 1;
    dut->submit(compute);
    const auto exhausted = dut->run();
    require(exhausted.m_status == RunStatus::FAULT &&
                exhausted.m_fault.m_code == FaultCode::SHADER_BUDGET,
        "compute budget exhaustion was accepted");
    // 同一缓冲区内混合清除、计算和清除，并按包预算恢复游标。
    dut->reset();
    compute.m_step_budget = 100000;
    auto red = clear;
    red.m_color = {1, 0, 0, 1};
    auto green = clear;
    green.m_color = {0, 1, 0, 1};
    const auto buffer = dut->submit_commands({red, compute, green});
    require(dut->pending() == 1, "one command buffer was split into submissions");
    const auto one = dut->run(1);
    require(one.m_commands == 1 && !one.completed() && dut->pending() == 1,
        "CP did not preserve packet cursor");
    require(dut->read(framebuffer.m_color_address, 4) == 0xff0000ffU, "first packet not executed");
    // 修改已消费包，续跑不能重新解析或重放该包。
    dut->write(buffer, 4, 0);
    const auto rest = dut->run();
    require(rest.completed() && rest.m_commands == 2, "CP replayed consumed packet");
    require(
        dut->read(framebuffer.m_color_address, 4) == 0xff00ff00U, "mixed buffer ordering failed");
    require(dut->counters().m_dispatches == 1, "compute packet did not use Shader");

    // 当前包必须整体解析通过才产生任何模块副作用。
    const std::pair<unsigned, std::uint32_t> malformed[] = {
        {0, 0}, {1, 99}, {2, 252}, {3, 1}, {9, 2}, {16, 256}, {63, 1}};
    for (const auto& field : malformed) {
        dut->reset();
        const auto address = dut->submit_commands({red, green, clear});
        dut->write(address + 256 + field.first * 4, 4, field.second);
        const auto failed_packet = dut->run();
        require(!failed_packet.m_fault.m_message.empty() && failed_packet.m_commands == 1 &&
                    dut->pending() == 1,
            "malformed packet did not stop at correct offset");
        require(dut->read(framebuffer.m_color_address, 4) == 0xff0000ffU,
            "failed packet partially executed or later packet ran");
        require(dut->run().m_fault.m_message == failed_packet.m_fault.m_message,
            "parse error not latched");
    }
    // 提交只验证范围；未消费的命令负载由CP执行时从内存读取。
    dut->reset();
    const auto live = dut->submit_commands({red});
    dut->write(live + 11 * 4, 4, 0);
    dut->write(live + 12 * 4, 4, 0x3f800000U);
    complete(*dut);
    require(dut->read(framebuffer.m_color_address, 4) == 0xff00ff00U,
        "CP bypassed command memory parsing");
    const std::pair<std::uint32_t, std::uint32_t> bad_ranges[] = {
        {0, 0}, {0, 255}, {1, 256}, {0xffffff00U, 512}, {0xffff00U, 512}};
    for (const auto& range : bad_ranges) {
        bool rejected = false;
        try {
            dut->submit_buffer(range.first, range.second);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && dut->pending() == 0, "invalid buffer range was enqueued");
    }
    std::cout << "CP: command memory parsing, mixed packet buffers, malformed packets, zero/one "
                 "command budgets, 65-thread compute, error latch/reset, shader budget verified\n";
}
}
