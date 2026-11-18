#pragma once
#include "instr.h"
#include <vector>

namespace helo::func {
// 按 warp 和线程掩码跟踪寄存器及浮点状态的未完成写入。
class Scoreboard {
public:
    explicit Scoreboard(std::size_t warp_count);
    void reset();
    bool ready(const BufferedInstr& instruction) const;
    void reserve(const BufferedInstr& instruction);
    void release(const BufferedInstr& instruction);

private:
    std::vector<std::array<ThreadMask, REGISTER_COUNT + 1>>
        m_pending; // 按 warp 和寄存器保存待写线程掩码，最后一槽用于浮点控制状态。
};
}
