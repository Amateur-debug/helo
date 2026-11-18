#pragma once
#include "graphics_types.h"
#include <memory>

namespace helo::verif {
// 规定Rop模块各实现共用的功能行为验证接口。
class RopDut {
public:
    // 允许通过模块接口释放适配器。
    virtual ~RopDut() = default;
    virtual std::uint32_t read_memory(std::uint32_t address, unsigned size = 4) = 0;
    virtual void write_memory(std::uint32_t address, std::uint32_t value) = 0;
    virtual void clear(const FramebufferDesc& framebuffer,
        const helo::Color& color,
        float depth,
        std::uint8_t stencil) = 0;
    virtual bool write_fragment(const FramebufferDesc& framebuffer,
        const RopState& state,
        const helo::Fragment& fragment) = 0;
    virtual std::uint32_t pack(const helo::Color& color) = 0;
    virtual helo::Color blend(const helo::Color& source, const helo::Color& destination) = 0;
    virtual bool compare(CompareOp operation, float source, float destination) = 0;
    virtual std::uint8_t stencil(
        StencilOp operation, std::uint8_t stored, std::uint8_t reference, std::uint8_t mask) = 0;
};

std::unique_ptr<RopDut> make_rop_dut();
}
