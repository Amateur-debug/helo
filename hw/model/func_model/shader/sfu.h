#pragma once
#include "instr.h"

namespace helo::func {
// 提供特殊数学函数执行入口，并拒绝当前尚未定义的指令。
class Sfu {
public:
    ExecutionResult execute(const IssuedInstr& instruction) const;
};
}
