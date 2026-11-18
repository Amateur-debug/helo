#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace helo::func {
constexpr std::size_t WARP_SIZE = 32;
constexpr std::size_t REGISTER_COUNT = 256;
using ThreadMask = std::uint32_t;

// 保存一个 warp 的线程存活、屏障等待与独立 PC 状态。
struct Warp {
    std::size_t m_id = 0; // 保存任务内的 warp 编号。
    ThreadMask m_live_mask = 0; // 标记尚未退出的线程，每位对应一个 lane。
    ThreadMask m_launch_mask = 0; // 保存本次启动的固定参与线程集合，供屏障协议使用。
    ThreadMask m_barrier_mask = 0; // 标记当前屏障轮次已到达并等待的线程。
    bool m_group_barrier = false; // 标记当前等待的是跨warp工作组屏障。
    std::uint32_t m_barrier_pc = 0; // 记录当前等待屏障的指令字节地址。
    std::array<std::uint32_t, WARP_SIZE> m_pc{}; // 按 lane 保存各线程的下一条指令字节地址。
    std::size_t m_next_thread = 0; // 记录下一轮优先尝试调度的 lane 索引。
};

// 描述本次调度选中的 warp、公共 PC 与有效线程集合。
struct ScheduledWarp {
    std::size_t m_warp = 0; // 指定被调度的 warp 索引。
    std::uint32_t m_pc = 0; // 保存本次线程组共同执行的指令字节地址。
    ThreadMask m_mask = 0; // 标记本次执行的有效线程，每位对应一个 lane。
};
}
