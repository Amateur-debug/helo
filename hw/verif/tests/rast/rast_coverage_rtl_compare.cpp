#include "Vhelo_rast_coverage.h"
#include "rast/rast.h"
#include "verilated.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
// 保存精确到1/16像素的屏幕位置，不混入模型的浮点覆盖算法。
struct Point {
    int x;
    int y;
};

// 使用独立的整数交叉积检查片元携带的重心分子。
std::int64_t edge_value(Point a, Point b, std::int64_t x, std::int64_t y) {
    return std::int64_t(b.x - a.x) * (y - a.y) - std::int64_t(b.y - a.y) * (x - a.x);
}

// 生成可复现的三角形和背压序列。
std::uint32_t next_random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// 推进一个时钟周期，供ready/valid握手后采样状态。
void tick(Vhelo_rast_coverage& top) {
    top.clk = 0;
    top.eval();
    top.clk = 1;
    top.eval();
    top.clk = 0;
    top.eval();
}

// 将定点网格输入转换为功能模型精确可表示的binary32位置。
helo::ScreenTriangle reference_triangle(const std::array<Point, 3>& points) {
    helo::ScreenTriangle triangle;
    for (unsigned i = 0; i < 3; ++i) {
        triangle[i].m_position[0] = float(points[i].x) / 16;
        triangle[i].m_position[1] = float(points[i].y) / 16;
    }
    return triangle;
}

// 比较所有片元坐标、顺序、完成时序，并交替施加输出背压。
std::vector<std::pair<unsigned, unsigned>> compare_case(Vhelo_rast_coverage& top,
    const helo::func::Rast& reference,
    const std::array<Point, 3>& points,
    unsigned width,
    unsigned height,
    std::uint32_t& seed) {
    std::vector<std::pair<unsigned, unsigned>> expected;
    for (const auto& fragment : reference.rasterize(reference_triangle(points), width, height)) {
        expected.emplace_back(fragment.m_x, fragment.m_y);
    }
    if (!top.start_ready || top.busy || top.out_valid) {
        throw std::runtime_error("raster input not ready after previous triangle");
    }
    top.width = width;
    top.height = height;
    top.x0 = std::uint32_t(points[0].x) & 0xfffff;
    top.y0 = std::uint32_t(points[0].y) & 0xfffff;
    top.x1 = std::uint32_t(points[1].x) & 0xfffff;
    top.y1 = std::uint32_t(points[1].y) & 0xfffff;
    top.x2 = std::uint32_t(points[2].x) & 0xfffff;
    top.y2 = std::uint32_t(points[2].y) & 0xfffff;
    top.start_valid = 1;
    top.eval();
    tick(top);
    top.start_valid = 0;

    std::vector<std::pair<unsigned, unsigned>> actual;
    auto oriented = points;
    auto area = edge_value(oriented[0], oriented[1], oriented[2].x, oriented[2].y);
    if (area < 0) {
        std::swap(oriented[1], oriented[2]);
        area = -area;
    }
    bool finished = false;
    for (unsigned cycle = 0; cycle < 5000; ++cycle) {
        top.out_ready = (next_random(seed) & 3) != 0;
        top.eval();
        const bool handshake = top.out_valid && top.out_ready;
        const bool stalled = top.out_valid && !top.out_ready;
        const auto old_x = top.out_x;
        const auto old_y = top.out_y;
        const auto old_edges =
            std::array<std::uint64_t, 4>{top.out_edge0, top.out_edge1, top.out_edge2, top.out_area};
        if (handshake) {
            actual.emplace_back(top.out_x, top.out_y);
            const auto px = std::int64_t(top.out_x) * 16 + 8;
            const auto py = std::int64_t(top.out_y) * 16 + 8;
            const auto e0 = edge_value(oriented[1], oriented[2], px, py);
            const auto e1 = edge_value(oriented[2], oriented[0], px, py);
            const auto e2 = edge_value(oriented[0], oriented[1], px, py);
            if (top.out_area != std::uint64_t(area) || top.out_edge0 != std::uint64_t(e0) ||
                top.out_edge1 != std::uint64_t(e1) || top.out_edge2 != std::uint64_t(e2) ||
                e0 + e1 + e2 != area) {
                throw std::runtime_error("raster barycentric edge values differ");
            }
        }
        tick(top);
        if (stalled &&
            (!top.out_valid || top.out_x != old_x || top.out_y != old_y ||
                old_edges != std::array<std::uint64_t, 4>{
                                 top.out_edge0, top.out_edge1, top.out_edge2, top.out_area})) {
            throw std::runtime_error("raster output changed under backpressure");
        }
        if (top.done) {
            if (top.out_valid || top.busy || !top.start_ready) {
                throw std::runtime_error("raster done before final output handshake");
            }
            finished = true;
            break;
        }
    }
    if (!finished || actual != expected) {
        std::cerr << "raster mismatch width=" << width << " height=" << height << " vertices=";
        for (const auto& point : points) {
            std::cerr << " (" << point.x << ',' << point.y << ')';
        }
        std::cerr << " expected=" << expected.size() << " actual=" << actual.size() << '\n';
        throw std::runtime_error("RTL raster coverage differs from functional Rast");
    }
    return actual;
}

