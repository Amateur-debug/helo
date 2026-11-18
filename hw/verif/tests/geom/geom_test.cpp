#include "geom_test.h"
#include "dut/geom_dut.h"
#include "tests/common/graphics_checks.h"
#include <cstring>
#include <limits>
#include <random>

namespace helo::verif {
// 构造具有位置相关属性的裁剪空间顶点以校验属性连续性。
static helo::Vertex vertex(float x, float y, float z = 0, float w = 1) {
    helo::Vertex result;
    result.m_position = {x, y, z, w};
    result.m_color = {x, y, z, 1};
    result.m_uv = {x, y};
    result.m_varyings[15] = 2 * x - y + z;
    return result;
}

// 在多个齐次尺度上检查裁剪约束，覆盖极小正W而不改变投影几何。
static void check_scaled_clipping(GeomDut& dut) {
    std::mt19937 random(0x434c4950U);
    for (unsigned trial = 0; trial < 96; ++trial) {
        try {
            const float scale = std::ldexp(1.0f, -int(random() % 101));
            const float outside = 1.25f + float(random() % 32) / 16;
            helo::Triangle triangle{vertex(-.5f, -.5f), vertex(outside, -.5f), vertex(0, .75f)};
            for (auto& v : triangle) {
                for (auto& coordinate : v.m_position) {
                    coordinate *= scale;
                }
            }
            const auto clipped = dut.clip(triangle);
            graphics_check(
                !clipped.empty(), "small positive W incorrectly discarded visible geometry");
            for (const auto& part : clipped) {
                for (const auto& v : part) {
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        graphics_check(std::abs(double(v.m_position[axis])) <=
                                           double(v.m_position[3]) * (1 + 1e-6),
                            "scaled clip plane violated");
                    }
                    const auto screen = dut.viewport(v, helo::Viewport{0, 0, 32, 32, 0, 1});
                    graphics_check(std::isfinite(screen.m_inv_w) && screen.m_inv_w > 0,
                        "small W reciprocal invalid");
                    graphics_check(screen.m_position[0] >= 0 && screen.m_position[0] <= 32 &&
                                       screen.m_position[1] >= 0 && screen.m_position[1] <= 32,
                        "clipped viewport outside target");
                }
            }

        } catch (const std::exception& error) {
            throw std::runtime_error(
                "seed=0x434c4950 trial=" + std::to_string(trial) + ": " + error.what());
        }
    }
    const auto tiny = std::numeric_limits<float>::denorm_min();
    graphics_reject([&] { dut.viewport(vertex(0, 0, 0, tiny), helo::Viewport{}); },
        "unrepresentable reciprocal W must reject");
}

