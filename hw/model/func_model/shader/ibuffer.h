#pragma once
#include "instr.h"
#include <deque>
#include <vector>

namespace helo::func {
// 按 warp 保存解码指令并维持各队列的发射顺序。
class Ibuffer {
public:
    explicit Ibuffer(std::size_t warp_count);
    void reset();
    void push(const BufferedInstr& instruction);
    const BufferedInstr* front(std::size_t warp) const;
    void pop(std::size_t warp);

private:
    std::vector<std::deque<BufferedInstr>> m_queues; // 按 warp 索引保存先进先出的解码指令队列。
};
}
