#pragma once
#include "dut/dut.h"
#include <stdexcept>

namespace helo::verif {
// 在功能断言不成立时终止当前用例并携带定位信息。
inline void require(bool condition, const char* description) {
    if (!condition) {
        throw std::runtime_error(description);
    }
}

// 按 HELO I 型字段独立编码12位立即数或 CSR 地址。
inline std::uint64_t imm(unsigned opcode, unsigned rd, unsigned funct, unsigned rs1, int value) {
    require(value >= -2048 && value <= 4095, "I immediate outside encoding range");
    return (std::uint64_t(static_cast<std::uint32_t>(value) & 4095U) << 32) |
           (std::uint64_t(funct) << 24) | (rs1 << 16) | (rd << 8) | opcode;
}

// 按 HELO R 型编码整数寄存器与乘除指令。
inline std::uint64_t reg(
    unsigned opcode, unsigned funct, unsigned rd, unsigned rs1, unsigned rs2 = 0) {
    return (std::uint64_t(funct) << 40) | (std::uint64_t(rs2) << 24) | (rs1 << 16) | (rd << 8) |
           opcode;
}

// 按 HELO S 型编码存储的基址、数据与有符号偏移。
inline std::uint64_t store(unsigned rs1, unsigned rs2, unsigned funct, int value = 0) {
    return imm(0x05, rs2, funct, rs1, value);
}

// 按 HELO B 型将字节偏移的一至十二位存入立即数字段。
inline std::uint64_t branch(unsigned rs1, unsigned rs2, unsigned condition, int offset) {
    require(
        offset >= -4096 && offset <= 4094 && offset % 2 == 0, "B offset outside encoding range");
    return (std::uint64_t((static_cast<std::uint32_t>(offset) >> 1) & 4095U) << 32) |
           (std::uint64_t(condition) << 24) | (rs2 << 16) | (rs1 << 8) | 0x08;
}

// 按 HELO J 型独立编码21位有符号字节偏移。
inline std::uint64_t jump(unsigned rd, int offset) {
    require(offset >= -1048576 && offset <= 1048574 && offset % 2 == 0,
        "J offset outside encoding range");
    return (std::uint64_t((static_cast<std::uint32_t>(offset) >> 1) & 0xfffffU) << 32) | (rd << 8) |
           0x09;
}

// 按 HELO U 型编码不带低12位的高位常量。
inline std::uint64_t upper(unsigned rd, unsigned funct, unsigned value) {
    require(value <= 0xfffffU, "U immediate outside encoding range");
    return (std::uint64_t(value) << 32) | (funct << 16) | (rd << 8) | 0x0b;
}

// 按 HELO FP/R 型编码功能号与独立的舍入字段。
inline std::uint64_t fp(
    unsigned funct, unsigned rd, unsigned rs1, unsigned rs2 = 0, unsigned mode = 0) {
    return reg(0x06, funct, rd, rs1, rs2) | (std::uint64_t(mode) << 48);
}

// 按 HELO FMA/R4 型编码三个完整8位源寄存器。
inline std::uint64_t fused(
    unsigned funct, unsigned rd, unsigned a, unsigned b, unsigned c, unsigned mode = 0) {
    return reg(0x07, funct, rd, a, b) | (std::uint64_t(c) << 32) | (std::uint64_t(mode) << 48);
}

constexpr std::uint64_t NOP = 0x0000000c;
constexpr std::uint64_t EXIT = 0x0100000c;
constexpr std::uint64_t FENCE = 0x0300000c;
constexpr std::uint64_t BARW = 0x0400000c;
void shader_test();
}
