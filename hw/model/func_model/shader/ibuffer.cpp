#include "ibuffer.h"
#include <stdexcept>

namespace helo::func {
// 建立相互独立的warp指令队列。
Ibuffer::Ibuffer(std::size_t warp_count) : m_queues(warp_count) {
}

// 丢弃所有尚未发射的指令。
void Ibuffer::reset() {
    for (auto& queue : m_queues) {
        queue.clear();
    }
}

// 将解码结果追加到所属warp的队尾。
void Ibuffer::push(const BufferedInstr& instruction) {
    m_queues.at(instruction.m_path.m_warp).push_back(instruction);
}

// 返回队首指令，空队列返回空指针。
const BufferedInstr* Ibuffer::front(std::size_t warp) const {
    const auto& queue = m_queues.at(warp);
    return queue.empty() ? nullptr : &queue.front();
}

// 移除成功发射的队首指令。
void Ibuffer::pop(std::size_t warp) {
    auto& queue = m_queues.at(warp);
    if (queue.empty()) {
        throw std::logic_error("empty instruction buffer");
    }
    queue.pop_front();
}
}
