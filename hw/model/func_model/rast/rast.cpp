#include "rast/rast.h"
#include <stdexcept>

namespace helo::func {
// 先完成全部三角形设置校验，再建立按行扫描的独立游标。
Rast::Cursor Rast::begin(
    const ScreenTriangle& triangle, std::uint32_t width, std::uint32_t height) const {
    Cursor cursor;
    cursor.m_setup = m_setup.prepare(triangle, width, height);
    if (cursor.m_setup) {
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
        const auto x = cursor.m_x++;
        const auto y = cursor.m_y;
        std::array<double, 3> weights{};
        bool covered = true;
        for (unsigned i = 0; i < 3; ++i) {
            const auto& a = vertices[(i + 1) % 3];
            const auto& b = vertices[(i + 2) % 3];
            const double value = m_setup.edge(a, b, double(x) + 0.5, double(y) + 0.5);
            if (value < 0 || (value == 0 && !m_setup.top_left(a, b))) {
                covered = false;
                break;
            }
            weights[i] = value / setup.m_area;
        }
        if (covered) {
            fragments.push_back(
                m_interp.interpolate(vertices, weights, std::uint32_t(x), std::uint32_t(y)));
        }
    }
    if (cursor.m_y > setup.m_max_y) {
        cursor.m_setup.reset();
    }
    return !fragments.empty();
}

// 为需要完整结果的独立调用保留收集接口，整机使用有界next接口。
std::vector<Fragment> Rast::rasterize(
    const ScreenTriangle& triangle, std::uint32_t width, std::uint32_t height) const {
    auto cursor = begin(triangle, width, height);
    std::vector<Fragment> result;
    std::vector<Fragment> batch;
    while (next(cursor, batch, 128)) {
        result.insert(result.end(), batch.begin(), batch.end());
    }
    return result;
}
}
