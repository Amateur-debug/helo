#pragma once
#include "instr.h"

namespace helo::func {
// 执行 HELO FP32 运算与浮点控制状态访问，并生成待提交结果。
class Fpu {
public:
    ExecutionResult execute(const IssuedInstr& instruction) const;
};
}
