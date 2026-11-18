#pragma once
#include "../mem/dcache.h"
#include "instr.h"

namespace helo::func {
// 验证有效线程的访问地址，并通过数据缓存接口完成加载与存储。
class Lsu {
public:
    explicit Lsu(Dcache& cache);
    void validate(const IssuedInstr& instruction) const;
    ExecutionResult execute(const IssuedInstr& instruction);

private:
    Dcache& m_cache; // 引用用于数据读写与地址检查的数据缓存接口。
};
}
