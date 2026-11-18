#include "Vhelo_rop_memory.h"
#include "mem/mem.h"
#include "rop/rop.h"
#include "verilated.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {
// 保留测试深度的FP32原始位模式，不借用生产ROP内部转换。
std::uint32_t bits(float value) {
    std::uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

// 从内存位模式构造特殊FP32测试值。
float float_from_bits(std::uint32_t value) {
    float result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

// 生成固定种子的比较、模板和片元输入。
std::uint32_t next_random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// 时钟推进后恢复低电平，以保持内存端口读写可观察。
void tick(Vhelo_rop_memory& top) {
    top.clk = 0;
    top.eval();
    top.clk = 1;
    top.eval();
    top.clk = 0;
    top.eval();
}

// 使用主机端口初始化或检查附件，不通过被测ROP写入初值。
void host_write(Vhelo_rop_memory& top,
    helo::func::Mem& memory,
    unsigned address,
    unsigned size,
    std::uint32_t value) {
    top.host_valid = 1;
    top.host_write = 1;
    top.host_addr = address;
    top.host_size = size;
    top.host_wdata = value;
    top.eval();
    if (top.host_error) {
        throw std::runtime_error("host attachment upload failed");
    }
    tick(top);
    top.host_valid = 0;
    memory.write(address, size, value);
}

// 比较整个已配置附件，发现片元间的部分写入或错误像素覆盖。
void compare_memory(Vhelo_rop_memory& top, const helo::func::Mem& memory) {
    top.host_valid = 1;
    top.host_write = 0;
    for (unsigned pixel = 0; pixel < 16; ++pixel) {
        for (const auto address : {256u + 4 * pixel, 512u + 4 * pixel}) {
            top.host_addr = address;
            top.host_size = 4;
            top.eval();
            if (top.host_error || top.host_rdata != memory.read(address, 4)) {
                throw std::runtime_error("ROP color/depth attachment differs from reference");
            }
        }
        top.host_addr = 768 + pixel;
        top.host_size = 1;
        top.eval();
        if (top.host_error || top.host_rdata != memory.read(768 + pixel, 1)) {
            throw std::runtime_error("ROP stencil attachment differs from reference");
        }
    }
    top.host_valid = 0;
    top.eval();
}

// 对一次片元请求比较返回语义与全部附件的持久状态。
void compare_case(Vhelo_rop_memory& top,
    helo::func::Rop& rop,
    const helo::func::Mem& memory,
    const helo::FramebufferDesc& fb,
    const helo::RopState& state,
    const helo::Fragment& fragment,
    std::uint32_t rgba) {
    if (!top.request_ready) {
        throw std::runtime_error("ROP not ready for a new fragment");
    }
    top.width = fb.m_width;
    top.height = fb.m_height;
    top.color_base = fb.m_color_address;
    top.depth_base = fb.m_depth_address;
    top.stencil_base = fb.m_stencil_address;
    top.has_depth = fb.m_has_depth;
    top.has_stencil = fb.m_has_stencil;
    top.depth_test = state.m_depth_stencil.m_depth_test;
    top.depth_write = state.m_depth_stencil.m_depth_write;
    top.depth_compare = static_cast<unsigned>(state.m_depth_stencil.m_depth_compare);
    top.stencil_test = state.m_depth_stencil.m_stencil_test;
    top.stencil_compare = static_cast<unsigned>(state.m_depth_stencil.m_stencil_compare);
    top.stencil_reference = state.m_depth_stencil.m_stencil_reference;
    top.stencil_read_mask = state.m_depth_stencil.m_stencil_read_mask;
    top.stencil_write_mask = state.m_depth_stencil.m_stencil_write_mask;
    top.stencil_fail = static_cast<unsigned>(state.m_depth_stencil.m_stencil_fail);
    top.stencil_depth_fail = static_cast<unsigned>(state.m_depth_stencil.m_depth_fail);
    top.stencil_pass = static_cast<unsigned>(state.m_depth_stencil.m_pass);
    top.color_write = state.m_color_write;
    top.blend = state.m_blend;
    top.fragment_x = fragment.m_x;
    top.fragment_y = fragment.m_y;
    top.fragment_depth = bits(fragment.m_depth);
    top.fragment_rgba = rgba;
    top.request_valid = 1;
    top.eval();
    tick(top);
    top.request_valid = 0;
    bool expected_error = false;
    bool expected_pass = false;
    try {
        expected_pass = rop.write(fb, state, fragment);
    } catch (const std::exception&) {
        expected_error = true;
    }
    bool completed = false;
    for (unsigned cycle = 0; cycle < 16; ++cycle) {
        tick(top);
        if (top.response_valid) {
            completed = true;
            break;
        }
    }
    if (!completed || top.response_error != expected_error || top.response_pass != expected_pass ||
        !top.request_ready) {
        std::cerr << "ROP mismatch at pixel=" << fragment.m_x << ',' << fragment.m_y << " depth=0x"
                  << std::hex << bits(fragment.m_depth) << std::dec
                  << " expected pass/error=" << expected_pass << '/' << expected_error
                  << " actual=" << int(top.response_pass) << '/' << int(top.response_error) << '\n';
        throw std::runtime_error("ROP response differs from functional model");
    }
    compare_memory(top, memory);
}

// 对全部比较/模板操作和固定种子状态组合进行片元级差分。
void run_test() {
    VerilatedContext context;
    Vhelo_rop_memory top(&context);
    helo::func::Mem memory(4096);
    helo::func::Rop rop(memory);
    helo::FramebufferDesc fb;
    fb.m_width = 4;
    fb.m_height = 4;
    fb.m_color_address = 256;
    fb.m_depth_address = 512;
    fb.m_stencil_address = 768;
    fb.m_has_depth = true;
    fb.m_has_stencil = true;
    top.capacity_bytes = 4096;
    top.fetch_valid = 0;
    top.fetch_addr = 0;
    top.host_valid = 0;
    top.request_valid = 0;
    top.reset = 1;
    tick(top);
    top.reset = 0;
    top.eval();
    std::uint32_t seed = 0x524f5031;
    for (unsigned pixel = 0; pixel < 16; ++pixel) {
        host_write(top, memory, 256 + 4 * pixel, 4, next_random(seed));
        host_write(top, memory, 512 + 4 * pixel, 4, bits(.5f));
        host_write(top, memory, 768 + pixel, 1, next_random(seed) & 255);
    }
    helo::Fragment fragment;
    fragment.m_x = 2;
    fragment.m_y = 1;
    fragment.m_depth = .25f;
    helo::RopState state;
    state.m_depth_stencil.m_stencil_test = true;
    for (unsigned comparison = 0; comparison < 8; ++comparison) {
        for (unsigned operation = 0; operation < 8; ++operation) {
            state.m_depth_stencil.m_depth_compare = helo::CompareOp(comparison);
            state.m_depth_stencil.m_stencil_compare = helo::CompareOp((comparison + 3) % 8);
            state.m_depth_stencil.m_stencil_reference = (comparison & 1) ? 0xa5 : 0x5a;
            state.m_depth_stencil.m_stencil_read_mask = 0x0f;
            state.m_depth_stencil.m_stencil_write_mask = 0xf3;
            state.m_depth_stencil.m_stencil_fail = helo::StencilOp(operation);
            state.m_depth_stencil.m_depth_fail = helo::StencilOp((operation + 1) % 8);
            state.m_depth_stencil.m_pass = helo::StencilOp((operation + 2) % 8);
            const std::uint32_t rgba = next_random(seed);
            for (unsigned channel = 0; channel < 4; ++channel) {
                fragment.m_color[channel] = float((rgba >> (8 * channel)) & 255) / 255.0f;
            }
            compare_case(top, rop, memory, fb, state, fragment, rgba);
        }
    }
    for (unsigned trial = 0; trial < 512; ++trial) {
        const auto r = [&]() { return next_random(seed); };
        fragment.m_x = r() % 6;
        fragment.m_y = r() % 6;
        const std::array<float, 6> depths{0.f, .25f, .5f, .75f, 1.f, -.25f};
        fragment.m_depth = depths[r() % depths.size()];
        state.m_depth_stencil.m_depth_test = (r() & 1) != 0;
        state.m_depth_stencil.m_depth_write = (r() & 1) != 0;
        state.m_depth_stencil.m_depth_compare = helo::CompareOp(r() % 8);
        state.m_depth_stencil.m_stencil_test = (r() & 1) != 0;
        state.m_depth_stencil.m_stencil_compare = helo::CompareOp(r() % 8);
        state.m_depth_stencil.m_stencil_reference = r() & 255;
        state.m_depth_stencil.m_stencil_read_mask = r() & 255;
        state.m_depth_stencil.m_stencil_write_mask = r() & 255;
        state.m_depth_stencil.m_stencil_fail = helo::StencilOp(r() % 8);
        state.m_depth_stencil.m_depth_fail = helo::StencilOp(r() % 8);
        state.m_depth_stencil.m_pass = helo::StencilOp(r() % 8);
        state.m_color_write = (r() & 1) != 0;
        state.m_blend = (r() & 1) != 0;
        const auto rgba = r();
        for (unsigned channel = 0; channel < 4; ++channel) {
            fragment.m_color[channel] = float((rgba >> (8 * channel)) & 255) / 255.0f;
        }
        compare_case(top, rop, memory, fb, state, fragment, rgba);
    }
    state = {};
    fragment.m_x = 2;
    fragment.m_y = 1;
    fragment.m_depth = .25f;
    state.m_depth_stencil.m_depth_test = false;
    state.m_depth_stencil.m_depth_write = false;
    state.m_blend = true;
    for (unsigned trial = 0; trial < 1024; ++trial) {
        const auto alpha = trial % 4 == 0 ? 0u : trial % 4 == 1 ? 255u : next_random(seed) & 255u;
        const std::uint32_t rgba = (alpha << 24) | (next_random(seed) & 0xffffffu);
        for (unsigned channel = 0; channel < 4; ++channel) {
            fragment.m_color[channel] = float((rgba >> (8 * channel)) & 255) / 255.0f;
        }
        compare_case(top, rop, memory, fb, state, fragment, rgba);
    }
    state = {};
    fragment.m_x = 1;
    fragment.m_y = 1;
    fragment.m_depth = .5f;
    fragment.m_color = {1, 0, 0, 1};
    const auto malformed = [&]() {
        auto result = fb;
        result.m_depth_address = 258;
        return result;
    }();
    compare_case(top, rop, memory, malformed, state, fragment, 0xff0000ff);
    auto no_depth = fb;
    no_depth.m_has_depth = false;
    compare_case(top, rop, memory, no_depth, state, fragment, 0xff0000ff);
    auto no_stencil = fb;
    no_stencil.m_has_stencil = false;
    state.m_depth_stencil.m_stencil_test = true;
    compare_case(top, rop, memory, no_stencil, state, fragment, 0xff0000ff);
    state = {};
    state.m_blend = true;
    compare_case(top, rop, memory, fb, state, fragment, 0xff0000ff);
    state.m_blend = false;
    fragment.m_depth = -0.f;
    compare_case(top, rop, memory, fb, state, fragment, 0xff0000ff);
    fragment.m_depth = 1.1f;
    compare_case(top, rop, memory, fb, state, fragment, 0xff0000ff);
    fragment.m_depth = float_from_bits(0x7fc00000);
    compare_case(top, rop, memory, fb, state, fragment, 0xff0000ff);
    fragment.m_depth = .25f;
    host_write(top, memory, 512 + 4 * 5, 4, 0x7fc00000);
    state.m_depth_stencil.m_depth_compare = helo::CompareOp::NOT_EQUAL;
    compare_case(top, rop, memory, fb, state, fragment, 0xff0000ff);
    state.m_depth_stencil.m_depth_compare = helo::CompareOp::LESS;
    compare_case(top, rop, memory, fb, state, fragment, 0xff0000ff);

    // 在验证通过后收缩容量，检查模板写失败不会继续写深度和颜色。
    top.width = fb.m_width;
    top.height = fb.m_height;
    top.color_base = fb.m_color_address;
    top.depth_base = fb.m_depth_address;
    top.stencil_base = fb.m_stencil_address;
    top.has_depth = 1;
    top.has_stencil = 1;
    top.depth_test = 1;
    top.depth_write = 1;
    top.depth_compare = 7;
    top.stencil_test = 1;
    top.stencil_compare = 7;
    top.stencil_reference = 0x7e;
    top.stencil_read_mask = 0xff;
    top.stencil_write_mask = 0xff;
    top.stencil_fail = 0;
    top.stencil_depth_fail = 0;
    top.stencil_pass = 2;
    top.color_write = 1;
    top.blend = 0;
    top.fragment_x = 1;
    top.fragment_y = 1;
    top.fragment_depth = bits(.25f);
    top.fragment_rgba = 0xff102030;
    top.request_valid = 1;
    tick(top);
    top.request_valid = 0;
    tick(top);
    tick(top);
    tick(top);
    tick(top);
    top.capacity_bytes = 768;
    tick(top);
    top.capacity_bytes = 4096;
    tick(top);
    if (!top.response_valid || !top.response_error || top.response_pass || !top.request_ready) {
        throw std::runtime_error("ROP continued after a mid-flight stencil write error");
    }
    compare_memory(top, memory);
}
}

// 将Verilator与功能模型的附件差分结果传给CTest。
int main() {
    try {
        run_test();
        std::cout << "ROP fragment memory compare: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
