#pragma once
#include "graphics_types.h"
#include <memory>

namespace helo::verif {
// 规定Tex模块各实现共用的功能行为验证接口。
class TexDut {
public:
    // 允许通过模块接口释放适配器。
    virtual ~TexDut() = default;
    virtual void write_memory(std::uint32_t address, std::uint32_t value) = 0;
    virtual helo::Color sample(
        const TextureDesc& texture, float u, float v, unsigned levels = 1, float lod = 0) = 0;
};

std::unique_ptr<TexDut> make_tex_dut();
}
