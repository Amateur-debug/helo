#pragma once
#include "regfile.h"
#include "scoreboard.h"

namespace helo::func {
// 按指令发射掩码提交寄存器和浮点状态写回，并解除依赖。
class Wbu {
public:
    void writeback(const ExecutionResult& result, Regfile& registers, Scoreboard& scoreboard) const;
};
}
