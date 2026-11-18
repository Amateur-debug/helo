#include "fpu.h"
#include <algorithm>
#include <softfloat.h>
#include <stdexcept>

namespace helo::func {
namespace {
// 判断原始单精度编码是否为NaN。
bool is_nan(std::uint32_t value) {
    return (value & 0x7fffffffu) > 0x7f800000u;
}

// 判断NaN是否需要触发无效运算标志。
bool is_signaling_nan(std::uint32_t value) {
    return is_nan(value) && !(value & 0x00400000u);
}

// 将单精度编码分类为HELO规定的十种互斥类别。
std::uint32_t classify(std::uint32_t value) {
    const auto magnitude = value & 0x7fffffffu;
    const bool negative = (value >> 31) != 0;
    if (is_nan(value)) {
        return 1u << (is_signaling_nan(value) ? 8 : 9);
    }
    if (magnitude == 0x7f800000u) {
        return 1u << (negative ? 0 : 7);
    }
    if (magnitude == 0) {
        return 1u << (negative ? 3 : 4);
    }
    if (magnitude < 0x00800000u) {
        return 1u << (negative ? 2 : 5);
    }
    return 1u << (negative ? 1 : 6);
}

// 标识真正使用舍入字段的浮点指令。
bool uses_rounding(Operation operation) {
    switch (operation) {
    case Operation::FADD:
    case Operation::FSUB:
    case Operation::FMUL:
    case Operation::FDIV:
    case Operation::FSQRT:
    case Operation::FMADD:
    case Operation::FMSUB:
    case Operation::FNMSUB:
    case Operation::FNMADD:
    case Operation::FCVT_W_S:
    case Operation::FCVT_WU_S:
    case Operation::FCVT_S_W:
    case Operation::FCVT_S_WU:
        return true;
    default:
        return false;
    }
}

// 在一次执行期间隔离SoftFloat线程局部状态，并在异常路径恢复调用者状态。
struct SoftfloatScope {
    uint_fast8_t m_rounding =
        softfloat_roundingMode; // 保存调用者舍入模式，供退出执行作用域时恢复。
    uint_fast8_t m_flags = softfloat_exceptionFlags; // 保存调用者异常标志，供退出执行作用域时恢复。
    uint_fast8_t m_tininess =
        softfloat_detectTininess; // 保存调用者微小性检测模式，供退出执行作用域时恢复。

    // 配置HELO要求的舍入后微小性检测。
    SoftfloatScope() {
        softfloat_detectTininess = softfloat_tininess_afterRounding;
    }

