#pragma once
#include "instr.h"

namespace helo::func {
// 对有效线程执行整数运算并生成寄存器结果。
class Iu {
public:
    ExecutionResult execute(const IssuedInstr& instruction) const;
};
}
