#pragma once
#include "helo/commands.h"
#include "helo/config.h"
#include "helo/execution.h"
#include <memory>
#include <variant>
#include <vector>

namespace helo::verif {
using Command = std::variant<helo::ClearCommand, helo::DrawCommand, helo::ComputeCommand>;
std::vector<std::uint32_t> encode_commands(const std::vector<Command>& commands);

// 提供整GPU验证所需的稳定执行计数。
struct GpuCounters {
    std::uint64_t m_draws; // 保存已完成绘制次数。
    std::uint64_t m_dispatches; // 保存已完成计算任务次数。
    std::uint64_t m_vertices; // 保存顶点调用总数。
    std::uint64_t m_fragments; // 保存片元调用总数。
    std::uint64_t m_triangles; // 保存光栅化三角形总数。
    std::uint64_t m_passed; // 保存通过写回测试的片元数。
    std::uint64_t m_shader_steps; // 保存实际着色指令组总数。
};

// 以命令和内存边界隔离功能模型、性能模型与RTL整机测试。
class GpuDut {
public:
    // 允许通过统一接口释放实现对象。
    virtual ~GpuDut() = default;
    virtual void upload_program(
        std::uint32_t address, const std::vector<std::uint64_t>& program) = 0;
    virtual void write(std::uint32_t address, unsigned bytes, std::uint32_t value) = 0;
    virtual std::uint32_t read(std::uint32_t address, unsigned bytes) const = 0;
    void submit(const Command& command);
    std::uint32_t submit_commands(const std::vector<Command>& commands);
    virtual void submit_buffer(std::uint32_t address, std::uint32_t byte_size) = 0;
    virtual helo::SubmitResult run(std::size_t budget = 100000) = 0;
    virtual void reset() = 0;
    virtual std::size_t pending() const = 0;
    virtual GpuCounters counters() const = 0;
    virtual std::vector<std::uint8_t> read_color(
        const helo::FramebufferDesc& framebuffer) const = 0;

protected:
    explicit GpuDut(std::size_t memory_bytes);

private:
    std::uint64_t m_upload_cursor = 0; // 指向本次设备配置的命令暂存区内下一可用字节。
    std::uint64_t m_upload_end = 0; // 保存暂存区末端，使用64位表示32位地址空间末端。
};

std::unique_ptr<GpuDut> make_gpu_dut(const GpuConfig& config = {});
}
