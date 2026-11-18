#pragma once
#include "ibuffer.h"
#include "regfile.h"
#include "scoreboard.h"
#include <optional>

namespace helo::func {
// 检查指令依赖并读取操作数，将就绪指令从缓冲中发射。
class Isu {
public:
    std::optional<IssuedInstr> issue(
        std::size_t warp, Ibuffer& buffer, Scoreboard& scoreboard, const Regfile& registers) const;
};
}
