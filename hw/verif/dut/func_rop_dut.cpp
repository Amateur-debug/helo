#include "rop/rop.h"
#include "rop_dut.h"

namespace helo::verif {
// 将共用附件描述转换为功能实现的输入状态。
static helo::FramebufferDesc framebuffer_desc(const FramebufferDesc& input) {
    return {input.m_color_address,
        input.m_depth_address,
        input.m_stencil_address,
        input.m_width,
        input.m_height,
        input.m_has_depth,
        input.m_has_stencil};
}

// 将独立测试协议的固定功能状态转换为实现状态。
static helo::RopState rop_state(const RopState& input) {
    helo::RopState result;
    auto& depth = result.m_depth_stencil;
    depth.m_depth_test = input.m_depth_test;
    depth.m_depth_write = input.m_depth_write;
    depth.m_depth_compare = static_cast<helo::CompareOp>(input.m_depth_compare);
    depth.m_stencil_test = input.m_stencil_test;
    depth.m_stencil_compare = static_cast<helo::CompareOp>(input.m_stencil_compare);
    depth.m_stencil_reference = input.m_stencil_reference;
    depth.m_stencil_read_mask = input.m_stencil_read_mask;
    depth.m_stencil_write_mask = input.m_stencil_write_mask;
    depth.m_stencil_fail = static_cast<helo::StencilOp>(input.m_stencil_fail);
    depth.m_depth_fail = static_cast<helo::StencilOp>(input.m_depth_fail);
    depth.m_pass = static_cast<helo::StencilOp>(input.m_pass);
    result.m_blend = input.m_blend;
    result.m_color_write = input.m_color_write;
    return result;
}

// 将Rop模块共用测试转发至功能实现。
class FuncRopDut final : public RopDut {
public:
    // 更新测试内存中的一个字。
    void write_memory(std::uint32_t address, std::uint32_t value) override {
        m_memory.write(address, 4, value);
    }

    // 读取附件或测试内存结果。
    std::uint32_t read_memory(std::uint32_t address, unsigned size) override {
        return m_memory.read(address, size);
    }

    // 清除附件以建立可比较的初始状态。
    void clear(const FramebufferDesc& framebuffer,
        const helo::Color& color,
        float depth,
        std::uint8_t stencil_value) override {
        m_rop.clear(framebuffer_desc(framebuffer), color, depth, stencil_value);
    }

    // 执行逐片元测试、混合与写回。
    bool write_fragment(const FramebufferDesc& framebuffer,
        const RopState& state,
        const helo::Fragment& fragment) override {
        return m_rop.write(framebuffer_desc(framebuffer), rop_state(state), fragment);
    }

    // 获取颜色量化结果。
    std::uint32_t pack(const helo::Color& color) override {
        return m_crop.pack(color);
    }

    // 获取非预乘透明度混合结果。
    helo::Color blend(const helo::Color& source, const helo::Color& destination) override {
        return m_crop.blend(source, destination);
    }

    // 执行深度模板共用比较。
    bool compare(CompareOp operation, float source, float destination) override {
        return m_zrop.compare(static_cast<helo::CompareOp>(operation), source, destination);
    }

    // 执行模板更新操作。
    std::uint8_t stencil(StencilOp operation,
        std::uint8_t stored,
        std::uint8_t reference,
        std::uint8_t mask) override {
        return m_zrop.stencil(static_cast<helo::StencilOp>(operation), stored, reference, mask);
    }

private:
    func::Mem m_memory{65536}; // 保存颜色、深度与模板附件。
    func::Rop m_rop{m_memory}; // 驱动片元提交模块。
    func::Crop m_crop; // 驱动独立颜色计算单元。
    func::Zrop m_zrop; // 驱动独立深度模板单元。
};

// 为一次模块测试建立独立的功能适配器。
std::unique_ptr<RopDut> make_rop_dut() {
    return std::make_unique<FuncRopDut>();
}
}