    // 恢复调用者的软件浮点环境。
    ~SoftfloatScope() {
        softfloat_roundingMode = m_rounding;
        softfloat_exceptionFlags = m_flags;
        softfloat_detectTininess = m_tininess;
    }
};
}

// 使用软件IEEE运算执行HELO FP32与CSR，结果和每线程fcsr仅在整组成功后提交。
ExecutionResult Fpu::execute(const IssuedInstr& instruction) const {
    ExecutionResult result;
    result.m_decoded = instruction.m_decoded;
    const auto& d = instruction.m_decoded.m_instr;
    const auto& path = instruction.m_decoded.m_path;
    SoftfloatScope environment;
    result.m_writes_fcsr = true;
    for (std::size_t thread = 0; thread < WARP_SIZE; ++thread) {
        if (!(path.m_mask & (1u << thread))) {
            continue;
        }
        const auto a = instruction.m_src1[thread];
        const auto b = instruction.m_src2[thread];
        const auto c = instruction.m_src3[thread];
        const auto old_fcsr = instruction.m_fcsr[thread];
        auto& value = result.m_values[thread];
        auto& fcsr = result.m_fcsr[thread];
        fcsr = old_fcsr;
        const auto rm = d.m_rounding_mode == 7 ? ((old_fcsr >> 5) & 7) : d.m_rounding_mode;
        if (uses_rounding(d.m_operation) && rm > 4) {
            throw std::runtime_error("reserved floating rounding mode");
        }
        softfloat_roundingMode = rm;
        softfloat_exceptionFlags = 0;
        const float32_t fa{a}, fb{b};
        bool canonicalize = false;
        switch (d.m_operation) {
        case Operation::FADD:
            value = f32_add(fa, fb).v;
            canonicalize = true;
            break;
        case Operation::FSUB:
            value = f32_sub(fa, fb).v;
            canonicalize = true;
            break;
        case Operation::FMUL:
            value = f32_mul(fa, fb).v;
            canonicalize = true;
            break;
        case Operation::FDIV:
            value = f32_div(fa, fb).v;
            canonicalize = true;
            break;
        case Operation::FSQRT:
            value = f32_sqrt(fa).v;
            canonicalize = true;
            break;
        case Operation::FMADD:
        case Operation::FMSUB:
        case Operation::FNMSUB:
        case Operation::FNMADD: {
            const auto product_sign =
                (d.m_operation == Operation::FNMSUB || d.m_operation == Operation::FNMADD)
                    ? 0x80000000u
                    : 0u;
            const auto addend_sign =
                (d.m_operation == Operation::FMSUB || d.m_operation == Operation::FNMADD)
                    ? 0x80000000u
                    : 0u;
            value = f32_mulAdd(float32_t{a ^ product_sign}, fb, float32_t{c ^ addend_sign}).v;
            canonicalize = true;
            break;
        }
        case Operation::FSGNJ:
            value = (a & 0x7fffffffu) | (b & 0x80000000u);
            break;
        case Operation::FSGNJN:
            value = (a & 0x7fffffffu) | (~b & 0x80000000u);
            break;
        case Operation::FSGNJX:
            value = a ^ (b & 0x80000000u);
            break;
        case Operation::FMIN:
        case Operation::FMAX: {
            const bool minimum = d.m_operation == Operation::FMIN;
            if (is_signaling_nan(a) || is_signaling_nan(b)) {
                softfloat_exceptionFlags |= softfloat_flag_invalid;
            }
            if (is_nan(a) && is_nan(b)) {
                value = 0x7fc00000u;
            } else if (is_nan(a)) {
                value = b;
            } else if (is_nan(b)) {
                value = a;
            } else if (((a | b) & 0x7fffffffu) == 0) {
                value = minimum ? (a | b) : (a & b);
            } else {
                value = (minimum ? f32_lt_quiet(fa, fb) : f32_lt_quiet(fb, fa)) ? a : b;
            }
            break;
        }
        case Operation::FEQ:
            value = f32_eq(fa, fb);
            break;
        case Operation::FLT:
            value = f32_lt(fa, fb);
            break;
        case Operation::FLE:
            value = f32_le(fa, fb);
            break;
        case Operation::FCLASS:
            value = classify(a);
            break;
        case Operation::FCVT_W_S:
            value = static_cast<std::uint32_t>(f32_to_i32(fa, rm, true));
            break;
        case Operation::FCVT_WU_S:
            value = static_cast<std::uint32_t>(f32_to_ui32(fa, rm, true));
            break;
        case Operation::FCVT_S_W: {
            const auto signed_value =
                std::int64_t(a) - ((a & 0x80000000u) ? (std::int64_t(1) << 32) : 0);
            value = i32_to_f32(static_cast<std::int32_t>(signed_value)).v;
            break;
        }
        case Operation::FCVT_S_WU:
            value = ui32_to_f32(a).v;
            break;
        case Operation::CSRRW:
        case Operation::CSRRS:
        case Operation::CSRRC: {
            const auto csr = d.m_immediate;
            const bool writing = d.m_operation == Operation::CSRRW || d.m_rs1 != 0;
            if (csr > 2 &&
                (csr < 0x10 || csr > (instruction.m_range.m_group_size ? 0x19u : 0x16u))) {
                throw std::runtime_error("illegal state access: undefined CSR");
            }
            if (csr > 2 && writing) {
                throw std::runtime_error("illegal state access: read-only CSR");
            }
            if (csr > 2) {
                switch (csr) {
                case 0x10:
                    value = static_cast<std::uint32_t>(thread);
                    break;
                case 0x11:
                    value =
                        instruction.m_range.warp_base() + static_cast<std::uint32_t>(path.m_warp);
                    break;
                case 0x12:
                    value = instruction.m_range.m_thread_base +
                            static_cast<std::uint32_t>(path.m_warp * WARP_SIZE + thread);
                    break;
                case 0x13:
                    value = instruction.m_thread_count;
                    break;
                case 0x14:
                    value = WARP_SIZE;
                    break;
                case 0x15:
                    value = instruction.m_param_base;
                    break;
                case 0x16:
                    value = 1;
                    break;
                case 0x17:
                    value = instruction.m_range.m_thread_base / instruction.m_range.m_group_size;
                    break;
                case 0x18:
                    value = static_cast<std::uint32_t>(path.m_warp * WARP_SIZE + thread);
                    break;
                case 0x19:
                    value = std::min(instruction.m_range.m_group_size,
                        instruction.m_thread_count - instruction.m_range.m_thread_base);
                    break;
                }
                break;
            }
            const auto shift = csr == 1 ? 5u : 0u;
            const auto mask = csr == 0 ? 0x1fu : csr == 1 ? 7u : 0xffu;
            value = (old_fcsr >> shift) & mask;
            if (writing) {
                const auto source = d.m_immediate_operand ? d.m_rs1 : a;
                const auto next = d.m_operation == Operation::CSRRW   ? source
                                  : d.m_operation == Operation::CSRRS ? value | source
                                                                      : value & ~source;
                fcsr = (old_fcsr & ~(mask << shift)) | ((next & mask) << shift);
            }
            break;
        }
        default:
            throw std::logic_error("instruction routed to wrong floating unit");
        }
        if (canonicalize && is_nan(value)) {
            value = 0x7fc00000u;
        }
        fcsr |= softfloat_exceptionFlags & 0x1f;
        result.m_next_pc[thread] = path.m_pc + 8;
    }
    return result;
}
}
