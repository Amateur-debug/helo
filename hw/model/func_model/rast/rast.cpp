#include "rast/rast.h"
#include <algorithm>
#include <stdexcept>

namespace helo::func {
// 四样本在像素四个象限中心按行排列，单样本保持像素中心。
std::array<float, 2> Rast::sample_offset(unsigned samples, unsigned sample) {
    if (samples == 1) {
        return {0.5F, 0.5F};
    }
    return {0.25F + 0.5F * (sample % 2), 0.25F + 0.5F * (sample / 2)};
}

// 先完成全部三角形设置校验，再建立按行扫描的独立游标。
Rast::Cursor Rast::begin(const ScreenTriangle& triangle,
    std::uint32_t width,
    std::uint32_t height,
    unsigned samples,
    const Scissor& scissor,
    RasterPrecision precision) const {
    if (samples != 1 && samples != 4) {
        throw std::invalid_argument("invalid sample count");
    }
    Cursor cursor;
    cursor.m_samples = samples;
    const bool fixed = precision == RasterPrecision::Q4_Q16_V1;
    cursor.m_setup =
        m_setup.prepare(fixed ? snap_positions(triangle) : triangle, width, height, samples);
    if (cursor.m_setup) {
        auto& setup = *cursor.m_setup;
        if (fixed) {
            cursor.m_fixed.emplace();
            for (unsigned i = 0; i < 3; ++i) {
                (*cursor.m_fixed)[i] = quantize_vertex(setup.m_vertices[i]);
            }
        }
        if (scissor.m_enabled) {
            const auto x_end =
                std::min<std::uint64_t>(std::uint64_t(scissor.m_x) + scissor.m_width, width);
            const auto y_end =
                std::min<std::uint64_t>(std::uint64_t(scissor.m_y) + scissor.m_height, height);
            setup.m_min_x = std::max(setup.m_min_x, int(std::min(scissor.m_x, width)));
            setup.m_min_y = std::max(setup.m_min_y, int(std::min(scissor.m_y, height)));
            setup.m_max_x = std::min(setup.m_max_x, int(x_end) - 1);
            setup.m_max_y = std::min(setup.m_max_y, int(y_end) - 1);
            if (setup.m_min_x > setup.m_max_x || setup.m_min_y > setup.m_max_y) {
                cursor.m_setup.reset();
                return cursor;
            }
        }
        cursor.m_x = cursor.m_setup->m_min_x;
        cursor.m_y = cursor.m_setup->m_min_y;
    }
    return cursor;
}

// 返回至多capacity个片元，保留原有按y再x递增的覆盖及插值顺序。
bool Rast::next(Cursor& cursor, std::vector<Fragment>& fragments, std::size_t capacity) const {
    if (!capacity) {
        throw std::invalid_argument("raster batch capacity must be positive");
    }
    fragments.clear();
    if (!cursor.m_setup) {
        return false;
    }
    const auto& setup = *cursor.m_setup;
    const auto& vertices = setup.m_vertices;
    while (cursor.m_y <= setup.m_max_y && fragments.size() < capacity) {
        if (cursor.m_x > setup.m_max_x) {
            cursor.m_x = setup.m_min_x;
            ++cursor.m_y;
            continue;
        }
        const auto x = cursor.m_x;
        const unsigned sample = cursor.m_sample++;
        if (cursor.m_sample == cursor.m_samples) {
            cursor.m_sample = 0;
            ++cursor.m_x;
        }
        const auto offset = sample_offset(cursor.m_samples, sample);
        std::array<std::uint64_t, 3> edges{};
        const auto y = cursor.m_y;
        std::array<double, 3> weights{};
        bool covered = true;
        for (unsigned i = 0; i < 3; ++i) {
            const auto& a = vertices[(i + 1) % 3];
            const auto& b = vertices[(i + 2) % 3];
            const double value = m_setup.edge(a, b, double(x) + offset[0], double(y) + offset[1]);
            if (value < 0 || (value == 0 && !m_setup.top_left(a, b))) {
                covered = false;
                break;
            }
            weights[i] = value / setup.m_area;
            if (cursor.m_fixed) {
                edges[i] = static_cast<std::uint64_t>(value * 256);
            }
        }
        if (covered) {
            fragments.push_back(
                m_interp.interpolate(vertices, weights, std::uint32_t(x), std::uint32_t(y)));
            auto& fragment = fragments.back();
            fragment.m_sample = sample;
            if (cursor.m_fixed) {
                interpolate_fixed(*cursor.m_fixed, edges, fragment);
            }
        }
    }
    if (cursor.m_y > setup.m_max_y) {
        cursor.m_setup.reset();
    }
    return !fragments.empty();
}

// 为需要完整结果的独立调用保留收集接口，整机使用有界next接口。
std::vector<Fragment> Rast::rasterize(const ScreenTriangle& triangle,
    std::uint32_t width,
    std::uint32_t height,
    RasterPrecision precision) const {
    auto cursor = begin(triangle, width, height, 1, {}, precision);
    std::vector<Fragment> result;
    std::vector<Fragment> batch;
    while (next(cursor, batch, 128)) {
        result.insert(result.end(), batch.begin(), batch.end());
    }
    return result;
}
}
