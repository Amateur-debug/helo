#include "Vhelo_shader_core.h"
#include "mem/mem.h"
#include "shader/shader.h"
#include "verilated.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
// 在同一个仿真对象上推进一个完整时钟。
void tick(Vhelo_shader_core& top) {
    top.clk = 0;
    top.eval();
    top.clk = 1;
    top.eval();
    top.clk = 0;
    top.eval();
}

// 通过共享主机端口写入指令和测试数据。
void host_write(Vhelo_shader_core& top, unsigned address, unsigned value) {
    top.host_valid = 1;
    top.host_write = 1;
    top.host_addr = address;
    top.host_size = 4;
    top.host_wdata = value;
    top.eval();
    if (top.host_error) {
        throw std::runtime_error("host upload rejected");
    }
    tick(top);
    top.host_valid = 0;
    top.eval();
}

// 对每个64位字按小端顺序上传，与参考模型共用的只有内存数据。
void load_program(Vhelo_shader_core& top,
    helo::func::Mem& memory,
    unsigned entry,
    const std::vector<std::uint64_t>& words) {
    memory.load_instructions(entry, words);
    for (unsigned i = 0; i < words.size(); ++i) {
        const auto address = entry + i * 8;
        host_write(top, address, static_cast<unsigned>(words[i]));
        host_write(top, address + 4, static_cast<unsigned>(words[i] >> 32));
    }
}

// 生成I、S、B、J格式，不依赖功能模型译码器。
std::uint64_t iword(unsigned family, unsigned funct, unsigned rd, unsigned rs1, int imm) {
    return (std::uint64_t(static_cast<unsigned>(imm) & 4095) << 32) | (std::uint64_t(funct) << 24) |
           (std::uint64_t(rs1) << 16) | (std::uint64_t(rd) << 8) | family;
}

// 启动程序，拒绝非法入口时保证不会破坏旧运行状态。
void launch(Vhelo_shader_core& top, unsigned entry, unsigned registers) {
    top.launch_valid = 1;
    top.launch_entry = entry;
    top.launch_registers = registers;
    top.eval();
    if (top.launch_error) {
        throw std::runtime_error("valid task launch rejected");
    }
    tick(top);
    top.launch_valid = 0;
    top.eval();
}

// 在单线程程序每条提交后比较真实Shader模型的PC与已完成状态。
void run_program(Vhelo_shader_core& top, helo::func::Shader& reference, unsigned expected_steps) {
    unsigned steps = 0;
    for (unsigned cycle = 0; cycle < 2000 && !top.completed && !top.fault; ++cycle) {
        tick(top);
        if (top.retired) {
            if (!reference.step() || top.pc != reference.warps().at(0).m_pc[0]) {
                throw std::runtime_error("retired instruction PC differs from functional Shader");
            }
            ++steps;
        }
    }
    if (!top.completed || top.fault || steps != expected_steps || reference.step()) {
        throw std::runtime_error("integer/LSU program did not complete correctly");
    }
}

// 结束后逐一核对GPR、r0与共享内存的可见数据。
void compare_state(Vhelo_shader_core& top,
    const helo::func::Shader& reference,
    const helo::func::Mem& memory,
    unsigned count) {
    for (unsigned reg = 0; reg < count; ++reg) {
        top.debug_read_valid = 1;
        top.debug_reg = reg;
        top.eval();
        if (top.debug_error || top.debug_rdata != reference.registers().read_gpr(0, 0, reg)) {
            throw std::runtime_error("committed GPR differs from functional Shader");
        }
    }
    top.debug_read_valid = 0;
    for (unsigned address = 0; address < 512; address += 4) {
        top.host_valid = 1;
        top.host_write = 0;
        top.host_addr = address;
        top.host_size = 4;
        top.eval();
        if (top.host_error || top.host_rdata != memory.read(address, 4)) {
            throw std::runtime_error("committed memory differs from functional Shader");
        }
    }
    top.host_valid = 0;
}

// 等待故障并检查已完成指令数量，故障状态在时钟推进后保持锁存。
void expect_fault(Vhelo_shader_core& top, unsigned expected_retired) {
    unsigned retired = 0;
    for (unsigned cycle = 0; cycle < 50 && !top.fault; ++cycle) {
        tick(top);
        retired += unsigned(top.retired);
    }
    if (!top.fault || top.completed || retired != expected_retired) {
        throw std::runtime_error("fault status or instruction commit count mismatch");
    }
    const auto pc = top.pc;
    tick(top);
    if (!top.fault || top.pc != pc || top.retired) {
        throw std::runtime_error("fault was not latched");
    }
}

