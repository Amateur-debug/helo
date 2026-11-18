#include "rop_test.h"
#include "dut/rop_dut.h"
#include "tests/common/graphics_checks.h"
#include <cstring>
#include <limits>

namespace helo::verif {
// 将附件存储字解释为FP32以检查深度结果。
static float depth_value(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// 检查所有比较/模板操作与真实附件上的深度、混合和故障行为。
void rop_test() {
    auto dut = make_rop_dut();
    const CompareOp comparisons[]{CompareOp::NEVER,
        CompareOp::LESS,
        CompareOp::EQUAL,
        CompareOp::LESS_EQUAL,
        CompareOp::GREATER,
        CompareOp::NOT_EQUAL,
        CompareOp::GREATER_EQUAL,
        CompareOp::ALWAYS};
    const bool less[]{false, true, false, true, false, true, false, true};
    const bool equal[]{false, false, true, true, false, false, true, true};
    const bool greater[]{false, false, false, false, true, true, true, true};
    for (unsigned i = 0; i < 8; ++i) {
        graphics_check(dut->compare(comparisons[i], .2f, .8f) == less[i], "compare less pair");
        graphics_check(dut->compare(comparisons[i], .5f, .5f) == equal[i], "compare equal pair");
        graphics_check(
            dut->compare(comparisons[i], .8f, .2f) == greater[i], "compare greater pair");
    }
    const StencilOp operations[]{StencilOp::KEEP,
        StencilOp::ZERO,
        StencilOp::REPLACE,
        StencilOp::INCREMENT_CLAMP,
        StencilOp::DECREMENT_CLAMP,
        StencilOp::INVERT,
        StencilOp::INCREMENT_WRAP,
        StencilOp::DECREMENT_WRAP};
    const unsigned expected[]{0xa5, 0, 0x3c, 0xa6, 0xa4, 0x5a, 0xa6, 0xa4};
    for (unsigned i = 0; i < 8; ++i) {
        graphics_check(dut->stencil(operations[i], 0xa5, 0x3c, 255) == expected[i],
            "stencil full mask operation");
        graphics_check(dut->stencil(operations[i], 0xa5, 0x3c, 15) == (0xa0 | (expected[i] & 15)),
            "stencil preserves masked bits");
    }
    graphics_check(
        dut->stencil(StencilOp::INCREMENT_CLAMP, 255, 0, 255) == 255, "increment saturation");
    graphics_check(
        dut->stencil(StencilOp::DECREMENT_CLAMP, 0, 0, 255) == 0, "decrement saturation");
    graphics_check(dut->stencil(StencilOp::INCREMENT_WRAP, 255, 0, 255) == 0, "increment wrapping");
    graphics_check(dut->stencil(StencilOp::DECREMENT_WRAP, 0, 0, 255) == 255, "decrement wrapping");
    graphics_check(dut->pack({-1, .5f, 2, 1}) == 0xffff8000u, "RGBA8 clamp round and byte order");
    const auto blend = dut->blend({1, 0, 0, .25f}, {0, 0, 1, .5f});
    graphics_near(blend[0], .25f, "source-over RGB source factor");
    graphics_near(blend[2], .75f, "source-over RGB destination factor");
    graphics_near(blend[3], .625f, "source-over alpha equation");
    FramebufferDesc fb;
    RopState state;
    dut->clear(fb, {0, 0, 1, 1}, 1, 0xa5);
    for (unsigned i = 0; i < 4; ++i) {
        graphics_check(dut->read_memory(1024 + 4 * i) == 0xffff0000u, "clear all colors");
        graphics_near(depth_value(dut->read_memory(2048 + 4 * i)), 1, "clear all depths");
        graphics_check(dut->read_memory(3072 + i, 1) == 0xa5, "clear all stencils");
    }
    helo::Fragment f;
    f.m_x = 1;
    f.m_y = 1;
    f.m_depth = .4f;
    f.m_color = {1, 0, 0, 1};
    graphics_check(dut->write_fragment(fb, state, f), "near fragment accepted");
    graphics_check(dut->read_memory(1036) == 0xff0000ffu, "accepted color written");
    graphics_near(depth_value(dut->read_memory(2060)), .4f, "accepted depth written");
    graphics_check(dut->read_memory(1024) == 0xffff0000u, "neighbor color unchanged");
    f.m_depth = .8f;
    f.m_color = {0, 1, 0, 1};
    graphics_check(!dut->write_fragment(fb, state, f), "far fragment rejected");
    graphics_check(dut->read_memory(1036) == 0xff0000ffu, "rejected fragment leaves color");
    state.m_depth_test = false;
    state.m_depth_write = false;
    state.m_blend = true;
    f.m_color = {0, 0, 1, .5f};
    graphics_check(dut->write_fragment(fb, state, f), "disabled depth test accepts");
    graphics_check(dut->read_memory(1036) == 0xff800080u, "RGBA8 source alpha blend");
    graphics_near(depth_value(dut->read_memory(2060)), .4f, "disabled depth write preserves depth");
    state.m_blend = false;
    state.m_color_write = false;
    state.m_depth_write = true;
    graphics_check(dut->write_fragment(fb, state, f), "depth write independent of depth test");
    graphics_near(depth_value(dut->read_memory(2060)), .8f, "depth write enabled without compare");
    graphics_check(dut->read_memory(1036) == 0xff800080u, "color write mask preserves color");
    state = RopState{};
    state.m_stencil_test = true;
    state.m_stencil_compare = CompareOp::EQUAL;
    state.m_stencil_reference = 0xb5;
    state.m_stencil_read_mask = 15;
    state.m_stencil_write_mask = 15;
    state.m_pass = StencilOp::REPLACE;
    f.m_depth = .2f;
    graphics_check(dut->write_fragment(fb, state, f), "stencil comparison respects read mask");
    graphics_check(dut->read_memory(3075, 1) == 0xa5, "stencil write mask preserves upper bits");
    state.m_stencil_compare = CompareOp::NEVER;
    state.m_stencil_fail = StencilOp::INCREMENT_CLAMP;
    graphics_check(!dut->write_fragment(fb, state, f), "stencil failure rejects fragment");
    graphics_check(dut->read_memory(3075, 1) == 0xa6, "stencil failure operation applied");
    graphics_near(depth_value(dut->read_memory(2060)), .2f, "stencil failure preserves depth");
    state.m_stencil_compare = CompareOp::ALWAYS;
    state.m_depth_fail = StencilOp::DECREMENT_CLAMP;
    f.m_depth = .9f;
    graphics_check(!dut->write_fragment(fb, state, f), "depth failure after stencil pass");
    graphics_check(dut->read_memory(3075, 1) == 0xa5, "depth failure stencil operation applied");
    const auto old_color = dut->read_memory(1024);
    auto invalid = fb;
    invalid.m_depth_address = 2049;
    graphics_reject([&] { dut->clear(invalid, {1, 1, 1, 1}, 1, 0); },
        "misaligned attachment rejected before clear");
    graphics_check(dut->read_memory(1024) == old_color, "invalid clear leaves colors unchanged");
    invalid = fb;
    invalid.m_depth_address = 65532;
    graphics_reject(
        [&] { dut->clear(invalid, {1, 1, 1, 1}, 1, 0); }, "full attachment bounds validated");
    graphics_check(dut->read_memory(1024) == old_color, "out-of-range clear is atomic");
    invalid = fb;
    invalid.m_depth_address = 1028;
    graphics_reject(
        [&] { dut->clear(invalid, {1, 1, 1, 1}, 1, 0); }, "overlapping attachments rejected");
    invalid = fb;
    invalid.m_has_depth = false;
    graphics_reject(
        [&] { dut->write_fragment(invalid, state, f); }, "enabled absent depth rejected");
    invalid = fb;
    invalid.m_has_stencil = false;
    graphics_reject(
        [&] { dut->write_fragment(invalid, state, f); }, "enabled absent stencil rejected");
    std::array<std::uint32_t, 12> snapshot{};
    for (unsigned i = 0; i < 4; ++i) {
        snapshot[i] = dut->read_memory(1024 + 4 * i);
        snapshot[4 + i] = dut->read_memory(2048 + 4 * i);
        snapshot[8 + i] = dut->read_memory(3072 + i, 1);
    }
    for (float depth : {std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity(),
             -std::numeric_limits<float>::infinity(),
             -.1f,
             1.1f}) {
        f.m_depth = depth;
        graphics_check(!dut->write_fragment(fb, state, f), "invalid fragment depth discarded");
    }
    f.m_depth = .1f;
    f.m_x = 2;
    graphics_check(!dut->write_fragment(fb, state, f), "out-of-bounds fragment discarded");
    for (unsigned i = 0; i < 4; ++i) {
        graphics_check(
            dut->read_memory(1024 + 4 * i) == snapshot[i], "discard preserves all colors");
        graphics_check(
            dut->read_memory(2048 + 4 * i) == snapshot[4 + i], "discard preserves all depth");
        graphics_check(
            dut->read_memory(3072 + i, 1) == snapshot[8 + i], "discard preserves all stencil");
    }
}
}
