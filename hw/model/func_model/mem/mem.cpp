#include "mem.h"
#include <stdexcept>

namespace helo::func {
// 分配零初始化的统一字节地址空间。
Mem::Mem(std::size_t bytes) : m_bytes(bytes) {
}

// 检查访存宽度、自然对齐和越界，使用宽整数避免地址回绕。
void Mem::validate(std::uint32_t address, unsigned size) const {
    if (size != 1 && size != 2 && size != 4) {
        throw std::invalid_argument("invalid memory width");
    }
    if (address % size != 0) {
        throw std::runtime_error("misaligned memory access");
    }
    if (std::uint64_t(address) + size > m_bytes.size() ||
        std::uint64_t(address) + size > 0x100000000ULL) {
        throw std::out_of_range("memory access out of range");
    }
}

// 按小端顺序读取无符号数据。
std::uint32_t Mem::read(std::uint32_t address, unsigned size) const {
    validate(address, size);
    std::uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i) {
        value |= std::uint32_t(m_bytes[address + i]) << (8 * i);
    }
    return value;
}

// 按小端顺序写入指定宽度的数据。
void Mem::write(std::uint32_t address, unsigned size, std::uint32_t value) {
    validate(address, size);
    for (unsigned i = 0; i < size; ++i) {
        m_bytes[address + i] = (value >> (8 * i)) & 255;
    }
}

// 先检查整个程序范围再写入指令，避免部分加载。
void Mem::load_words(std::uint32_t address, const std::vector<std::uint32_t>& words) {
    if (address % 4 || address > m_bytes.size() || words.size() > (m_bytes.size() - address) / 4 ||
        words.size() > (0x100000000ULL - address) / 4) {
        throw std::out_of_range("program range invalid");
    }
    for (std::size_t i = 0; i < words.size(); ++i) {
        write(address + static_cast<std::uint32_t>(i * 4), 4, words[i]);
    }
}

// 在写入前验证整个64位程序范围，避免截断及部分加载。
void Mem::load_instructions(std::uint32_t address, const std::vector<std::uint64_t>& instructions) {
    if (address % 8 || address > m_bytes.size() ||
        instructions.size() > (m_bytes.size() - address) / 8 ||
        instructions.size() > (0x100000000ULL - address) / 8) {
        throw std::out_of_range("program range invalid");
    }
    for (std::size_t i = 0; i < instructions.size(); ++i) {
        const auto pc = address + static_cast<std::uint32_t>(i * 8);
        write(pc, 4, static_cast<std::uint32_t>(instructions[i]));
        write(pc + 4, 4, static_cast<std::uint32_t>(instructions[i] >> 32));
    }
}
}
