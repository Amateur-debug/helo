#include "Vhelo_flat_draw.h"
#include "mem/mem.h"
#include "rast/rast.h"
#include "rop/rop.h"
#include "tex/tex.h"
#include "verilated.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {
// 描述当前受限绘制入口的平面或逐顶点Q16属性。
struct DrawAttributes {
    bool enabled = false;
    std::int32_t u_q16 = 0;
    std::int32_t v_q16 = 0;
    bool repeat_u = false;
    bool repeat_v = false;
    bool interpolate_uv = false;
    std::array<std::uint32_t, 3> inv_w_q16{65536, 65536, 65536};
    std::array<std::int32_t, 3> vertex_u_q16{0, 0, 0};
    std::array<std::int32_t, 3> vertex_v_q16{0, 0, 0};
    bool interpolate_color = false;
    std::array<std::uint32_t, 3> vertex_rgba{0xffffffff, 0xffffffff, 0xffffffff};
};

// 产生确定性的三角形和颜色配置。
std::uint32_t next_random(std::uint32_t& seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

// 保留窗口深度原始FP32位模式。
std::uint32_t bits(float value) {
    std::uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

// 推进时钟并更新组合端口。
void tick(Vhelo_flat_draw& top) {
    top.clk = 0;
    top.eval();
    top.clk = 1;
    top.eval();
    top.clk = 0;
    top.eval();
}

// 在真实共享内存的主机端口上设置附件初始值。
void host_write(
    Vhelo_flat_draw& top, helo::func::Mem& memory, unsigned address, std::uint32_t value) {
    top.host_valid = 1;
    top.host_write = 1;
    top.host_addr = address;
    top.host_size = 4;
    top.host_wdata = value;
    top.eval();
    if (top.host_error) {
        throw std::runtime_error("flat draw upload failed");
    }
    tick(top);
    top.host_valid = 0;
    memory.write(address, 4, value);
}

// 保证所有像素颜色/深度与功能模型相同，包括未覆盖和被深度拒绝的像素。
void compare_image(Vhelo_flat_draw& top, const helo::func::Mem& memory) {
    top.host_valid = 1;
    top.host_write = 0;
    top.host_size = 4;
    for (unsigned pixel = 0; pixel < 64; ++pixel) {
        for (const auto address : {256u + 4 * pixel, 1024u + 4 * pixel}) {
            top.host_addr = address;
            top.eval();
            if (top.host_error || top.host_rdata != memory.read(address, 4)) {
                std::cerr << "flat draw memory mismatch pixel=" << pixel << " address=" << address
                          << " rtl=" << top.host_rdata << " expected=" << memory.read(address, 4)
                          << '\n';
                throw std::runtime_error("draw attachment differs from functional pipeline");
            }
        }
    }
    top.host_valid = 0;
}

// 生成与Q4整数网格精确一致的C++屏幕三角形。
helo::ScreenTriangle make_triangle(const std::array<std::array<int, 2>, 3>& points) {
    helo::ScreenTriangle triangle;
    for (unsigned i = 0; i < 3; ++i) {
        triangle[i].m_position[0] = float(points[i][0]) / 16;
        triangle[i].m_position[1] = float(points[i][1]) / 16;
    }
    return triangle;
}

// 逐图元比较RTL有序片元提交与C++ Rast→Rop链路。
void compare_draw(Vhelo_flat_draw& top,
    helo::func::Mem& memory,
    const helo::func::Rast& rast,
    const helo::func::Tex& tex,
    helo::func::Rop& rop,
    const std::array<std::array<int, 2>, 3>& points,
    float depth,
    std::uint32_t rgba,
    bool blend = false,
    bool interpolate_depth = false,
    const std::array<std::uint32_t, 3>& vertex_depth_q16 = {0, 0, 0},
    DrawAttributes texture = {}) {
    helo::FramebufferDesc fb;
    fb.m_width = 8;
    fb.m_height = 8;
    fb.m_color_address = 256;
    fb.m_depth_address = 1024;
    fb.m_has_depth = true;
    fb.m_has_stencil = false;
    helo::RopState state;
    state.m_blend = blend;
    helo::TextureDesc desc;
    if (texture.enabled) {
        desc.m_address = 2048;
        desc.m_width = 4;
        desc.m_height = 4;
        desc.m_wrap_u = texture.repeat_u ? helo::TextureWrap::REPEAT : helo::TextureWrap::CLAMP;
        desc.m_wrap_v = texture.repeat_v ? helo::TextureWrap::REPEAT : helo::TextureWrap::CLAMP;
    }
    unsigned covered = 0;
    unsigned passed = 0;
    auto triangle = make_triangle(points);
    for (unsigned i = 0; i < 3; ++i) {
        triangle[i].m_position[2] = float(vertex_depth_q16[i]) / 65536.f;
        triangle[i].m_inv_w = float(texture.inv_w_q16[i]) / 65536.f;
        triangle[i].m_uv[0] = float(texture.vertex_u_q16[i]) / 65536.f;
        triangle[i].m_uv[1] = float(texture.vertex_v_q16[i]) / 65536.f;
        for (unsigned c = 0; c < 4; ++c) {
            triangle[i].m_color[c] = float((texture.vertex_rgba[i] >> (c * 8)) & 255) / 255.f;
        }
    }
    for (auto fragment : rast.rasterize(triangle, 8, 8, helo::RasterPrecision::Q4_Q16_V1)) {
        ++covered;
        if (!interpolate_depth) {
            fragment.m_depth = depth;
        }
        std::uint32_t expected_rgba = rgba;
        if (texture.interpolate_color) {
            expected_rgba = 0;
            for (unsigned c = 0; c < 4; ++c) {
                const auto channel = std::uint32_t(std::floor(fragment.m_color[c] * 255.f + .5f));
                expected_rgba |= channel << (c * 8);
            }
        }
        if (texture.enabled) {
            const auto u_q16 = texture.interpolate_uv ? fragment.m_q16_uv[0] : texture.u_q16;
            const auto v_q16 = texture.interpolate_uv ? fragment.m_q16_uv[1] : texture.v_q16;
            const auto sample = tex.prepare(desc).sample_q16(u_q16, v_q16);
            const auto source = expected_rgba;
            expected_rgba = 0;
            for (unsigned c = 0; c < 4; ++c) {
                const auto channel =
                    std::uint32_t(std::floor(float((source >> (c * 8)) & 255) * sample[c] + .5f));
                expected_rgba |= channel << (8 * c);
            }
        }
        for (unsigned c = 0; c < 4; ++c) {
            fragment.m_color[c] = float((expected_rgba >> (c * 8)) & 255) / 255.0f;
        }
        if (rop.write(fb, state, fragment)) {
            ++passed;
        }
    }
    if (!top.draw_ready || top.coverage_busy) {
        throw std::runtime_error("flat draw did not release previous triangle");
    }
    top.width = 8;
    top.height = 8;
    top.color_base = 256;
    top.depth_base = 1024;
    top.x0 = std::uint32_t(points[0][0]) & 0xfffff;
    top.y0 = std::uint32_t(points[0][1]) & 0xfffff;
    top.x1 = std::uint32_t(points[1][0]) & 0xfffff;
    top.y1 = std::uint32_t(points[1][1]) & 0xfffff;
    top.x2 = std::uint32_t(points[2][0]) & 0xfffff;
    top.y2 = std::uint32_t(points[2][1]) & 0xfffff;
    top.flat_depth = bits(depth);
    top.flat_rgba = rgba;
    top.blend = blend;
    top.interpolate_depth = interpolate_depth;
    top.depth0_q16 = vertex_depth_q16[0];
    top.depth1_q16 = vertex_depth_q16[1];
    top.depth2_q16 = vertex_depth_q16[2];
    top.texture_enable = texture.enabled;
    top.texture_base = 2048;
    top.texture_width = 4;
    top.texture_height = 4;
    top.texture_u_q16 = texture.u_q16;
    top.texture_v_q16 = texture.v_q16;
    top.texture_repeat_u = texture.repeat_u;
    top.texture_repeat_v = texture.repeat_v;
    top.texture_interpolate_uv = texture.interpolate_uv;
    top.inv_w0_q16 = texture.inv_w_q16[0];
    top.inv_w1_q16 = texture.inv_w_q16[1];
    top.inv_w2_q16 = texture.inv_w_q16[2];
    top.u0_q16 = texture.vertex_u_q16[0];
    top.u1_q16 = texture.vertex_u_q16[1];
    top.u2_q16 = texture.vertex_u_q16[2];
    top.v0_q16 = texture.vertex_v_q16[0];
    top.v1_q16 = texture.vertex_v_q16[1];
    top.v2_q16 = texture.vertex_v_q16[2];
    top.interpolate_color = texture.interpolate_color;
    top.rgba0 = texture.vertex_rgba[0];
    top.rgba1 = texture.vertex_rgba[1];
    top.rgba2 = texture.vertex_rgba[2];
    top.draw_valid = 1;
    top.eval();
    tick(top);
    top.draw_valid = 0;
    top.blend = 0;
    top.interpolate_depth = 0;
    top.texture_enable = 0;
    top.texture_interpolate_uv = 0;
    top.interpolate_color = 0;
    bool completed = false;
    for (unsigned cycle = 0; cycle < 2000; ++cycle) {
        tick(top);
        if (top.draw_error) {
            throw std::runtime_error("valid flat draw faulted");
        }
        if (top.draw_done) {
            completed = true;
            break;
        }
    }
    if (!completed || top.covered_count != covered || top.passed_count != passed ||
        !top.draw_ready) {
        std::cerr << "triangle covered=" << covered << " passed=" << passed
                  << " RTL covered=" << top.covered_count << " passed=" << top.passed_count << '\n';
        throw std::runtime_error("flat draw count or completion mismatch");
    }
    try {
        compare_image(top, memory);
    } catch (...) {
        std::cerr << "draw rgba=" << rgba << " blend=" << blend << " texture=" << texture.enabled
                  << " uv=" << texture.u_q16 << ',' << texture.v_q16
                  << " repeat=" << texture.repeat_u << ',' << texture.repeat_v
                  << " interp_uv=" << texture.interpolate_uv
                  << " interp_color=" << texture.interpolate_color << '\n';
        throw;
    }
}

// 验证覆盖、遮挡、多图元顺序与复位保留附件内容。
void run_test() {
    VerilatedContext context;
    Vhelo_flat_draw top(&context);
    helo::func::Mem memory(4096);
    helo::func::Rast rast;
    helo::func::Tex tex(memory);
    helo::func::Rop rop(memory);
    top.capacity_bytes = 4096;
    top.host_valid = 0;
    top.fetch_valid = 0;
    top.fetch_addr = 0;
    top.draw_valid = 0;
    top.blend = 0;
    top.interpolate_depth = 0;
    top.texture_enable = 0;
    top.texture_interpolate_uv = 0;
    top.interpolate_color = 0;
    top.reset = 1;
    tick(top);
    top.reset = 0;
    top.eval();
    for (unsigned pixel = 0; pixel < 64; ++pixel) {
        host_write(top, memory, 256 + 4 * pixel, 0xff101010);
        host_write(top, memory, 1024 + 4 * pixel, bits(1.f));
    }
    for (unsigned texel = 0; texel < 16; ++texel) {
        const auto color = (std::uint32_t((texel * 53 + 91) & 255) << 24) |
                           (std::uint32_t((texel * 107 + 33) & 255) << 16) |
                           (std::uint32_t((texel * 31 + 17) & 255) << 8) |
                           std::uint32_t((texel * 67 + 211) & 255);
        host_write(top, memory, 2048 + 4 * texel, color);
    }
    using Triangle = std::array<std::array<int, 2>, 3>;
    compare_draw(
        top, memory, rast, tex, rop, Triangle{{{0, 0}, {128, 0}, {128, 128}}}, .5f, 0xff0000ff);
    compare_draw(
        top, memory, rast, tex, rop, Triangle{{{0, 0}, {128, 128}, {0, 128}}}, .5f, 0xff00ff00);
    compare_draw(
        top, memory, rast, tex, rop, Triangle{{{128, 128}, {128, 0}, {0, 0}}}, .75f, 0xffff0000);
    compare_draw(top,
        memory,
        rast,
        tex,
        rop,
        Triangle{{{-32, -32}, {80, -32}, {-32, 80}}},
        .25f,
        0xff3399dd);
    std::uint32_t seed = 0x44524157;
    for (unsigned trial = 0; trial < 64; ++trial) {
        Triangle triangle{};
        for (auto& point : triangle) {
            point = {int(next_random(seed) % 193) - 32, int(next_random(seed) % 193) - 32};
        }
        const std::array<float, 4> depths{.125f, .25f, .5f, .75f};
        compare_draw(top,
            memory,
            rast,
            tex,
            rop,
            triangle,
            depths[next_random(seed) % depths.size()],
            next_random(seed));
    }
    for (unsigned trial = 0; trial < 64; ++trial) {
        Triangle triangle{};
        for (auto& point : triangle) {
            point = {int(next_random(seed) % 161) - 16, int(next_random(seed) % 161) - 16};
        }
        const std::array<std::uint32_t, 3> depths{
            next_random(seed) % 65537, next_random(seed) % 65537, next_random(seed) % 65537};
        compare_draw(top,
            memory,
            rast,
            tex,
            rop,
            triangle,
            0.f,
            next_random(seed),
            (trial & 1) != 0,
            true,
            depths);
    }
    const std::array<std::int32_t, 11> coordinates{
        -131072, -65536, -16384, -1, 0, 16384, 32768, 65535, 65536, 81920, 131072};
    for (unsigned trial = 0; trial < 44; ++trial) {
        const DrawAttributes texture{true,
            coordinates[trial % coordinates.size()],
            coordinates[(trial * 7 + 3) % coordinates.size()],
            (trial & 1) != 0,
            (trial & 2) != 0};
        compare_draw(top,
            memory,
            rast,
            tex,
            rop,
            Triangle{{{0, 0}, {128, 0}, {0, 128}}},
            .0625f,
            next_random(seed),
            (trial & 4) != 0,
            false,
            {0, 0, 0},
            texture);
    }
    for (unsigned trial = 0; trial < 16; ++trial) {
        for (unsigned pixel = 0; pixel < 64; ++pixel) {
            host_write(top, memory, 1024 + 4 * pixel, bits(1.f));
        }
        DrawAttributes texture;
        texture.enabled = true;
        texture.repeat_u = (trial & 1) != 0;
        texture.repeat_v = (trial & 2) != 0;
        texture.interpolate_uv = true;
        texture.inv_w_q16 = {65536, (trial & 4) != 0 ? 16384u : 65536u, 32768};
        texture.vertex_u_q16 = {-16384, 65536, 8192};
        texture.vertex_v_q16 = {65536, -16384, 57344};
        texture.interpolate_color = (trial & 1) != 0;
        texture.vertex_rgba = {0x7f20aaff, 0xccee1100, 0x40ff5533};
        const Triangle points = (trial & 8) != 0 ? Triangle{{{0, 128}, {128, 0}, {0, 0}}}
                                                 : Triangle{{{0, 0}, {128, 0}, {0, 128}}};
        compare_draw(top,
            memory,
            rast,
            tex,
            rop,
            points,
            .03125f,
            0xffdedac4,
            (trial & 4) != 0,
            false,
            {0, 0, 0},
            texture);
    }
    for (unsigned trial = 0; trial < 8; ++trial) {
        for (unsigned pixel = 0; pixel < 64; ++pixel) {
            host_write(top, memory, 1024 + 4 * pixel, bits(1.f));
        }
        DrawAttributes attributes;
        attributes.interpolate_color = true;
        attributes.inv_w_q16 = {32768, 65536, (trial & 1) != 0 ? 16384u : 65536u};
        attributes.vertex_rgba = {0x800020ff, 0xccff8100, 0x55aaff66};
        const Triangle points = (trial & 2) != 0 ? Triangle{{{0, 128}, {128, 0}, {0, 0}}}
                                                 : Triangle{{{0, 0}, {128, 0}, {0, 128}}};
        compare_draw(top,
            memory,
            rast,
            tex,
            rop,
            points,
            .03125f,
            0,
            (trial & 4) != 0,
            false,
            {0, 0, 0},
            attributes);
    }
    // 宽三角形的Q8边值乘透视权重和大UV超出64位，逐像素比较纹理重复寻址。
    for (unsigned pixel = 0; pixel < 64; ++pixel) {
        host_write(top, memory, 1024 + 4 * pixel, bits(1.f));
    }
    DrawAttributes wide;
    wide.enabled = true;
    wide.interpolate_uv = true;
    wide.interpolate_color = true;
    wide.repeat_u = true;
    wide.repeat_v = true;
    wide.vertex_u_q16 = {-2147483648, 2147483520, -1073741824};
    wide.vertex_v_q16 = {1073741824, 2147483520, -2147483648};
    wide.inv_w_q16 = {65536, 65535, 32768};
    wide.vertex_rgba = {0xff123456, 0xff876543, 0xffff6699};
    compare_draw(top,
        memory,
        rast,
        tex,
        rop,
        Triangle{{{-524288, -524288}, {524272, 0}, {0, 524272}}},
        0,
        0,
        false,
        true,
        {0, 65536, 32767},
        wide);
    // 大的绝对坐标紧贴纹素边界，转成binary32会丢失最低一位Q16小数。
    for (unsigned pixel = 0; pixel < 64; ++pixel) {
        host_write(top, memory, 1024 + 4 * pixel, bits(1.f));
    }
    wide.interpolate_uv = false;
    wide.u_q16 = 0x7fff3fff;
    wide.v_q16 = -0x7fff4001;
    compare_draw(top,
        memory,
        rast,
        tex,
        rop,
        Triangle{{{0, 0}, {128, 0}, {0, 128}}},
        .5f,
        0xffffffff,
        false,
        false,
        {},
        wide);
    top.texture_enable = 1;
    top.texture_interpolate_uv = 1;
    top.inv_w0_q16 = 0;
    top.texture_width = 4;
    top.draw_valid = 1;
    tick(top);
    top.draw_valid = 0;
    if (!top.draw_error || top.draw_done || top.draw_ready || top.covered_count != 0) {
        throw std::runtime_error("invalid inverse w did not latch a draw error");
    }
    compare_image(top, memory);
    top.reset = 1;
    tick(top);
    top.reset = 0;
    top.eval();
    top.texture_enable = 1;
    top.texture_interpolate_uv = 0;
    top.texture_width = 0;
    top.draw_valid = 1;
    tick(top);
    top.draw_valid = 0;
    if (!top.draw_error || top.draw_done || top.draw_ready || top.covered_count != 0) {
        throw std::runtime_error("invalid texture descriptor did not latch a draw error");
    }
    compare_image(top, memory);
    top.reset = 1;
    tick(top);
    top.reset = 0;
    top.texture_enable = 0;
    top.eval();
    top.width = 8;
    top.height = 8;
    top.color_base = 256;
    top.depth_base = 258;
    top.x0 = 0;
    top.y0 = 0;
    top.x1 = 128;
    top.y1 = 0;
    top.x2 = 128;
    top.y2 = 128;
    top.flat_depth = bits(.25f);
    top.flat_rgba = 0xff88cc55;
    top.draw_valid = 1;
    tick(top);
    top.draw_valid = 0;
    top.blend = 0;
    bool faulted = false;
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        tick(top);
        if (top.draw_error) {
            faulted = true;
            break;
        }
    }
    if (!faulted || top.draw_ready || top.draw_done || top.passed_count != 0) {
        throw std::runtime_error("invalid attachment layout did not latch a draw error");
    }
    compare_image(top, memory);
    top.reset = 1;
    tick(top);
    top.reset = 0;
    top.eval();
    compare_image(top, memory);
    top.width = 8;
    top.height = 8;
    top.color_base = 256;
    top.depth_base = 258;
    top.x0 = 0;
    top.y0 = 0;
    top.x1 = 0;
    top.y1 = 0;
    top.x2 = 0;
    top.y2 = 0;
    top.draw_valid = 1;
    tick(top);
    top.draw_valid = 0;
    if (!top.draw_error || top.draw_done || top.draw_ready || top.covered_count != 0) {
        throw std::runtime_error("degenerate triangle bypassed attachment validation");
    }
    compare_image(top, memory);
    top.reset = 1;
    tick(top);
    top.reset = 0;
    top.eval();
    compare_draw(
        top, memory, rast, tex, rop, Triangle{{{0, 0}, {128, 0}, {0, 128}}}, .125f, 0xff224466);
    compare_draw(top,
        memory,
        rast,
        tex,
        rop,
        Triangle{{{0, 0}, {128, 0}, {0, 128}}},
        .0625f,
        0x801122ff,
        true);
    compare_draw(top,
        memory,
        rast,
        tex,
        rop,
        Triangle{{{0, 0}, {128, 0}, {0, 128}}},
        .75f,
        0x7fbb9977,
        true);
    compare_draw(top,
        memory,
        rast,
        tex,
        rop,
        Triangle{{{0, 0}, {128, 0}, {0, 128}}},
        0.f,
        0x80a0b0c0,
        true,
        true,
        {0, 65536, 32768});
}
}

// 报告平面图元的Verilator整段像素差分结果。
int main() {
    try {
        run_test();
        std::cout << "flat raster-to-ROP draw compare: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
