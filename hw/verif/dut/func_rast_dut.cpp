#include "rast/rast.h"
#include "rast_dut.h"

namespace helo::verif {
// 将Rast模块共用测试转发至功能实现。
class FuncRastDut final : public RastDut {
public:
    // 生成像素中心覆盖片元。
    std::vector<helo::Fragment> rasterize(
        const helo::ScreenTriangle& triangle, std::uint32_t width, std::uint32_t height) override {
        return m_rast.rasterize(triangle, width, height);
    }

    // 按指定重心坐标插值属性。
    helo::Fragment interpolate(
        const helo::ScreenTriangle& triangle, const std::array<double, 3>& weights) override {
        return m_interp.interpolate(triangle, weights, 0, 0);
    }

private:
    func::Rast m_rast; // 驱动完整光栅模块。
    func::Interp m_interp; // 驱动独立属性插值单元。
};

// 为一次模块测试建立独立的功能适配器。
std::unique_ptr<RastDut> make_rast_dut() {
    return std::make_unique<FuncRastDut>();
}
}
