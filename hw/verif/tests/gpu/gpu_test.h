#pragma once
#include "dut/gpu_dut.h"
#include "tests/shader/shader_test.h"
#include <string>

namespace helo::verif {
void gpu_test();
void gpu_render_test(const std::string& output_path);
void upload_vertices(GpuDut& dut, std::uint32_t address, const std::vector<helo::Vertex>& vertices);
helo::FramebufferDesc test_framebuffer(unsigned width = 32, unsigned height = 32);
helo::DrawCommand test_draw(const helo::FramebufferDesc& framebuffer);
void complete(GpuDut& dut);
}
