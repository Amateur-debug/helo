#include "gpu_test.h"

namespace helo::verif {
// 从真实命令包验证裁切、逐样本深度、两路着色输出、线性混合及读回解析。
void raster_outputs_test() {
    auto dut = make_gpu_dut();
    auto fb = test_framebuffer(2, 2);
    fb.m_samples = 4;
    fb.m_srgb = true;
    fb.m_has_color1 = true;
    fb.m_color1_address = 0x500000;
    fb.m_has_stencil = true;
    fb.m_stencil_address = 0x600000;
    helo::ClearCommand clear;
    clear.m_framebuffer = fb;
    clear.m_color = {};
    auto draw = test_draw(fb);
    draw.m_indexed = false;
    draw.m_vertex_count = 3;
    draw.m_precision = helo::RasterPrecision::Q4_Q16_V1;
    constexpr std::uint32_t STATE = 0x3000;
    draw.m_raster_state_address = STATE;
    const std::array<unsigned, 9> state{1, 1, 0, 1, 1, 1, 1, 1, 1};
    for (unsigned i = 0; i < state.size(); ++i) {
        dut->write(STATE + 4 * i, 4, state[i]);
    }
    upload_vertices(
        *dut, draw.m_vertex_address, {{{-1, 1, 0, 1}}, {{1, 1, 0, 1}}, {{-1, -1, 0, 1}}});
    draw.m_fragment_shader = {true, 0x200, 0, 40, 100};
    // 单独输出采样位置，样本号非零时丢弃，便于确认样本寄存器与坐标。
    dut->upload_program(0x200,
        {upper(1, 0, 0x3f000),
            imm(2, 2, 0, 0, 0),
            imm(2, 3, 0, 0, 0),
            imm(2, 32, 0, 12, 0),
            imm(2, 33, 0, 11, 0),
            imm(2, 35, 0, 4, 0),
            imm(2, 14, 0, 36, 0),
            EXIT});
    draw.m_rop.m_depth_stencil.m_stencil_test = true;
    draw.m_rop.m_depth_stencil.m_pass = helo::StencilOp::REPLACE;
    draw.m_rop.m_depth_stencil.m_stencil_reference = 9;
    dut->submit_commands({clear, draw});
    complete(*dut);
    require(dut->counters().m_fragments == 1 && dut->counters().m_passed == 1,
        "scissor or diagonal sample coverage");
    for (unsigned i = 0; i < 16; ++i) {
        require(
            dut->read(fb.m_color_address + 4 * i, 4) == (i == 4 ? 0xff0000bcU : 0) &&
                dut->read(fb.m_color1_address + 4 * i, 4) == (i == 4 ? 0xff00ff89U : 0) &&
                dut->read(fb.m_depth_address + 4 * i, 4) == (i == 4 ? 0x3f000000U : 0x3f800000U) &&
                dut->read(fb.m_stencil_address + i, 1) == (i == 4 ? 9U : 0),
            "per-sample attachments or sRGB output");
    }
    auto pixels = dut->read_color(fb);
    const auto second = dut->read_color(fb, 1);
    require(pixels[4] == 99 && pixels[7] == 64 && second[5] == 137 && second[7] == 64,
        "resolve must average in linear light");
    // 放开裁切后首像素具有四个覆盖样本，应仅保留编号零。已写入的邻像素由深度测试拒绝。
    dut->write(STATE + 4, 4, 0);
    dut->write(STATE + 12, 4, 2);
    dut->submit(draw);
    complete(*dut);
    require(dut->read(fb.m_color_address, 4) == 0xff0000bcU &&
                dut->read(fb.m_color_address + 4, 4) == 0 &&
                dut->read(fb.m_color_address + 8, 4) == 0 &&
                dut->read(fb.m_color_address + 12, 4) == 0,
        "per-sample shader discard");
    // 以ONE/ONE在线性空间累加两个半强度红色，RGB结果应饱和至白亮度。
    draw.m_rop.m_blend = true;
    draw.m_rop.m_depth_stencil.m_depth_compare = helo::CompareOp::EQUAL;
    dut->submit(draw);
    complete(*dut);
    require(dut->read(fb.m_color_address, 4) == 0xff0000ffU, "programmable additive blend");
    require((dut->read(fb.m_color1_address, 4) & 255) == 188,
        "sRGB blend must decode destination before addition");
    dut->write(STATE + 12, 4, 0);
    const auto shaded = dut->counters().m_fragments;
    dut->submit(draw);
    complete(*dut);
    require(dut->counters().m_fragments == shaded, "empty scissor ran fragment shading");
    // 空裁切照常验证状态表；验证失败不得覆盖此前写入的颜色。
    dut->write(STATE + 12, 4, 0);
    dut->write(STATE + 32, 4, 10);
    dut->submit(draw);
    require(dut->run().m_fault.m_code == FaultCode::COMMAND &&
                dut->read(fb.m_color_address, 4) == 0xff0000ffU,
        "invalid blend descriptor");
    dut->reset();
    clear.m_framebuffer.m_color1_address = fb.m_color_address + 60;
    dut->submit(clear);
    require(dut->run().m_fault.m_code == FaultCode::COMMAND &&
                dut->read(fb.m_color_address, 4) == 0xff0000ffU,
        "MSAA attachment overlap before clear");
}
}
