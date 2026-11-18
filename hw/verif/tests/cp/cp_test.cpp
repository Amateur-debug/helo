#include "cp_test.h"
#include "tests/gpu/gpu_test.h"
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
            const unsigned first_reserved = kind == 0 ? 17 : kind == 1 ? 55 : 9;
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

// 检查命令预算、快照语义、错误隔离、复位与真实计算调度。
void cp_test() {
    check_random_packets();
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
