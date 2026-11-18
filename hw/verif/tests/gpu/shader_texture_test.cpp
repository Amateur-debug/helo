#include "gpu_test.h"

namespace helo::verif {
namespace {
// 独立编码显式LOD、绑定槽及返回通道的三源纹理指令。
std::uint64_t tex_lod(
    unsigned slot, unsigned channel, unsigned rd, unsigned u, unsigned v, unsigned lod) {
    return reg(0x0d, 4 * slot + channel, rd, u, v) | (std::uint64_t(lod) << 32);
}

// 用顶点流的自定义分量在两个像素上产生不同UV，验证真实图形阶段间的数据传递。
void check_varying_coordinates(GpuDut& dut, helo::DrawCommand draw) {
    draw.m_framebuffer = test_framebuffer(4, 4);
    draw.m_geometry.m_viewport.m_width = draw.m_geometry.m_viewport.m_height = 4;
    draw.m_vertex_layout_address = 0x3800;
    draw.m_varying_count = 1;
    draw.m_vertex_shader.m_register_count = draw.m_fragment_shader.m_register_count = 49;
    const std::array<std::uint32_t, 4> offsets{0, 16, 0xffffffffU, 32};
    for (unsigned i = 0; i < offsets.size(); ++i) {
        dut.write(draw.m_vertex_layout_address + 4 * i, 4, offsets[i]);
    }
    // 原顶点记录偏移32处作为自定义分量，右上顶点为1，其余为0。
    dut.write(draw.m_vertex_address + draw.m_vertex_stride + 32, 4, 0x3f800000U);
    dut.upload_program(
        0x200, {upper(16, 0, 0x3f800), fp(1, 18, 16, 48), tex_lod(1, 1, 2, 18, 0, 0), EXIT});
    dut.submit(draw);
    complete(dut);
    // (0,0)、(2,0)的插值分量为1/8、5/8，1-分量分别选择绿色和黑色。
    require(dut.read(draw.m_framebuffer.m_color_address, 4) == 0x0000ff3f &&
                dut.read(draw.m_framebuffer.m_color_address + 8, 4) == 0x0000003f,
        "interpolated custom varying did not select distinct texels from the second binding");
}
}

// 在真实图形命令上检查VS/FS共用绑定表、计算坐标、跨层过滤及故障隔离。
void shader_texture_test() {
    auto dut = make_gpu_dut();
    constexpr std::uint32_t TABLE = 0x3000;
    constexpr std::uint32_t FIRST = 0x3400;
    constexpr std::uint32_t SECOND = 0x3500;
    const std::array<std::uint32_t, 14> descriptors{
        FIRST, 1, 1, 0, 0, 0, 1, SECOND, 2, 1, 0, 0, 0, 2};
    for (unsigned i = 0; i < descriptors.size(); ++i) {
        dut->write(TABLE + i * 4, 4, descriptors[i]);
    }
    dut->write(FIRST, 4, 0xff00003f);
    dut->write(SECOND, 4, 0xff000000);
    dut->write(SECOND + 4, 4, 0xff00ff00);
    dut->write(SECOND + 8, 4, 0xffff0000);
    auto draw = test_draw(test_framebuffer(2, 2));
    draw.m_indexed = false;
    draw.m_vertex_count = 3;
    // 三个顶点的输入U均为0。FS用1-U选择第二个纹素，再混合蓝色的1x1级。
    upload_vertices(*dut,
        draw.m_vertex_address,
        {{{-1, 1, 0, 1}, {}, {}}, {{1, 1, 0, 1}, {}, {}}, {{-1, -1, 0, 1}, {}, {}}});
    draw.m_rop.m_depth_stencil.m_depth_test = false;
    draw.m_rop.m_depth_stencil.m_depth_write = false;
    draw.m_texture_table_address = TABLE;
    draw.m_texture_binding_count = 2;
    draw.m_vertex_shader = {true, 0x100, 0, 16, 100};
    draw.m_fragment_shader = {true, 0x200, 0, 20, 100};
    dut->upload_program(0x100, {tex_lod(0, 0, 5, 9, 10, 0), EXIT});
    dut->upload_program(0x200,
        {upper(16, 0, 0x3f800),
            upper(17, 0, 0x3f000),
            fp(1, 18, 16, 9),
            tex_lod(1, 1, 2, 18, 10, 17),
            tex_lod(1, 2, 3, 18, 10, 17),
            tex_lod(1, 3, 4, 18, 10, 17),
            EXIT});
    dut->submit(draw);
    complete(*dut);
    require(dut->read(draw.m_framebuffer.m_color_address, 4) == 0xff80803f,
        "vertex and fragment sampling, computed UV or mip interpolation failed");

    // 下一绘制撤销槽1，失败批次禁止向ROP提交颜色。
    draw.m_texture_binding_count = 1;
    dut->write(draw.m_framebuffer.m_color_address, 4, 0x12345678);
    dut->submit(draw);
    const auto fault = dut->run();
    require(fault.m_fault.m_code == FaultCode::SHADER_EXECUTION && fault.m_fault.m_pc == 0x218 &&
                dut->read(draw.m_framebuffer.m_color_address, 4) == 0x12345678,
        "unbound slot was reused or the failing fragment batch reached ROP");

    dut->reset();
    draw.m_texture_binding_count = 2;
    dut->write(TABLE + 52, 4, 3);
    dut->submit(draw);
    require(dut->run().m_fault.m_code == FaultCode::COMMAND && dut->counters().m_vertices == 0,
        "invalid mip descriptor reached the vertex stage");

    dut->reset();
    dut->write(TABLE + 52, 4, 2);
    check_varying_coordinates(*dut, draw);
}
}
