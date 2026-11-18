#include "gpu/gpu.h"
#include "gpu_dut.h"

namespace helo::verif {
namespace {
// 将统一整机验证接口转发至实际GPU功能模型。
class FuncGpuDut final : public GpuDut {
public:
    // 为一次独立整机实验分配统一设备内存。
    explicit FuncGpuDut(const GpuConfig& config) : GpuDut(config.m_memory_bytes), m_gpu(config) {
    }

    // 将独立编码的机器指令装载到设备内存。
    void upload_program(std::uint32_t address, const std::vector<std::uint64_t>& program) override {
        m_gpu.memory().load_instructions(address, program);
    }

    // 经设备存储接口写入资源数据。
    void write(std::uint32_t address, unsigned bytes, std::uint32_t value) override {
        m_gpu.memory().write(address, bytes, value);
    }

    // 经设备存储接口读取执行结果。
    std::uint32_t read(std::uint32_t address, unsigned bytes) const override {
        return m_gpu.memory().read(address, bytes);
    }

    // 仅提交缓冲区范围，由真实CP读取和解析内存中的命令。
    void submit_buffer(std::uint32_t address, std::uint32_t byte_size) override {
        m_gpu.submit(address, byte_size);
    }

    // 推进真实命令处理器的给定命令预算。
    helo::SubmitResult run(std::size_t budget) override {
        return m_gpu.run(budget);
    }

    // 清除设备执行状态但保留存储资源。
    void reset() override {
        m_gpu.reset();
    }

    // 查询命令处理器中尚未完成的命令。
    std::size_t pending() const override {
        return m_gpu.pending();
    }

    // 将实现统计转换为共用验证计数。
    GpuCounters counters() const override {
        const auto& s = m_gpu.stats();
        return {s.m_draw_calls,
            s.m_dispatches,
            s.m_vertex_invocations,
            s.m_fragment_invocations,
            s.m_raster_triangles,
            s.m_passed_fragments,
            s.m_shader_steps};
    }

    // 从真实颜色附件读取紧密RGBA8数据。
    std::vector<std::uint8_t> read_color(const helo::FramebufferDesc& framebuffer) const override {
        return m_gpu.read_color(framebuffer);
    }

private:
    helo::func::Gpu m_gpu; // 持有被测GPU模型及其统一内存。
};
}

// 创建用于当前功能模型的整机适配器。
std::unique_ptr<GpuDut> make_gpu_dut(const GpuConfig& config) {
    return std::make_unique<FuncGpuDut>(config);
}
}
