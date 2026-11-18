#include "icache.h"
#include <stdexcept>

namespace helo::func {
// 绑定统一存储，不建立额外缓存副本。
Icache::Icache(Mem& memory) : m_memory(memory) {
}

// 验证完整八字节取指区间并按小端拼接HELO指令。
std::uint64_t Icache::fetch(std::uint32_t pc) const {
    if (pc % 8) {
        throw std::runtime_error("misaligned instruction fetch");
    }
    m_memory.validate(pc, 4);
    m_memory.validate(pc + 4, 4);
    return std::uint64_t(m_memory.read(pc, 4)) | (std::uint64_t(m_memory.read(pc + 4, 4)) << 32);
}
}
