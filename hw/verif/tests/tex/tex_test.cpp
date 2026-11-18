#include "tex_test.h"
#include "dut/tex_dut.h"
#include "tests/common/graphics_checks.h"
#include <limits>

namespace helo::verif {
// 用解析2x2颜色格检查过滤和边界地址计算。
void tex_test() {
    auto dut = make_tex_dut();
    dut->write_memory(0, 0xff0000ff);
    dut->write_memory(4, 0xff00ff00);
    dut->write_memory(8, 0xffff0000);
    dut->write_memory(12, 0xffffffff);
    TextureDesc texture;
    const helo::Color red{1, 0, 0, 1}, green{0, 1, 0, 1}, blue{0, 0, 1, 1}, white{1, 1, 1, 1};
    graphics_check(dut->sample(texture, .25f, .25f) == red, "nearest red texel");
    graphics_check(dut->sample(texture, .75f, .25f) == green, "nearest green texel");
    graphics_check(dut->sample(texture, .25f, .75f) == blue, "nearest blue texel");
    graphics_check(dut->sample(texture, 1, 1) == white, "clamped upper endpoint");
    graphics_check(dut->sample(texture, -100, -100) == red, "clamped negative coordinates");
    texture.m_wrap_u = texture.m_wrap_v = TextureWrap::REPEAT;
    graphics_check(dut->sample(texture, 1, 1) == red, "repeat exact unit endpoint");
    graphics_check(dut->sample(texture, -.25f, -.25f) == white, "repeat negative coordinates");
    graphics_check(dut->sample(texture, -std::numeric_limits<float>::min(), .25f) == green,
        "small negative repeat coordinate crossed the seam");
    texture.m_filter = TextureFilter::BILINEAR;
    const auto center = dut->sample(texture, .5f, .5f);
    for (unsigned channel = 0; channel < 3; ++channel) {
        graphics_near(center[channel], .5f, "four texel center average");
    }
    graphics_near(center[3], 1, "bilinear alpha");
    const auto seam = dut->sample(texture, 0, .25f);
    graphics_near(seam[0], .5f, "repeat seam wraps left neighbor");
    graphics_near(seam[1], .5f, "repeat seam wraps right neighbor");
    graphics_near(seam[2], 0, "repeat seam maintains row");
    texture.m_wrap_u = texture.m_wrap_v = TextureWrap::CLAMP;
    graphics_check(dut->sample(texture, 0, 0) == red, "bilinear clamp corner");
    const auto quarter = dut->sample(texture, .375f, .375f);
    graphics_near(quarter[0], .625f, "bilinear asymmetric red");
    graphics_near(quarter[1], .25f, "bilinear asymmetric green");
    graphics_near(quarter[2], .25f, "bilinear asymmetric blue");
    auto one = texture;
    one.m_width = one.m_height = 1;
    graphics_check(dut->sample(one, .3f, .9f) == red, "one texel bilinear texture");
    // 非二次幂的3x1基础级接1x1末级，边界LOD夹取与层间混合各有独立期望。
    auto mip = texture;
    mip.m_width = 3;
    mip.m_height = 1;
    graphics_check(dut->sample(mip, 0, 0, 2, -10) == red, "negative LOD selects base");
    graphics_check(dut->sample(mip, 0, 0, 2, 5) == white, "high LOD selects last level");
    graphics_check(dut->sample(mip, 0, 0, 2, .25f) == helo::Color{1, .25f, .25f, 1},
        "fractional LOD weights adjacent levels");
    graphics_reject([&] { dut->sample(mip, 0, 0, 0); }, "empty mip chain accepted");
    graphics_reject([&] { dut->sample(mip, 0, 0, 3); }, "mip chain extends beyond 1x1");
    graphics_reject([&] { dut->sample(mip, 0, 0, 2, std::numeric_limits<float>::infinity()); },
        "nonfinite LOD accepted");
    mip.m_address = 65524;
    graphics_reject([&] { dut->sample(mip, 0, 0, 2, 0); }, "last mip range was not validated");
    auto invalid = texture;
    invalid.m_width = 0;
    graphics_reject([&] { dut->sample(invalid, 0, 0); }, "empty texture rejected");
    invalid = texture;
    invalid.m_address = 65532;
    graphics_reject([&] { dut->sample(invalid, 0, 0); }, "full texture range validated");
    invalid = texture;
    invalid.m_address = 1;
    graphics_reject([&] { dut->sample(invalid, 0, 0); }, "misaligned texture rejected");
    invalid = texture;
    invalid.m_width = invalid.m_height = 0xffffffffu;
    graphics_reject([&] { dut->sample(invalid, 0, 0); }, "texture extent overflow rejected");
    graphics_reject([&] { dut->sample(texture, std::numeric_limits<float>::infinity(), 0); },
        "nonfinite coordinates rejected");
}
}
