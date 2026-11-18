#pragma once
#include <array>
#include <cstdint>
#include <limits>

// 定义各模型、驱动和验证共用的图形记录，不依赖执行模块。
namespace helo {
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24 &&
                  sizeof(float) == 4,
    "graphics protocol requires IEEE binary32");
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53 &&
                  sizeof(double) == 8,
    "graphics reference requires IEEE binary64");
using Color = std::array<float, 4>;
constexpr unsigned MAX_VARYING_COMPONENTS = 16;
using Varyings = std::array<float, MAX_VARYING_COMPONENTS>;
constexpr std::uint32_t ABSENT_ATTRIBUTE = 0xffffffffU;

// 保存裁剪空间位置与待插值的顶点属性。
struct Vertex {
    std::array<float, 4> m_position{0, 0, 0, 1}; // 保存齐次裁剪坐标。
    Color m_color{1, 1, 1, 1}; // 保存线性 RGBA 颜色。
    std::array<float, 2> m_uv{0, 0}; // 保存纹理坐标。
    Varyings m_varyings{}; // 按槽保存自定义FP32属性分量。
};

// 保存左上原点、Y 轴向下的屏幕顶点。
struct ScreenVertex {
    std::array<float, 3> m_position{0, 0, 0}; // 保存屏幕 XY 与窗口深度。
    float m_inv_w = 1; // 保存透视校正使用的齐次 W 倒数。
    Color m_color{1, 1, 1, 1}; // 保存未除 W 的顶点颜色。
    std::array<float, 2> m_uv{0, 0}; // 保存未除 W 的纹理坐标。
    Varyings m_varyings{}; // 按槽保存自定义FP32属性分量。
};

using Triangle = std::array<Vertex, 3>;
using ScreenTriangle = std::array<ScreenVertex, 3>;

// 保存单个像素中心处的光栅化结果。
struct Fragment {
    std::uint32_t m_x = 0; // 保存像素列坐标。
    std::uint32_t m_y = 0; // 保存像素行坐标。
    float m_depth = 0; // 保存线性插值的窗口深度。
    Color m_color{1, 1, 1, 1}; // 保存透视校正后的颜色。
    std::array<float, 2> m_uv{0, 0}; // 保存透视校正后的纹理坐标。
    Varyings m_varyings{}; // 按槽保存自定义FP32属性分量。
    unsigned m_sample = 0; // 指定像素内的样本编号。
    Color m_color1{}; // 保存第二颜色目标输出。
    std::array<std::int32_t, 2> m_q16_uv{}; // 保留定点纹理寻址所需的完整位数。
};

// 选择屏幕坐标与基础颜色、深度、UV插值精度。
enum class RasterPrecision { FLOAT, Q4_Q16_V1 };

// 可选半开像素矩形，以零宽高表示空矩形。
struct Scissor {
    bool m_enabled = false; // 选择是否限制覆盖范围。
    std::uint32_t m_x = 0; // 保存包含的最左列。
    std::uint32_t m_y = 0; // 保存包含的最上行。
    std::uint32_t m_width = 0; // 保存水平像素数。
    std::uint32_t m_height = 0; // 保存垂直像素数。
};

// 定义裁剪空间到屏幕坐标与窗口深度的映射。
struct Viewport {
    float m_x = 0; // 保存视口左边界。
    float m_y = 0; // 保存视口上边界。
    float m_width = 1; // 保存正值视口宽度。
    float m_height = 1; // 保存正值视口高度。
    float m_min_depth = 0; // 保存近裁剪平面映射深度。
    float m_max_depth = 1; // 保存远裁剪平面映射深度。
};
enum class CullMode { NONE, FRONT, BACK };

// 配置几何阶段的视口与面剔除约定。
struct GeometryState {
    Viewport m_viewport; // 保存视口映射参数。
    CullMode m_cull_mode = CullMode::NONE; // 选择需要剔除的面。
    bool m_front_counter_clockwise = true; // 指定 NDC 中逆时针绕序为正面。
};
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

