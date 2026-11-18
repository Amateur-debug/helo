#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace helo::func {
// 以字节数组保存统一地址空间，并提供小端数据与指令装载接口。
class Mem {
public:
    explicit Mem(std::size_t bytes);
    std::uint32_t read(std::uint32_t address, unsigned size) const;
    void write(std::uint32_t address, unsigned size, std::uint32_t value);
    void load_words(std::uint32_t address, const std::vector<std::uint32_t>& words);
    void load_instructions(std::uint32_t address, const std::vector<std::uint64_t>& instructions);
    void validate(std::uint32_t address, unsigned size) const;

private:
    std::vector<std::uint8_t> m_bytes; // 按字节地址保存统一内存内容。
};
}
