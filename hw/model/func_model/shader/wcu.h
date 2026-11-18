#pragma once
#include "instr.h"
#include <vector>

namespace helo::func {
// 处理分支、跳转、屏障及退出，并提交各线程的执行路径状态。
class Wcu {
public:
    void validate(const Warp& warp, const IssuedInstr& instruction) const;
    void validate_group(const std::vector<Warp>& warps, const IssuedInstr& instruction) const;
    void complete_group(std::vector<Warp>& warps, const ExecutionResult& result) const;
    ExecutionResult execute(const IssuedInstr& instruction) const;
    void complete(Warp& warp, const ExecutionResult& result) const;
};
}
