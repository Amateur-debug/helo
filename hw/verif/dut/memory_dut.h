#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace helo::verif {
// 为各实现共用的内存及缓存测试提供统一访问协议。
class MemoryDut {
public:
    // 允许经统一接口安全释放具体适配器。
    virtual ~MemoryDut() = default;
    virtual std::uint32_t read(std::uint32_t address, unsigned size) const = 0;
    virtual void write(std::uint32_t address, unsigned size, std::uint32_t value) = 0;
    virtual void validate(std::uint32_t address, unsigned size) const = 0;
    virtual void load_words(std::uint32_t address, const std::vector<std::uint32_t>& words) = 0;
    virtual void load_instructions(
        std::uint32_t address, const std::vector<std::uint64_t>& words) = 0;
    virtual std::uint64_t fetch(std::uint32_t address) const = 0;
    virtual std::uint32_t data_read(std::uint32_t address, unsigned size) const = 0;
    virtual void data_write(std::uint32_t address, unsigned size, std::uint32_t value) = 0;
    virtual void data_validate(std::uint32_t address, unsigned size) const = 0;
};

std::unique_ptr<MemoryDut> create_memory_dut(std::size_t bytes);
}
