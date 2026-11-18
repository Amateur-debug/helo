#include "rast/rast.h"
#include "rop/rop.h"
#include "shader/shader.h"
#include "tests/common/graphics_checks.h"
#include "tests/shader/shader_test.h"
#include "tex/tex.h"
#include <iostream>
#include <limits>

namespace helo::verif {
// 检查C++游标的批次上限、耗尽、属性一致性及不同游标的隔离。
static void check_raster_cursor() {
    func::Rast raster;
    ScreenTriangle triangle;
    triangle[0].m_position = {0, 0, .1f};
    triangle[1].m_position = {32, 0, .5f};
    triangle[2].m_position = {0, 32, .9f};
    triangle[0].m_color = {1, 0, 0, 1};
    triangle[1].m_color = {0, 1, 0, 1};
    triangle[2].m_color = {0, 0, 1, 1};
    triangle[1].m_inv_w = .5f;
    triangle[2].m_inv_w = .25f;
    const auto expected = raster.rasterize(triangle, 32, 32);
    for (const std::size_t capacity : {1, 3, 32, 128, 1024}) {
        auto cursor = raster.begin(triangle, 32, 32);
        auto independent = raster.begin(triangle, 32, 32);
        std::vector<Fragment> batch;
        std::size_t offset = 0;
        while (raster.next(cursor, batch, capacity)) {
            graphics_check(batch.size() <= capacity, "cursor exceeded batch capacity");
            for (const auto& fragment : batch) {
                graphics_check(offset < expected.size(), "cursor produced extra fragments");
                const auto& reference = expected[offset++];
                graphics_check(fragment.m_x == reference.m_x && fragment.m_y == reference.m_y &&
                                   fragment.m_depth == reference.m_depth &&
                                   fragment.m_color == reference.m_color &&
                                   fragment.m_uv == reference.m_uv,
                    "cursor changed output order or attributes");
            }
        }
        graphics_check(offset == expected.size(), "cursor lost fragments");
        graphics_check(
            !raster.next(cursor, batch, capacity) && batch.empty(), "exhausted cursor revived");
        graphics_check(raster.next(independent, batch, 1) &&
                           batch.front().m_x == expected.front().m_x &&
                           batch.front().m_y == expected.front().m_y,
            "one cursor changed another cursor");
        graphics_reject(
            [&] { raster.next(independent, batch, 0); }, "zero batch capacity accepted");
    }
}

// 直接启动32位任务的最后一个组，验证全局编号和被拒绝的启动不会破坏该组。
static void check_compute_tail_launch() {
    constexpr std::uint32_t TOTAL = 0xffffffffu;
    constexpr std::uint32_t GROUP = 37;
    constexpr std::uint32_t TAIL = TOTAL % GROUP;
    constexpr std::uint32_t BASE = TOTAL - TAIL;
    func::Mem memory(128);
    memory.load_instructions(0,
        {imm(12, 1, 8, 0, 0x12),
            imm(12, 2, 8, 0, 0x11),
            imm(12, 3, 8, 0, 0x13),
            imm(12, 4, 8, 0, 0x17),
            imm(12, 5, 8, 0, 0x19),
            EXIT});
    ShaderConfig config;
    config.m_max_warps = 2;
    func::Shader shader(memory, config);
    shader.launch(0, TAIL, 0, 6, {BASE, TOTAL, GROUP, 0});
    require(shader.step(), "tail launch did not begin");
    graphics_reject([&] { shader.launch(0, 1, 0, 6, {0, 1, 65, 0}); },
        "direct launch accepted an oversized group for a small tail");
    graphics_reject([&] { shader.launch(0, 0, 0, 6, {0, TOTAL, 0, 0}); },
        "direct launch accepted an empty batch for a nonempty task");
    require(shader.run().completed(), "rejected launch destroyed pending tail");
    for (unsigned lane = 0; lane < TAIL; ++lane) {
        const auto& registers = shader.registers();
        require(registers.read_gpr(0, lane, 1) == BASE + lane &&
                    registers.read_gpr(0, lane, 2) == (BASE / GROUP) * 2 &&
                    registers.read_gpr(0, lane, 3) == TOTAL &&
                    registers.read_gpr(0, lane, 4) == BASE / GROUP &&
                    registers.read_gpr(0, lane, 5) == TAIL,
            "uint32 maximum dispatch lost tail CSR state");
    }
}

// 检查采样句柄保存布局快照但实时读取纹素，并继续拒绝非法坐标。
static void check_texture_handle() {
    func::Mem memory(64);
    func::Tex texture(memory);
    TextureDesc desc;
    desc.m_width = desc.m_height = 1;
    memory.write(0, 4, 0xff0000ffU);
    const auto sampler = texture.prepare(desc);
    desc.m_address = 63;
    graphics_check(
        sampler.sample(0, 0) == Color{1, 0, 0, 1}, "sampler did not snapshot descriptor");
    memory.write(0, 4, 0xff00ff00U);
    graphics_check(sampler.sample(0, 0) == Color{0, 1, 0, 1}, "sampler cached stale data");
    graphics_reject([&] { sampler.sample(std::numeric_limits<float>::quiet_NaN(), 0); },
        "sampler accepted NaN");
    graphics_reject([&] { texture.prepare(desc); }, "invalid sampler layout accepted");
    graphics_check(sampler.sample(0, 0) == Color{0, 1, 0, 1},
        "failed preparation invalidated existing handle");
}

// 检查附件句柄的布局快照、逐片元深度可见性和动态状态验证。
static void check_target_handle() {
    func::Mem memory(64);
    func::Rop rop(memory);
    FramebufferDesc desc;
    desc.m_width = desc.m_height = 1;
    desc.m_depth_address = 16;
    rop.clear(desc, {0, 0, 1, 1});
    const auto target = rop.prepare(desc);
    desc.m_color_address = 63;
    Fragment fragment;
    fragment.m_depth = .25f;
    fragment.m_color = {1, 0, 0, 1};
    RopState state;
    graphics_check(target.write(state, fragment) && memory.read(0, 4) == 0xff0000ffU,
        "target did not snapshot attachment layout");
    fragment.m_depth = .5f;
    graphics_check(!target.write(state, fragment), "target ignored earlier depth write");
    fragment.m_depth = std::numeric_limits<float>::quiet_NaN();
    graphics_check(!target.write(state, fragment), "target accepted invalid fragment depth");
    desc.m_color_address = 0;
    desc.m_has_depth = false;
    const auto color_only = rop.prepare(desc);
    fragment.m_depth = .5f;
    graphics_reject(
        [&] { color_only.write(state, fragment); }, "target failed to validate dynamic state");
    graphics_check(memory.read(0, 4) == 0xff0000ffU, "rejected target write changed memory");
}
}

// 单独运行功能模型内部资源检查，不要求RTL或性能模型实现C++句柄。
int main() {
    try {
        helo::verif::check_raster_cursor();
        helo::verif::check_compute_tail_launch();
        helo::verif::check_texture_handle();
        helo::verif::check_target_handle();
        std::cout << "PASS func resources\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
