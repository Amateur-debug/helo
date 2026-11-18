#pragma once
#include <cstdint>
#include <stdexcept>

// HELO 固定64位编码；主操作码决定格式，功能字段决定具体运算。
#define HELO_OPCODE_OP 0x01u
#define HELO_OPCODE_OP_IMM 0x02u
#define HELO_OPCODE_MULDIV 0x03u
#define HELO_OPCODE_LOAD 0x04u
#define HELO_OPCODE_STORE 0x05u
#define HELO_OPCODE_FP 0x06u
#define HELO_OPCODE_FMA 0x07u
#define HELO_OPCODE_BRANCH 0x08u
#define HELO_OPCODE_JUMP 0x09u
#define HELO_OPCODE_JUMP_REG 0x0au
#define HELO_OPCODE_UPPER 0x0bu
#define HELO_OPCODE_SYSTEM 0x0cu
#define HELO_OPCODE_WORKGROUP 0x0eu
#define HELO_OPCODE_TEX 0x0du
#define HELO_OPCODE(encoding) (static_cast<unsigned>((encoding) & 0xffu))

namespace helo::func::encoding {
enum class Format { R, R4, I, S, B, U, J };

// 保存按指令格式提取的寄存器、功能、控制与立即数字段。
struct Fields {
    unsigned m_rd = 0; // 保存目的寄存器槽的解析值，S/B格式会重映射到源寄存器。
    unsigned m_rs1 = 0; // 保存第一源寄存器编号，CSR立即数形式下保存zimm。
    unsigned m_rs2 = 0; // 保存第二源通用寄存器编号。
    unsigned m_rs3 = 0; // 保存第三源通用寄存器编号。
    unsigned m_funct = 0; // 保存指令族内用于选择运算的功能字段。
    unsigned m_ctrl = 0; // 保存R/R4格式的控制字段，供舍入模式等检查使用。
    std::uint32_t m_immediate = 0; // 保存完成符号扩展或格式移位后的立即数位模式。
    unsigned m_raw_immediate = 0; // 保存尚未符号扩展或移位的原始无符号立即数字段。
};

// 将有界补码字段符号扩展为32位位模式，不依赖宿主有符号移位。
inline constexpr std::uint32_t sign_extend(std::uint32_t value, unsigned width) {
    const auto sign = std::uint32_t(1) << (width - 1);
    return (value ^ sign) - sign;
}

// 提取单个字节字段，保留完整8位寄存器编号。
inline constexpr unsigned byte(std::uint64_t encoding, unsigned shift) {
    return static_cast<unsigned>((encoding >> shift) & 0xffu);
}

// 在解析字段前拒绝本版本尚未分配的保留位。
inline void require_zero(std::uint64_t bits) {
    if (bits != 0) {
        throw std::runtime_error("illegal instruction: nonzero reserved field");
    }
}

// 根据主操作码确定唯一的指令格式。
inline Format classify(std::uint64_t encoding) {
    switch (HELO_OPCODE(encoding)) {
    case HELO_OPCODE_OP:
    case HELO_OPCODE_MULDIV:
    case HELO_OPCODE_WORKGROUP:
    case HELO_OPCODE_FP:
        return Format::R;
    case HELO_OPCODE_FMA:
    case HELO_OPCODE_TEX:
        return Format::R4;
    case HELO_OPCODE_OP_IMM:
    case HELO_OPCODE_LOAD:
    case HELO_OPCODE_JUMP_REG:
    case HELO_OPCODE_SYSTEM:
        return Format::I;
    case HELO_OPCODE_STORE:
        return Format::S;
    case HELO_OPCODE_BRANCH:
        return Format::B;
    case HELO_OPCODE_UPPER:
        return Format::U;
    case HELO_OPCODE_JUMP:
        return Format::J;
    default:
        throw std::runtime_error("illegal instruction: unassigned opcode");
    }
}

// 解析 R4 型的全部寄存器、功能和控制字段。
inline Fields decode_r4(std::uint64_t encoding) {
    require_zero(encoding >> 56);
    Fields fields;
    fields.m_rd = byte(encoding, 8);
    fields.m_rs1 = byte(encoding, 16);
    fields.m_rs2 = byte(encoding, 24);
    fields.m_rs3 = byte(encoding, 32);
    fields.m_funct = byte(encoding, 40);
    fields.m_ctrl = byte(encoding, 48);
    return fields;
}

// 解析 R 型并检查未分配给第三源的字节必须为零。
inline Fields decode_r(std::uint64_t encoding) {
    auto fields = decode_r4(encoding);
    require_zero(fields.m_rs3);
    return fields;
}

// 解析 I 型，分别保留符号扩展值与 CSR/陷阱使用的无符号字段。
inline Fields decode_i(std::uint64_t encoding) {
    require_zero(encoding >> 44);
    Fields fields;
    fields.m_rd = byte(encoding, 8);
    fields.m_rs1 = byte(encoding, 16);
    fields.m_funct = byte(encoding, 24);
    fields.m_raw_immediate = static_cast<unsigned>((encoding >> 32) & 0xfffu);
    fields.m_immediate = sign_extend(fields.m_raw_immediate, 12);
    return fields;
}

// 解析 S 型，将低寄存器槽解释为存储数据源。
inline Fields decode_s(std::uint64_t encoding) {
    auto fields = decode_i(encoding);
    fields.m_rs2 = fields.m_rd;
    fields.m_rd = 0;
    return fields;
}

// 解析 B 型并恢复偏移隐含的最低零位。
inline Fields decode_b(std::uint64_t encoding) {
    auto fields = decode_i(encoding);
    fields.m_rs2 = fields.m_rs1;
    fields.m_rs1 = fields.m_rd;
    fields.m_rd = 0;
    fields.m_immediate = sign_extend(fields.m_raw_immediate << 1, 13);
    return fields;
}

// 解析 U 型，将20位常量放入32位结果的高20位。
inline Fields decode_u(std::uint64_t encoding) {
    require_zero(encoding >> 52);
    require_zero(byte(encoding, 24));
    Fields fields;
    fields.m_rd = byte(encoding, 8);
    fields.m_funct = byte(encoding, 16);
    fields.m_raw_immediate = static_cast<unsigned>((encoding >> 32) & 0xfffffu);
    fields.m_immediate = fields.m_raw_immediate << 12;
    return fields;
}

// 解析 J 型，将20位字段重建为21位有符号字节偏移。
inline Fields decode_j(std::uint64_t encoding) {
    auto fields = decode_u(encoding);
    fields.m_immediate = sign_extend(fields.m_raw_immediate << 1, 21);
    return fields;
}
}

#define HELO_DECODE_R(word) (::helo::func::encoding::decode_r(word))
#define HELO_DECODE_R4(word) (::helo::func::encoding::decode_r4(word))
#define HELO_DECODE_I(word) (::helo::func::encoding::decode_i(word))
#define HELO_DECODE_S(word) (::helo::func::encoding::decode_s(word))
#define HELO_DECODE_B(word) (::helo::func::encoding::decode_b(word))
#define HELO_DECODE_U(word) (::helo::func::encoding::decode_u(word))
#define HELO_DECODE_J(word) (::helo::func::encoding::decode_j(word))