// 覆盖边界、裁界、退化、反向绕序和随机共享边矩形。
void run_test() {
    VerilatedContext context;
    Vhelo_rast_coverage top(&context);
    helo::func::Rast reference;
    top.start_valid = 0;
    top.out_ready = 1;
    top.reset = 1;
    tick(top);
    top.reset = 0;
    top.eval();
    std::uint32_t seed = 0x48454c4f;
    const std::array<Point, 3> lower{{{0, 0}, {64, 0}, {64, 64}}};
    const std::array<Point, 3> upper{{{0, 0}, {64, 64}, {0, 64}}};
    std::array<unsigned, 16> shared{};
    for (const auto& triangle : {lower, upper}) {
        for (const auto [x, y] : compare_case(top, reference, triangle, 4, 4, seed)) {
            ++shared[y * 4 + x];
        }
    }
    for (const auto count : shared) {
        if (count != 1) {
            throw std::runtime_error("shared diagonal has crack or double coverage");
        }
    }
    compare_case(top, reference, {{{64, 64}, {64, 0}, {0, 0}}}, 4, 4, seed);
    compare_case(top, reference, {{{0, 0}, {0, 0}, {64, 64}}}, 4, 4, seed);
    compare_case(top, reference, {{{0, 0}, {64, 0}, {64, 64}}}, 0, 4, seed);
    compare_case(top, reference, {{{-128, -128}, {320, -128}, {-128, 320}}}, 3, 2, seed);
    compare_case(
        top, reference, {{{-524288, -524288}, {524287, -524288}, {-524288, 524287}}}, 32, 32, seed);
    for (unsigned trial = 0; trial < 512; ++trial) {
        const auto coordinate = [&]() { return int(next_random(seed) % 769) - 256; };
        const std::array<Point, 3> triangle{{{coordinate(), coordinate()},
            {coordinate(), coordinate()},
            {coordinate(), coordinate()}}};
        compare_case(top, reference, triangle, 32, 24, seed);
    }
    for (unsigned trial = 0; trial < 64; ++trial) {
        const auto left = int(next_random(seed) % 256) - 64;
        const auto top_edge = int(next_random(seed) % 256) - 64;
        const auto right = left + int(next_random(seed) % 256) + 1;
        const auto bottom = top_edge + int(next_random(seed) % 256) + 1;
        const std::array<Point, 4> corners{
            {{left, top_edge}, {right, top_edge}, {right, bottom}, {left, bottom}}};
        const std::array<Point, 3> first{{corners[0], corners[1], corners[2]}};
        const std::array<Point, 3> second{{corners[0], corners[2], corners[3]}};
        std::array<unsigned, 32 * 32> coverage{};
        for (const auto& triangle : {first, second}) {
            for (const auto [x, y] : compare_case(top, reference, triangle, 32, 32, seed)) {
                ++coverage[y * 32 + x];
            }
        }
        for (unsigned y = 0; y < 32; ++y) {
            for (unsigned x = 0; x < 32; ++x) {
                const bool inside = int(x * 16 + 8) >= left && int(x * 16 + 8) < right &&
                                    int(y * 16 + 8) >= top_edge && int(y * 16 + 8) < bottom;
                if (coverage[y * 32 + x] != unsigned(inside)) {
                    throw std::runtime_error("shared rectangle coverage mismatch");
                }
            }
        }
    }
    top.width = 4;
    top.height = 4;
    top.x0 = 0;
    top.y0 = 0;
    top.x1 = 64;
    top.y1 = 0;
    top.x2 = 64;
    top.y2 = 64;
    top.start_valid = 1;
    top.out_ready = 0;
    tick(top);
    top.start_valid = 0;
    for (unsigned cycle = 0; cycle < 16 && !top.out_valid; ++cycle) {
        tick(top);
    }
    if (!top.out_valid || !top.busy || top.start_ready) {
        throw std::runtime_error("raster could not be stalled");
    }
    top.reset = 1;
    tick(top);
    top.reset = 0;
    top.eval();
    if (top.busy || top.out_valid || top.done || !top.start_ready) {
        throw std::runtime_error("raster reset did not cancel stalled output");
    }
    compare_case(top, reference, lower, 4, 4, seed);
}
}

// 将确定性片元坐标差分结果传递给CTest。
int main() {
    try {
        run_test();
        std::cout << "raster coverage RTL compare: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
