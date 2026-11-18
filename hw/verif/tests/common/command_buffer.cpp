#include "dut/gpu_dut.h"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace helo::verif {
namespace {
// 独立按协议字段顺序构造命令包，不使用CP解码代码或生产编码宏。
class Encoder {
public:
    std::vector<std::uint32_t> m_words{0x48434d01, 0, 256, 0}; // 保存独立编码的包头与负载。

    // 拒绝超出线协议32位字段范围的主机值。
    void word(std::uint64_t value) {
        if (value > 0xffffffffULL) {
            throw std::out_of_range("test command field exceeds 32 bits");
        }
        m_words.push_back(static_cast<std::uint32_t>(value));
    }

    // 将浮点值按原始位模式写入负载。
    void real(float value) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        word(bits);
    }

    // 按固定七字布局编码帧缓冲。
    void framebuffer(const helo::FramebufferDesc& f) {
        word(f.m_color_address);
        word(f.m_depth_address);
        word(f.m_stencil_address);
        word(f.m_width);
        word(f.m_height);
        word(f.m_has_depth);
        word(f.m_has_stencil);
    }

    // 按固定五字布局编码程序描述。
    void program(const helo::ShaderProgram& p) {
        word(p.m_enabled);
        word(p.m_entry);
        word(p.m_param_base);
        word(p.m_register_count);
        word(p.m_step_budget);
    }

    // 编码清除操作及清除值。
    void encode(const helo::ClearCommand& c) {
        m_words[1] = 1;
        framebuffer(c.m_framebuffer);
        for (auto v : c.m_color) {
            real(v);
        }
        real(c.m_depth);
        word(c.m_stencil);
    }

    // 编码计算程序启动操作。
    void encode(const helo::ComputeCommand& c) {
        m_words[1] = 3;
        word(c.m_entry);
        word(c.m_thread_count);
        word(c.m_param_base);
        word(c.m_register_count);
        word(c.m_step_budget);
    }

    // 编码渲染状态快照，字段位置独立对应线协议表。
    void encode(const helo::DrawCommand& c) {
        m_words[1] = 2;
        word(c.m_vertex_address);
        word(c.m_vertex_count);
        word(c.m_vertex_stride);
        word(c.m_indexed);
        word(c.m_index_address);
        word(c.m_index_count);
        const auto& v = c.m_geometry.m_viewport;
        real(v.m_x);
        real(v.m_y);
        real(v.m_width);
        real(v.m_height);
        real(v.m_min_depth);
        real(v.m_max_depth);
        word(static_cast<unsigned>(c.m_geometry.m_cull_mode));
        word(c.m_geometry.m_front_counter_clockwise);
        framebuffer(c.m_framebuffer);
        const auto& d = c.m_rop.m_depth_stencil;
        word(d.m_depth_test);
        word(d.m_depth_write);
        word(static_cast<unsigned>(d.m_depth_compare));
        word(d.m_stencil_test);
        word(static_cast<unsigned>(d.m_stencil_compare));
        word(d.m_stencil_reference);
        word(d.m_stencil_read_mask);
        word(d.m_stencil_write_mask);
        word(static_cast<unsigned>(d.m_stencil_fail));
        word(static_cast<unsigned>(d.m_depth_fail));
        word(static_cast<unsigned>(d.m_pass));
        word(c.m_rop.m_blend);
        word(c.m_rop.m_color_write);
        program(c.m_vertex_shader);
        program(c.m_fragment_shader);
        word(c.m_texture_enabled);
        word(c.m_texture.m_address);
        word(c.m_texture.m_width);
        word(c.m_texture.m_height);
        word(static_cast<unsigned>(c.m_texture.m_filter));
        word(static_cast<unsigned>(c.m_texture.m_wrap_u));
        word(static_cast<unsigned>(c.m_texture.m_wrap_v));
    }
};
}

// 将任意操作序列编码为同一连续command buffer，所有保留字清零。
std::vector<std::uint32_t> encode_commands(const std::vector<Command>& commands) {
    std::vector<std::uint32_t> result;
    for (const auto& command : commands) {
        Encoder e;
        // 将测试描述转换为独立线协议字节，DUT只接收内存范围。
        std::visit([&e](const auto& c) { e.encode(c); }, command);
        if (e.m_words.size() > 64) {
            throw std::logic_error("test packet overflow");
        }
        e.m_words.resize(64, 0);
        result.insert(result.end(), e.m_words.begin(), e.m_words.end());
    }
    return result;
}

// 在实际内存末端保留最多2MiB且不超过容量八分之一的命令暂存区。
GpuDut::GpuDut(std::size_t memory_bytes) {
    const std::uint64_t address_limit = 0x100000000ULL;
    const std::uint64_t bytes = memory_bytes < address_limit ? memory_bytes : address_limit;
    m_upload_end = bytes & ~std::uint64_t(255);
    const auto available = bytes / 8;
    const auto reserved =
        (available < 2 * 1024 * 1024 ? available : 2 * 1024 * 1024) & ~std::uint64_t(255);
    m_upload_cursor = m_upload_end - reserved;
}

// 将单个测试描述编码成单包缓冲区后通过统一设备入口提交。
void GpuDut::submit(const Command& command) {
    (void)submit_commands({command});
}

// 上传连续包并提交一次缓冲区描述，返回地址供解析边界测试使用。
std::uint32_t GpuDut::submit_commands(const std::vector<Command>& commands) {
    const auto words = encode_commands(commands);
    if (words.empty() || words.size() > (m_upload_end - m_upload_cursor) / 4) {
        throw std::out_of_range("test command upload space exhausted");
    }
    const auto address = static_cast<std::uint32_t>(m_upload_cursor);
    for (std::size_t i = 0; i < words.size(); ++i) {
        write(address + static_cast<std::uint32_t>(i * 4), 4, words[i]);
    }
    const auto size = static_cast<std::uint32_t>(words.size() * 4);
    submit_buffer(address, size);
    m_upload_cursor += size;
    return address;
}
}
