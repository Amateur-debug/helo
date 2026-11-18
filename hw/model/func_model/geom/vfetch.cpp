#include "vfetch.h"
#include "mem/mem.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace helo::func {
namespace {
// 校验整个连续属性范围，阻止地址溢出和未对齐读取。
void validate_range(const Mem& memory,
    std::uint32_t address,
    std::uint32_t count,
    std::uint32_t stride,
    std::uint32_t bytes) {
    if (stride < bytes || stride % 4 || address % 4) {
        throw std::invalid_argument("invalid vertex/index layout");
    }
    if (!count) {
        return;
    }
    const auto end = std::uint64_t(address) + std::uint64_t(count - 1) * stride + bytes;
    if (end > 0x100000000ULL) {
        throw std::out_of_range("vertex/index address overflow");
    }
    memory.validate(address, 4);
    memory.validate(static_cast<std::uint32_t>(end - 4), 4);
}

// 将小端内存中的 FP32 位模式恢复为主机浮点值。
float read_float(const Mem& memory, std::uint32_t address) {
    const auto bits = memory.read(address, 4);
    float value;
    static_assert(sizeof(value) == sizeof(bits), "FP32 storage required");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
}

// 将顶点获取单元连接到统一内存。
Vfetch::Vfetch(const Mem& memory) : m_memory(memory) {
}

// 按本次绘制的布局映射属性，读取流之前验证全部偏移与访问范围。
std::vector<Vertex> Vfetch::fetch(std::uint32_t address,
    std::uint32_t count,
    std::uint32_t stride,
    std::uint32_t layout_address,
    unsigned varying_count) const {
    if (varying_count > MAX_VARYING_COMPONENTS || (layout_address == 0 && varying_count != 0)) {
        throw std::invalid_argument("invalid vertex varying layout");
    }
    // 表内前三项分别描述位置、颜色、UV连续分量；每项自定义属性是单个FP32。
    std::array<std::uint32_t, 3 + MAX_VARYING_COMPONENTS> offsets{0, 16, 32};
    const auto entries = 3 + varying_count;
    if (layout_address != 0) {
        validate_range(m_memory, layout_address, entries, 4, 4);
        for (unsigned slot = 0; slot < entries; ++slot) {
            offsets[slot] = m_memory.read(layout_address + 4 * slot, 4);
        }
    }
    if (offsets[0] == ABSENT_ATTRIBUTE) {
        throw std::invalid_argument("vertex layout requires position");
    }
    std::uint32_t record_bytes = 0;
    for (unsigned slot = 0; slot < entries; ++slot) {
        const auto offset = offsets[slot];
        if (offset == ABSENT_ATTRIBUTE) {
            continue;
        }
        const unsigned bytes = slot < 2 ? 16 : slot == 2 ? 8 : 4;
        if (offset % 4 != 0 || std::uint64_t(offset) + bytes > stride) {
            throw std::invalid_argument("attribute outside vertex stride or unaligned");
        }
        record_bytes = std::max(record_bytes, offset + bytes);
    }
    validate_range(m_memory, address, count, stride, record_bytes);
    std::vector<Vertex> result(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto base = address + i * stride;
        auto& vertex = result[i];
        for (unsigned j = 0; j < vertex.m_position.size(); ++j) {
            vertex.m_position[j] = read_float(m_memory, base + offsets[0] + j * 4);
        }
        if (offsets[1] != ABSENT_ATTRIBUTE) {
            for (unsigned j = 0; j < vertex.m_color.size(); ++j) {
                vertex.m_color[j] = read_float(m_memory, base + offsets[1] + j * 4);
            }
        }
        if (offsets[2] != ABSENT_ATTRIBUTE) {
            for (unsigned j = 0; j < vertex.m_uv.size(); ++j) {
                vertex.m_uv[j] = read_float(m_memory, base + offsets[2] + j * 4);
            }
        }
        for (unsigned slot = 0; slot < varying_count; ++slot) {
            if (offsets[3 + slot] != ABSENT_ATTRIBUTE) {
                vertex.m_varyings[slot] = read_float(m_memory, base + offsets[3 + slot]);
            }
        }
    }
    return result;
}

// 读取连续的小端 32 位顶点索引。
std::vector<std::uint32_t> Vfetch::fetch_indices(std::uint32_t address, std::uint32_t count) const {
    validate_range(m_memory, address, count, 4, 4);
    std::vector<std::uint32_t> result(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        result[i] = m_memory.read(address + i * 4, 4);
    }
    return result;
}
}
