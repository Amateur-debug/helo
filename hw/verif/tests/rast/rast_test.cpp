#include "rast_test.h"
#include "dut/rast_dut.h"
#include "tests/common/graphics_checks.h"
#include <algorithm>
#include <limits>
#include <map>
#include <random>

namespace helo::verif {
// 构造具有默认有效透视因子的屏幕顶点。
static helo::ScreenVertex screen_vertex(float x, float y, float z = 0) {
    helo::ScreenVertex result;
    result.m_position = {x, y, z};
    return result;
}

// 用固定种子的二进制精确矩形验证共享边和外边界。
static void check_random_shared_edges(RastDut& dut) {
    std::mt19937 random(0x48454c4fU);
    for (unsigned trial = 0; trial < 64; ++trial) {
        try {
            const float left = float(int(random() % 256) - 64) / 16;
            const float top = float(int(random() % 256) - 64) / 16;
            const float right = left + float(random() % 256 + 1) / 16;
            const float bottom = top + float(random() % 256 + 1) / 16;
            const auto a = screen_vertex(left, top), b = screen_vertex(right, top);
            const auto c = screen_vertex(right, bottom), d = screen_vertex(left, bottom);
            const helo::ScreenTriangle triangles[2] = {
                trial % 2 ? helo::ScreenTriangle{a, b, d} : helo::ScreenTriangle{a, b, c},
                trial % 2 ? helo::ScreenTriangle{b, c, d} : helo::ScreenTriangle{a, c, d}};
            std::array<unsigned, 32 * 32> coverage{};
            for (auto triangle : triangles) {
                if (trial % 3 == 0) {
                    std::swap(triangle[0], triangle[2]);
                }
                const auto expected = dut.rasterize(triangle, 32, 32);
                for (const auto& f : expected) {
                    ++coverage[f.m_y * 32 + f.m_x];
                }
            }
            for (unsigned y = 0; y < 32; ++y) {
                for (unsigned x = 0; x < 32; ++x) {
                    const bool inside =
                        x + .5 >= left && x + .5 < right && y + .5 >= top && y + .5 < bottom;
                    graphics_check(coverage[y * 32 + x] == unsigned(inside),
                        "random rectangle has crack, overlap or wrong boundary");
                }
            }

        } catch (const std::exception& error) {
            throw std::runtime_error(
                "seed=0x48454c4f trial=" + std::to_string(trial) + ": " + error.what());
        }
    }
}

// 以解析例验证非网格顶点落点、共享边归属及Q16/UNORM8正负半值舍入。
static void check_fixed_precision(RastDut& dut) {
    const auto mode = helo::RasterPrecision::Q4_Q16_V1;
    const auto a = screen_vertex(.52f, .52f), b = screen_vertex(2.52f, .52f);
    const auto c = screen_vertex(2.52f, 2.52f), d = screen_vertex(.52f, 2.52f);
    std::array<unsigned, 9> hits{};
    for (const helo::ScreenTriangle& tri :
        {helo::ScreenTriangle{a, b, c}, helo::ScreenTriangle{d, c, a}}) {
        for (const auto& f : dut.rasterize(tri, 3, 3, mode)) {
            ++hits[3 * f.m_y + f.m_x];
        }
    }
    graphics_check(hits == std::array<unsigned, 9>{1, 1, 0, 1, 1, 0, 0, 0, 0},
        "off-grid quad snapped to half-pixel boundaries without cracks or duplicates");
    helo::ScreenTriangle tri{screen_vertex(0, 0), screen_vertex(2, 0), screen_vertex(0, 2)};
    for (unsigned i = 1; i < 3; ++i) {
        tri[i].m_position[2] = 1.f / 65536;
        tri[i].m_uv = {-1.f / 65536, 1.f / 65536};
        tri[i].m_color[0] = 1.f / 255;
    }
    tri[0].m_color[0] = 0;
    const auto first = dut.rasterize(tri, 1, 1, mode);
    graphics_check(first.size() == 1 && first[0].m_depth == 1.f / 65536 &&
                       first[0].m_q16_uv == std::array<std::int32_t, 2>{-1, 1} &&
                       first[0].m_color[0] == 1.f / 255,
        "Q16 and UNORM8 half ties must round away from zero");
    for (auto& v : tri) {
        v.m_position[2] = .5f / 65536;
        v.m_uv = {-1e10f, 1e10f};
        v.m_color = {-1, 2, .5f, 1};
    }
    const auto saturated = dut.rasterize(tri, 1, 1, mode);
    graphics_check(saturated.size() == 1 && saturated[0].m_depth == 1.f / 65536 &&
                       saturated[0].m_q16_uv == std::array<std::int32_t, 2>{INT32_MIN, INT32_MAX} &&
                       saturated[0].m_color == helo::Color{0, 1, 128.f / 255, 1},
        "fixed vertex inputs must saturate and round at the raster boundary");
}

// 检查共享边覆盖、裁界、退化和独立透视插值公式。
void rast_test() {
    auto dut = make_rast_dut();
    check_random_shared_edges(*dut);
    check_fixed_precision(*dut);
    const auto a = screen_vertex(0, 0), b = screen_vertex(4, 0), c = screen_vertex(4, 4),
               d = screen_vertex(0, 4);
    std::map<std::pair<unsigned, unsigned>, unsigned> coverage;
    const helo::ScreenTriangle lower{a, b, c}, upper{a, c, d};
    for (const auto& triangle : {lower, upper}) {
        for (const auto& f : dut->rasterize(triangle, 4, 4)) {
            ++coverage[{f.m_x, f.m_y}];
        }
    }
    graphics_check(coverage.size() == 16, "two triangles cover full 4x4 square");
    for (unsigned y = 0; y < 4; ++y) {
        for (unsigned x = 0; x < 4; ++x) {
            graphics_check(
                coverage[{x, y}] == 1, "shared diagonal assigned exactly once by top-left rule");
        }
    }
    const auto original = dut->rasterize(lower, 4, 4);
    const auto reverse = dut->rasterize({c, b, a}, 4, 4);
    graphics_check(original.size() == reverse.size(), "winding-independent raster coverage");
    for (std::size_t i = 0; i < original.size(); ++i) {
        graphics_check(original[i].m_x == reverse[i].m_x && original[i].m_y == reverse[i].m_y,
            "winding-independent pixel locations");
    }
    graphics_check(
        dut->rasterize({a, a, b}, 4, 4).empty(), "duplicate vertices produce no fragments");
    graphics_check(dut->rasterize({a, screen_vertex(1, 1), c}, 4, 4).empty(),
        "collinear triangle produces no fragments");
    graphics_check(dut->rasterize(lower, 0, 4).empty(), "zero-width target produces no fragments");
    graphics_check(
        dut->rasterize({screen_vertex(-5, -5), screen_vertex(-3, -5), screen_vertex(-4, -3)}, 4, 4)
            .empty(),
        "offscreen triangle rejected");
    const auto clipped =
        dut->rasterize({screen_vertex(-8, -8), screen_vertex(20, -8), screen_vertex(-8, 20)}, 3, 2);
    graphics_check(clipped.size() == 6, "raster bounding box clips to small target");
    helo::ScreenTriangle triangle{
        screen_vertex(0, 0, .1f), screen_vertex(4, 0, .5f), screen_vertex(0, 4, .9f)};
    triangle[0].m_inv_w = 1;
    triangle[1].m_inv_w = .5f;
    triangle[2].m_inv_w = .25f;
    triangle[0].m_color = {1, 0, 0, 1};
    triangle[1].m_color = {0, 1, 0, 1};
    triangle[2].m_color = {0, 0, 1, 1};
    triangle[0].m_uv = {0, 0};
    triangle[1].m_uv = {1, 0};
    triangle[2].m_uv = {0, 1};
    const auto value = dut->interpolate(triangle, {.25, .25, .5});
    graphics_near(value.m_depth, .6f, "window depth interpolates linearly without reciprocal W");
    graphics_near(value.m_color[0], .5f, "perspective red");
    graphics_near(value.m_color[1], .25f, "perspective green");
    graphics_near(value.m_color[2], .25f, "perspective blue");
    graphics_near(value.m_uv[0], .25f, "perspective U");
    graphics_near(value.m_uv[1], .25f, "perspective V");
    const auto fragments = dut->rasterize(triangle, 4, 4);
    bool saw_origin = false;
    for (const auto& f : fragments) {
        if (f.m_x == 0 && f.m_y == 0) {
            saw_origin = true;
            graphics_near(f.m_depth, .25f, "pixel-center barycentric depth");
            graphics_near(f.m_uv[0], 2.f / 27.f, "pixel-center perspective U");
            graphics_near(f.m_uv[1], 1.f / 27.f, "pixel-center perspective V");
        }
    }
    graphics_check(saw_origin, "triangle covers origin pixel center");
    triangle[0].m_position[0] = std::numeric_limits<float>::quiet_NaN();
    graphics_reject([&] { dut->rasterize(triangle, 4, 4); }, "nonfinite screen position rejected");
}
}
