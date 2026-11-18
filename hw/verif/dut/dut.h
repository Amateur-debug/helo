#pragma once
#include "helo/config.h"
#include "helo/execution.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace helo::verif {
// 描述Shader独立测试所需的主机内存与被测核心配置。
struct DutConfig {
    std::size_t m_memory_bytes = 65536; // 为测试程序与数据分配统一内存。
    ShaderConfig m_shader; // 使用与GPU相同的核心资源配置。
};

// 提供成功提交指令的执行批次统计。
struct ExecutionStats {
    std::uint64_t m_iu_batches; // 记录整数批次。
    std::uint64_t m_fpu_batches; // 记录浮点及CSR批次。
    std::uint64_t m_lsu_batches; // 记录访存批次。
    std::uint64_t m_wcu_batches; // 记录控制批次。
};

// 定义各被测实现共用的任务执行、状态访问和独立单元验证接口。
class Dut {
public:
    // 允许适配对象通过统一接口安全销毁。
    virtual ~Dut() = default;
    virtual void load_program(
        const std::vector<std::uint64_t>& words, std::uint32_t address = 0) = 0;
    virtual void launch(std::size_t threads,
        std::uint32_t entry = 0,
        std::uint32_t param_base = 0,
        unsigned register_count = 256) = 0;
    virtual std::size_t resident_warp_capacity() const = 0;
    virtual std::size_t resident_thread_capacity() const = 0;
    virtual std::size_t regfile_capacity_bytes() const = 0;
    virtual ExecutionStats execution_stats() const = 0;
    virtual RunResult run(std::size_t budget = 100000) = 0;
    virtual bool step() = 0;
    virtual void reset() = 0;
    virtual std::uint32_t read_register(std::size_t thread, unsigned reg) const = 0;
    virtual void write_register(std::size_t thread, unsigned reg, std::uint32_t value) = 0;
    virtual std::uint32_t read_fcsr(std::size_t thread) const = 0;
    virtual void write_fcsr(std::size_t thread, std::uint32_t value) = 0;
    virtual std::uint32_t read_memory(std::uint32_t address, unsigned size) const = 0;
    virtual void write_memory(std::uint32_t address, unsigned size, std::uint32_t value) = 0;
    virtual bool dependency_ready(
        std::uint64_t encoding, std::size_t warp, std::uint32_t mask) const = 0;
    virtual void reserve(std::uint64_t encoding, std::size_t warp, std::uint32_t mask) = 0;
    virtual void enqueue(
        std::uint64_t encoding, std::size_t warp, std::uint32_t mask, std::uint32_t pc) = 0;
    virtual bool queue_empty(std::size_t warp) const = 0;
    virtual std::uint32_t queue_pc(std::size_t warp) const = 0;
    virtual bool issue(std::size_t warp) = 0;
    virtual void writeback(std::uint64_t encoding,
        std::size_t warp,
        std::uint32_t mask,
        const std::array<std::uint32_t, 32>& values) = 0;
};

std::unique_ptr<Dut> make_dut(const DutConfig& config = {});
}
