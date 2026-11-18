#include "sfu.h"
#include <stdexcept>

namespace helo::func {
// 明确拒绝尚未定义数值语义和编码的执行请求。
ExecutionResult Sfu::execute(const IssuedInstr&) const {
    throw std::runtime_error("unsupported SFU instruction");
}
}
