#include "dcache.h"

namespace helo::func {
// 绑定数据访存所使用的统一存储。
Dcache::Dcache(Mem& memory) : m_memory(memory) {
}

// 将读取请求直接转发至底层内存。
std::uint32_t Dcache::read(std::uint32_t address, unsigned size) const {
    return m_memory.read(address, size);
}

// 将写入请求直接转发至底层内存。
void Dcache::write(std::uint32_t address, unsigned size, std::uint32_t value) {
    m_memory.write(address, size, value);
}

// 在提交线程组的访存前验证地址。
void Dcache::validate(std::uint32_t address, unsigned size) const {
    m_memory.validate(address, size);
}
}