// 保存深度与模板测试、更新策略。
struct DepthStencilState {
    bool m_depth_test = true; // 控制深度比较。
    bool m_depth_write = true; // 控制通过测试时的深度更新。
    CompareOp m_depth_compare = CompareOp::LESS; // 选择深度比较函数。
    bool m_stencil_test = false; // 控制模板测试与模板更新。
    CompareOp m_stencil_compare = CompareOp::ALWAYS; // 比较掩码后的参考值与缓冲值。
    std::uint8_t m_stencil_reference = 0; // 保存模板参考值。
    std::uint8_t m_stencil_read_mask = 255; // 指定模板比较位。
    std::uint8_t m_stencil_write_mask = 255; // 指定模板可更新位。
    StencilOp m_stencil_fail = StencilOp::KEEP; // 选择模板失败操作。
    StencilOp m_depth_fail = StencilOp::KEEP; // 选择深度失败操作。
    StencilOp m_pass = StencilOp::KEEP; // 选择全部通过操作。
};

// 描述统一内存中的紧密RGBA8、FP32深度和8位模板附件。
struct FramebufferDesc {
    std::uint32_t m_color_address = 0; // 保存RGBA8颜色附件起始字节地址。
    std::uint32_t m_depth_address = 0; // 保存FP32深度附件起始字节地址。
    std::uint32_t m_stencil_address = 0; // 保存8位模板附件起始字节地址。
    std::uint32_t m_width = 0; // 保存目标像素宽度。
    std::uint32_t m_height = 0; // 保存目标像素高度。
    bool m_has_depth = true; // 指明是否配置深度附件。
    bool m_has_stencil = false; // 指明是否配置模板附件。
    unsigned m_samples = 1; // 选择每像素一个或四个交错存储样本。
    bool m_has_color1 = false; // 选择第二个颜色附件。
    std::uint32_t m_color1_address = 0; // 保存第二个RGBA8附件地址。
    bool m_srgb = false; // 选择颜色附件的sRGB编码，透明度保持线性。
};

// 控制加法混合的源项和目标项权重。
enum class BlendFactor {
    ZERO,
    ONE,
    SRC_ALPHA,
    INV_SRC_ALPHA,
    DST_ALPHA,
    INV_DST_ALPHA,
    SRC_COLOR,
    INV_SRC_COLOR,
    DST_COLOR,
    INV_DST_COLOR
};

struct BlendFactors {
    BlendFactor m_src_rgb = BlendFactor::SRC_ALPHA; // 设置源RGB权重。
    BlendFactor m_dst_rgb = BlendFactor::INV_SRC_ALPHA; // 设置目标RGB权重。
    BlendFactor m_src_alpha = BlendFactor::ONE; // 设置源透明度权重。
    BlendFactor m_dst_alpha = BlendFactor::INV_SRC_ALPHA; // 设置目标透明度权重。
};

// 保存片元写回的固定功能状态。
struct RopState {
    DepthStencilState m_depth_stencil; // 保存深度与模板策略。
    bool m_blend = false; // 启用非预乘源透明度混合。
    bool m_color_write = true; // 控制颜色附件更新。
    BlendFactors m_factors{}; // 选择RGB和透明度各自的加法混合权重。
};
enum class TextureFilter { NEAREST, BILINEAR };
enum class TextureWrap { CLAMP, REPEAT };

// 描述紧密存储的RGBA8二维纹理及采样方式。
struct TextureDesc {
    std::uint32_t m_address = 0; // 保存首个RGBA8纹素的字节地址。
    std::uint32_t m_width = 0; // 保存纹理宽度。
    std::uint32_t m_height = 0; // 保存纹理高度。
    TextureFilter m_filter = TextureFilter::NEAREST; // 选择纹素过滤方式。
    TextureWrap m_wrap_u = TextureWrap::CLAMP; // 选择横向边界处理。
    TextureWrap m_wrap_v = TextureWrap::CLAMP; // 选择纵向边界处理。
};
}
