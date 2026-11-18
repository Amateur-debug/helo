#include "Vhelo_memory.h"
#include "memory_dut.h"
#include "verilated.h"
#include <limits>
#include <stdexcept>

namespace helo::verif {
// 通过时钟边沿推进Verilator内存模型，并将错误响应映射为测试异常。
class RtlMemoryDut final : public MemoryDut {
public:
    // 为单个测试实例建立独立的仿真上下文和设备内存。
    explicit RtlMemoryDut(std::size_t bytes)
        : m_context(std::make_unique<VerilatedContext>()),
          m_top(std::make_unique<Vhelo_memory>(m_context.get())) {
        if (bytes > MAX_BYTES || bytes > std::numeric_limits<std::uint32_t>::max()) {
            throw std::invalid_argument("RTL memory capacity exceeds compiled limit");
        }
        m_top->clk = 0;
        m_top->capacity_bytes = static_cast<std::uint32_t>(bytes);
        m_top->host_valid = 0;
        m_top->data_valid = 0;
        m_top->fetch_valid = 0;
        m_top->eval();
    }

    // 从主机端口读取低位零扩展的数据。
    std::uint32_t read(std::uint32_t address, unsigned size) const override {
        return host_access(address, size, false, 0);
    }

    // 在完整请求合法时提交主机端口写入。
    void write(std::uint32_t address, unsigned size, std::uint32_t value) override {
        host_access(address, size, true, value);
    }

    // 只检查主机请求而不触发写时钟。
    void validate(std::uint32_t address, unsigned size) const override {
        host_access(address, size, false, 0);
    }

    // 在写入前检查整个字序列的范围。
    void load_words(std::uint32_t address, const std::vector<std::uint32_t>& words) override {
        check_load(address, words.size(), 4);
        for (std::size_t i = 0; i < words.size(); ++i) {
            write(address + static_cast<std::uint32_t>(i * 4), 4, words[i]);
        }
    }

    // 按机器码的小端低字优先顺序装载指令。
    void load_instructions(
        std::uint32_t address, const std::vector<std::uint64_t>& words) override {
        check_load(address, words.size(), 8);
        for (std::size_t i = 0; i < words.size(); ++i) {
            const auto pc = address + static_cast<std::uint32_t>(i * 8);
            write(pc, 4, static_cast<std::uint32_t>(words[i]));
            write(pc + 4, 4, static_cast<std::uint32_t>(words[i] >> 32));
        }
    }

    // 从独立取指端口读取完整64位指令。
    std::uint64_t fetch(std::uint32_t address) const override {
        m_top->host_valid = 0;
        m_top->data_valid = 0;
        m_top->fetch_addr = address;
        m_top->fetch_valid = 1;
        m_top->eval();
        if (m_top->fetch_error) {
            throw std::out_of_range("RTL instruction fetch rejected");
        }
        return m_top->fetch_instr;
    }

    // 从数据端口读取低位零扩展的数据。
    std::uint32_t data_read(std::uint32_t address, unsigned size) const override {
        return data_access(address, size, false, 0);
    }

    // 在完整请求合法时提交数据端口写入。
    void data_write(std::uint32_t address, unsigned size, std::uint32_t value) override {
        data_access(address, size, true, value);
    }

    // 只检查数据请求而不触发写时钟。
    void data_validate(std::uint32_t address, unsigned size) const override {
        data_access(address, size, false, 0);
    }

private:
    static constexpr std::size_t MAX_BYTES = 4096;
    std::unique_ptr<VerilatedContext> m_context; // 保存本实例的Verilator运行状态。
    mutable std::unique_ptr<Vhelo_memory> m_top; // 保存受测的RTL实例及信号。

    // 批量装载在发生任何写入前检查对齐、容量和32位地址上限。
    void check_load(std::uint32_t address, std::size_t count, unsigned width) const {
        const auto capacity = std::uint64_t(m_top->capacity_bytes);
        if (address % width || address > capacity || count > (capacity - address) / width ||
            count > (0x100000000ULL - address) / width) {
            throw std::out_of_range("RTL program load range invalid");
        }
    }

    // 提交一次有效写入并让组合输出看到新存储内容。
    void tick() const {
        m_top->clk = 1;
        m_top->eval();
        m_top->clk = 0;
        m_top->host_valid = 0;
        m_top->data_valid = 0;
        m_top->eval();
    }

    // 对主机端口设置请求，先检查RTL错误再读取或写入。
    std::uint32_t host_access(
        std::uint32_t address, unsigned size, bool write_request, std::uint32_t value) const {
        if (size > 7) {
            throw std::invalid_argument("RTL memory width cannot be encoded");
        }
        m_top->data_valid = 0;
        m_top->fetch_valid = 0;
        m_top->host_valid = 1;
        m_top->host_write = write_request;
        m_top->host_addr = address;
        m_top->host_size = size;
        m_top->host_wdata = value;
        m_top->eval();
        if (m_top->host_error) {
            throw std::out_of_range("RTL host memory request rejected");
        }
        const auto result = m_top->host_rdata;
        if (write_request) {
            tick();
        }
        return result;
    }

    // 对数据端口设置请求，先检查RTL错误再读取或写入。
    std::uint32_t data_access(
        std::uint32_t address, unsigned size, bool write_request, std::uint32_t value) const {
        if (size > 7) {
            throw std::invalid_argument("RTL memory width cannot be encoded");
        }
        m_top->host_valid = 0;
        m_top->fetch_valid = 0;
        m_top->data_valid = 1;
        m_top->data_write = write_request;
        m_top->data_addr = address;
        m_top->data_size = size;
        m_top->data_wdata = value;
        m_top->eval();
        if (m_top->data_error) {
            throw std::out_of_range("RTL data memory request rejected");
        }
        const auto result = m_top->data_rdata;
        if (write_request) {
            tick();
        }
        return result;
    }
};

// 创建内存、取指和数据端口共用的RTL测试适配器。
std::unique_ptr<MemoryDut> create_memory_dut(std::size_t bytes) {
    return std::make_unique<RtlMemoryDut>(bytes);
}
}
