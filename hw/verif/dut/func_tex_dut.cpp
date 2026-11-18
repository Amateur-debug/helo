#include "tex/tex.h"
#include "tex_dut.h"

namespace helo::verif {
// 将共用纹理描述转换为功能实现的输入状态。
static helo::TextureDesc texture_desc(const TextureDesc& input) {
    return {input.m_address,
        input.m_width,
        input.m_height,
        static_cast<helo::TextureFilter>(input.m_filter),
        static_cast<helo::TextureWrap>(input.m_wrap_u),
        static_cast<helo::TextureWrap>(input.m_wrap_v)};
}

// 将Tex模块共用测试转发至功能实现。
class FuncTexDut final : public TexDut {
public:
    // 更新测试内存中的一个字。
    void write_memory(std::uint32_t address, std::uint32_t value) override {
        m_memory.write(address, 4, value);
    }

    // 通过纹理单元读取过滤结果。
    helo::Color sample(
        const TextureDesc& texture, float u, float v, unsigned levels, float lod) override {
        return m_tex.prepare(texture_desc(texture), levels).sample(u, v, lod);
    }

private:
    func::Mem m_memory{65536}; // 保存纹理单元测试的纹素。
    func::Tex m_tex{m_memory}; // 驱动纹理采样模块。
};

// 为一次模块测试建立独立的功能适配器。
std::unique_ptr<TexDut> make_tex_dut() {
    return std::make_unique<FuncTexDut>();
}
}