// 用独立几何关系检查装配、六平面裁剪、视口与背面剔除。
void geom_test() {
    auto dut = make_geom_dut();
    check_scaled_clipping(*dut);
    const std::vector<helo::Vertex> vertices{vertex(-.5f, -.5f), vertex(.5f, -.5f), vertex(0, .5f)};
    graphics_check(dut->assemble(vertices).size() == 1, "triangle list assembly");
    const auto indexed = dut->assemble(vertices, {2, 1, 0});
    graphics_near(indexed[0][0].m_position[1], .5f, "index ordering");
    graphics_reject([&] { dut->assemble(vertices, {0, 1}); }, "incomplete index list");
    graphics_reject([&] { dut->assemble(vertices, {0, 1, 3}); }, "index out of range");
    graphics_reject([&] { dut->assemble({vertices[0]}); }, "incomplete vertex list");
    for (unsigned axis = 0; axis < 3; ++axis) {
        for (float sign : {-1.f, 1.f}) {
            helo::Triangle triangle{vertex(-.2f, -.2f), vertex(.2f, -.2f), vertex(0, .4f)};
            triangle[2].m_position[axis] = sign * 2;
            triangle[2].m_color[axis] = sign * 2;
            if (axis < 2) {
                triangle[2].m_uv[axis] = sign * 2;
            }
            for (auto& v : triangle) {
                v.m_varyings[15] = 2 * v.m_position[0] - v.m_position[1] + v.m_position[2];
            }
            const auto clipped = dut->clip(triangle);
            graphics_check(clipped.size() == 2, "single outside vertex creates quadrilateral");
            bool touched_plane = false;
            for (const auto& part : clipped) {
                for (const auto& v : part) {
                    for (unsigned a = 0; a < 3; ++a) {
                        graphics_check(std::abs(v.m_position[a]) <= v.m_position[3] + 1e-5f,
                            "all clip planes satisfied");
                    }
                    graphics_near(v.m_varyings[15],
                        2 * v.m_position[0] - v.m_position[1] + v.m_position[2],
                        "clip interpolates custom varying on all planes");
                    graphics_near(v.m_color[axis], v.m_position[axis], "clip interpolates color");
                    if (axis < 2) {
                        graphics_near(v.m_uv[axis], v.m_position[axis], "clip interpolates UV");
                    }
                    touched_plane |= std::abs(v.m_position[axis] - sign) < 1e-5f;
                }
            }
            graphics_check(touched_plane, "clip produces boundary intersection");
            for (auto& v : triangle) {
                v.m_position[axis] = sign * 2;
            }
            graphics_check(dut->clip(triangle).empty(), "fully outside plane rejected");
        }
    }
    helo::Viewport vp{3, 7, 20, 10, .2f, .8f};
    const auto screen = dut->viewport(vertex(1, -1, 1, 2), vp);
    graphics_near(screen.m_position[0], 18, "viewport X and offset");
    graphics_near(screen.m_position[1], 14.5f, "viewport Y inversion");
    graphics_near(screen.m_position[2], .65f, "viewport depth range");
    graphics_near(screen.m_inv_w, .5f, "reciprocal W");
    graphics_reject([&] { dut->viewport(vertex(0, 0, 0, 0), vp); }, "zero W rejected");
    auto bad_vp = vp;
    bad_vp.m_width = -1;
    graphics_reject([&] { dut->viewport(vertices[0], bad_vp); }, "invalid viewport rejected");
    auto invalid = vertices[0];
    invalid.m_uv[0] = std::numeric_limits<float>::infinity();
    graphics_reject(
        [&] { dut->clip({invalid, vertices[1], vertices[2]}); }, "nonfinite attributes rejected");
    helo::GeometryState state;
    state.m_viewport = vp;
    state.m_cull_mode = helo::CullMode::BACK;
    graphics_check(dut->geometry(vertices, {}, state).size() == 1, "NDC CCW front face retained");
    graphics_check(dut->geometry(vertices, {2, 1, 0}, state).empty(), "NDC CW back face culled");
    state.m_cull_mode = helo::CullMode::FRONT;
    graphics_check(dut->geometry(vertices, {}, state).empty(), "front face culled");
    state.m_cull_mode = helo::CullMode::NONE;
    graphics_check(
        dut->geometry({vertex(0, 0), vertex(.1f, .1f), vertex(.2f, .2f)}, {}, state).empty(),
        "collinear triangle culled");
    const float payload[10] = {.1f, .2f, .3f, 1, .4f, .5f, .6f, .7f, .8f, .9f};
    for (unsigned i = 0; i < 10; ++i) {
        std::uint32_t bits;
        std::memcpy(&bits, &payload[i], sizeof(bits));
        dut->write_memory(64 + 4 * i, bits);
    }
    const auto fetched = dut->fetch(64, 1, 48);
    graphics_near(fetched[0].m_position[2], .3f, "vertex fetch position layout");
    graphics_near(fetched[0].m_color[3], .7f, "vertex fetch color layout");
    graphics_near(fetched[0].m_uv[1], .9f, "vertex fetch UV layout");
    dut->write_memory(128, 42);
    graphics_check(dut->fetch_indices(128, 1)[0] == 42, "index fetch layout");
    graphics_reject([&] { dut->fetch(64, 1, 36); }, "undersized vertex stride rejected");
    graphics_reject([&] { dut->fetch(65520, 1); }, "vertex fetch bounds checked");
}
}
