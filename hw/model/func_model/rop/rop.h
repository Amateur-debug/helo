#pragma once
#include "common/types.h"
#include "mem/mem.h"
#include "rop/crop.h"
#include "rop/zrop.h"

namespace helo::func {
// 顺序执行模板、深度、颜色混合与统一内存写回。
class Rop {
public:
    // 保存附件布局的验证结果，片元和ROP状态仍在每次写入时检查。
    class Target {
    public:
        bool write(const RopState& state, const Fragment& fragment) const;

    private:
        friend class Rop;
        Target(Rop& owner, const FramebufferDesc& framebuffer);
        Rop& m_owner; // 引用必须比目标句柄活得更久的ROP。
        FramebufferDesc m_framebuffer; // 保存本次绘制的不可变附件布局。
    };

    explicit Rop(Mem& memory);
    Target prepare(const FramebufferDesc& framebuffer);
    void validate(const FramebufferDesc& framebuffer) const;
    void clear(const FramebufferDesc& framebuffer,
        const Color& color,
        float depth = 1.0f,
        std::uint8_t stencil = 0);
    // 目标外坐标及非有限或不在[0,1]内的深度返回false，不修改附件；非法描述符抛出异常。
    bool write(const FramebufferDesc& framebuffer, const RopState& state, const Fragment& fragment);

private:
    bool write_validated(
        const FramebufferDesc& framebuffer, const RopState& state, const Fragment& fragment);
    Zrop m_zrop; // 持有深度模板比较与更新单元。
    Crop m_crop; // 持有颜色量化与混合单元。
    Mem& m_memory; // 引用全部帧缓冲附件所在的统一内存。
};
}
