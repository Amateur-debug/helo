#pragma once
#include <cstddef>

namespace helo {
// 配置单个Shader中各类标量执行通路的数量。
struct ExecutionConfig {
    unsigned m_iu_count = 32; // 指定整数执行通路数，范围1–32。
    unsigned m_fpu_count = 32; // 指定浮点及CSR执行通路数，范围1–32。
    unsigned m_lsu_count = 32; // 指定访存执行通路数，范围1–32。
    unsigned m_wcu_count = 32; // 指定控制执行通路数，范围1–32。
};

// 集中描述Shader独立的上下文、寄存器和执行资源。
struct ShaderConfig {
    std::size_t m_max_warps = 8; // 限制驻留warp数量。
    std::size_t m_max_threads = 256; // 限制线程槽位，按完整warp向下取整。
    std::size_t m_regfile_bytes = 261120; // 指定GPR字节预算，不含r0和FCSR。
    ExecutionConfig m_execution; // 配置不影响驻留容量的执行通路数量。
};

// 从GPU顶层统一传递设备内存和内部Shader配置。
struct GpuConfig {
    std::size_t m_memory_bytes = 16 * 1024 * 1024; // 指定统一地址空间的容量。
    ShaderConfig m_shader; // 配置GPU持有的可编程执行核心。
};
}
