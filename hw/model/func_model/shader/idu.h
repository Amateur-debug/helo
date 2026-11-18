#pragma once
#include "instr.h"

namespace helo::func {
// 检查并解码 HELO 机器码，生成操作数依赖和执行单元信息。
class Idu {
public:
    Instr decode(std::uint64_t encoding) const;
};
}