// 验证机器码执行、分支、取指、访存、故障锁存以及重启清零。
void run_test() {
    VerilatedContext context;
    Vhelo_shader_core top(&context);
    helo::func::Mem memory(4096);
    helo::func::Shader reference(memory);
    top.capacity_bytes = 4096;
    top.host_valid = 0;
    top.launch_valid = 0;
    top.debug_read_valid = 0;
    top.debug_write_valid = 0;
    top.reset = 1;
    top.launch_valid = 1;
    top.launch_entry = 0;
    top.launch_registers = 16;
    top.eval();
    if (!top.launch_error) {
        throw std::runtime_error("launch during reset was silently accepted");
    }
    tick(top);
    top.reset = 0;
    top.launch_valid = 0;

    const std::vector<std::uint64_t> program{
        iword(2, 0, 1, 0, 256), // ADDI r1, r0, 256
        iword(2, 0, 2, 0, -1), // ADDI r2, r0, -1
        iword(5, 0, 2, 1, 0), // SB r2, 0(r1)
        iword(4, 0, 3, 1, 0), // LB r3, 0(r1)
        iword(2, 0, 4, 3, 1), // ADDI r4, r3, 1
        iword(8, 0, 4, 0, 8), // BEQ r4, r0, +16
        iword(12, 2, 0, 0, 1), // TRAP, unreachable
        iword(4, 3, 5, 1, 0), // LBU r5, 0(r1)
        iword(5, 2, 5, 1, 4), // SW r5, 4(r1)
        iword(4, 2, 6, 1, 4), // LW r6, 4(r1)
        (std::uint64_t(8) << 32) | (std::uint64_t(7) << 8) | 9, // JAL r7, +16
        iword(12, 2, 0, 0, 2), // TRAP, unreachable
        iword(12, 0, 0, 0, 0), // NOP
        iword(12, 1, 0, 0, 0) // EXIT
    };
    load_program(top, memory, 0, program);
    top.launch_valid = 1;
    top.launch_entry = 2;
    top.launch_registers = 16;
    top.eval();
    if (!top.launch_error || top.running) {
        throw std::runtime_error("unaligned entry accepted");
    }
    top.launch_valid = 0;
    launch(top, 0, 16);
    reference.launch(0, 1, 0, 16);
    run_program(top, reference, 12);
    compare_state(top, reference, memory, 16);
    if (memory.read(256, 4) != 0xff || memory.read(260, 4) != 0xff) {
        throw std::runtime_error("program did not write expected shared memory");
    }
    const auto completed_pc = top.pc;
    top.launch_valid = 1;
    top.launch_registers = 0;
    top.eval();
    if (!top.launch_error) {
        throw std::runtime_error("invalid register budget accepted");
    }
    tick(top);
    top.launch_valid = 0;
    if (!top.completed || top.pc != completed_pc) {
        throw std::runtime_error("rejected launch damaged completed task");
    }
    compare_state(top, reference, memory, 16);

    const std::vector<std::uint64_t> failure{
        iword(2, 0, 1, 0, 259), iword(5, 2, 5, 1, 0), iword(12, 1, 0, 0, 0)};
    load_program(top, memory, 512, failure);
    launch(top, 512, 16);
    reference.launch(512, 1, 0, 16);
    expect_fault(top, 1);
    if (!reference.step() || reference.run().completed() || memory.read(256, 4) != 0xff) {
        throw std::runtime_error("faulting store was not isolated");
    }
    compare_state(top, reference, memory, 16);
    launch(top, 0, 16);
    reference.launch(0, 1, 0, 16);
    run_program(top, reference, 12);
    compare_state(top, reference, memory, 16);

    load_program(top, memory, 640, {iword(2, 0, 16, 0, 7), iword(12, 1, 0, 0, 0)});
    launch(top, 640, 16);
    expect_fault(top, 0);
    top.debug_read_valid = 1;
    top.debug_reg = 0;
    top.eval();
    if (top.debug_error || top.debug_rdata != 0) {
        throw std::runtime_error("register range fault corrupted r0");
    }
    top.debug_read_valid = 0;

    load_program(top, memory, 704, {iword(5, 2, 5, 16, 0), iword(12, 1, 0, 0, 0)});
    launch(top, 704, 16);
    expect_fault(top, 0);
    top.host_valid = 1;
    top.host_write = 0;
    top.host_addr = 256;
    top.host_size = 4;
    top.eval();
    if (top.host_error || top.host_rdata != memory.read(256, 4)) {
        throw std::runtime_error("invalid source register caused partial store");
    }
    top.host_valid = 0;

    load_program(top,
        memory,
        768,
        {(std::uint64_t(2) << 24) | (std::uint64_t(1) << 16) | (std::uint64_t(3) << 8) | 6,
            iword(12, 1, 0, 0, 0)});
    launch(top, 768, 16);
    expect_fault(top, 0);

    load_program(top, memory, 4088, {(std::uint64_t(8) << 32) | 9});
    launch(top, 4088, 16);
    expect_fault(top, 1);
    if (top.pc != 4104) {
        throw std::runtime_error("out-of-range instruction fetch lost fault PC");
    }
}
}

// 返回整程序差分的真实执行结果。
int main() {
    try {
        run_test();
        std::cout << "single-thread shader core compare: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
