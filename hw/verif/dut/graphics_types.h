#pragma once
#include "helo/graphics.h"

namespace helo::verif {
enum class TextureFilter { NEAREST, BILINEAR };
enum class TextureWrap { CLAMP, REPEAT };
enum class CompareOp { NEVER, LESS, EQUAL, LESS_EQUAL, GREATER, NOT_EQUAL, GREATER_EQUAL, ALWAYS };
enum class StencilOp {
    KEEP,
    ZERO,
    REPLACE,
    INCREMENT_CLAMP,
    DECREMENT_CLAMP,
    INVERT,
    INCREMENT_WRAP,
    DECREMENT_WRAP
};

// 描述统一测试纹理的布局与采样状态。
struct TextureDesc {
    std::uint32_t m_address = 0; // 指定纹素起始地址。
    std::uint32_t m_width = 2; // 指定纹理宽度。
    std::uint32_t m_height = 2; // 指定纹理高度。
    TextureFilter m_filter = TextureFilter::NEAREST; // 指定过滤方式。
    TextureWrap m_wrap_u = TextureWrap::CLAMP; // 指定横向边界方式。
    TextureWrap m_wrap_v = TextureWrap::CLAMP; // 指定纵向边界方式。
};

// 描述统一测试帧缓冲附件。
struct FramebufferDesc {
    std::uint32_t m_color_address = 1024; // 指定颜色附件地址。
    std::uint32_t m_depth_address = 2048; // 指定深度附件地址。
    std::uint32_t m_stencil_address = 3072; // 指定模板附件地址。
    std::uint32_t m_width = 2; // 指定附件宽度。
    std::uint32_t m_height = 2; // 指定附件高度。
    bool m_has_depth = true; // 控制深度附件存在性。
    bool m_has_stencil = true; // 控制模板附件存在性。
};

// 描述独立于实现的逐片元状态。
struct RopState {
    bool m_depth_test = true; // 控制深度比较。
    bool m_depth_write = true; // 控制深度写入。
    CompareOp m_depth_compare = CompareOp::LESS; // 选择深度比较。
    bool m_stencil_test = false; // 控制模板比较。
    CompareOp m_stencil_compare = CompareOp::ALWAYS; // 选择模板比较。
    std::uint8_t m_stencil_reference = 0; // 指定模板参考值。
    std::uint8_t m_stencil_read_mask = 255; // 指定比较有效位。
    std::uint8_t m_stencil_write_mask = 255; // 指定更新有效位。
    StencilOp m_stencil_fail = StencilOp::KEEP; // 指定模板失败操作。
    StencilOp m_depth_fail = StencilOp::KEEP; // 指定深度失败操作。
    StencilOp m_pass = StencilOp::KEEP; // 指定成功操作。
    bool m_blend = false; // 控制透明度混合。
    bool m_color_write = true; // 控制颜色写入。
};
}
