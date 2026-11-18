#pragma once
#include "common/types.h"
#include "mem/mem.h"

namespace helo::func {
// 从统一内存读取纹素并执行归一化二维采样。
class Tex {
public:
    // 保存经过验证的描述符快照，采样仍实时读取所属Tex的内存。
    class Sampler {
    public:
        Color sample(float u, float v, float lod = 0) const;
        Color sample_q16(std::int32_t u, std::int32_t v) const;

    private:
        friend class Tex;
        Sampler(const Tex& owner, std::vector<TextureDesc> levels);
        const Tex& m_owner; // 引用必须比采样句柄活得更久的纹理单元。
        std::vector<TextureDesc> m_levels; // 保存每级已验证的纹理范围和采样状态。
    };

    using Bindings = std::vector<Sampler>;

    explicit Tex(Mem& memory);
    Color sample(const TextureDesc& texture, float u, float v) const;
    void validate(const TextureDesc& texture) const;
    Sampler prepare(const TextureDesc& texture, unsigned level_count = 1) const;
    Bindings prepare_bindings(std::uint32_t address, unsigned count) const;

private:
    Color sample_validated(const TextureDesc& texture, float u, float v) const;
    Mem& m_memory; // 引用纹理数据所在的统一内存。
};
}
