#include "mem/dcache.h"
#include "mem/icache.h"
#include "memory_dut.h"

namespace helo::verif {
// 将统一存储测试协议映射到功能模型的三个单元。
class FuncMemoryDut final : public MemoryDut {
public:
    // 建立共享同一底层内存的指令与数据缓存适配层。
    explicit FuncMemoryDut(std::size_t bytes)
        : m_memory(bytes), m_icache(m_memory), m_dcache(m_memory) {
    }

    // 经直接内存端口读取数据。
    std::uint32_t read(std::uint32_t address, unsigned size) const override {
        return m_memory.read(address, size);
    }

    // 经直接内存端口写入数据。
    void write(std::uint32_t address, unsigned size, std::uint32_t value) override {
        m_memory.write(address, size, value);
    }

    // 验证直接内存端口请求。
    void validate(std::uint32_t address, unsigned size) const override {
        m_memory.validate(address, size);
    }

    // 装载完整32位数据字序列。
    void load_words(std::uint32_t address, const std::vector<std::uint32_t>& words) override {
        m_memory.load_words(address, words);
    }

    // 装载完整64位指令序列。
    void load_instructions(
        std::uint32_t address, const std::vector<std::uint64_t>& words) override {
        m_memory.load_instructions(address, words);
    }

    // 从指令缓存接口取得完整指令。
    std::uint64_t fetch(std::uint32_t address) const override {
        return m_icache.fetch(address);
    }

    // 从数据缓存接口读取数据。
    std::uint32_t data_read(std::uint32_t address, unsigned size) const override {
        return m_dcache.read(address, size);
    }

    // 从数据缓存接口更新统一内存。
    void data_write(std::uint32_t address, unsigned size, std::uint32_t value) override {
        m_dcache.write(address, size, value);
    }

    // 验证数据缓存请求且不修改内存。
    void data_validate(std::uint32_t address, unsigned size) const override {
        m_dcache.validate(address, size);
    }

private:
    helo::func::Mem m_memory; // 保存统一测试字节空间。
    helo::func::Icache m_icache; // 提供连接同一内存的取指端口。
    helo::func::Dcache m_dcache; // 提供连接同一内存的数据端口。
};

// 创建选定功能模型的内存测试适配器。
std::unique_ptr<MemoryDut> create_memory_dut(std::size_t bytes) {
    return std::make_unique<FuncMemoryDut>(bytes);
}
}
