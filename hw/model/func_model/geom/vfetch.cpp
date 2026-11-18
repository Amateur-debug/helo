#include "vfetch.h"
#include "mem/mem.h"
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

// 按 position/color/uv 固定布局读取顶点并允许额外的尾部步长。
std::vector<Vertex> Vfetch::fetch(
    std::uint32_t address, std::uint32_t count, std::uint32_t stride) const {
    validate_range(m_memory, address, count, stride, 40);
    std::vector<Vertex> result(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto base =
            static_cast<std::uint32_t>(std::uint64_t(address) + std::uint64_t(i) * stride);
        for (unsigned j = 0; j < 4; ++j) {
            result[i].m_position[j] = read_float(m_memory, base + j * 4);
        }
        for (unsigned j = 0; j < 4; ++j) {
            result[i].m_color[j] = read_float(m_memory, base + 16 + j * 4);
        }
        for (unsigned j = 0; j < 2; ++j) {
            result[i].m_uv[j] = read_float(m_memory, base + 32 + j * 4);
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
