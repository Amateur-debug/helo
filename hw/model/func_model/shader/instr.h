#pragma once
#include "warp.h"

namespace helo::func {
enum class Operation {
    LUI,
    AUIPC,
    JAL,
    JALR,
    BEQ,
    BNE,
    BLT,
    BGE,
    BLTU,
    BGEU,
    LB,
    LH,
    LW,
    LBU,
    LHU,
    SB,
    SH,
    SW,
    ADD,
    SUB,
    SLL,
    SLT,
    SLTU,
    XOR,
    SRL,
    SRA,
    OR,
    AND,
    MOV,
    NOP,
    FENCE,
    EXIT,
    TRAP,
    BARW,
    MUL,
    MULH,
    MULHSU,
    MULHU,
    DIV,
    DIVU,
    REM,
    REMU,
    FADD,
    FSUB,
    FMUL,
    FDIV,
    FSQRT,
    FMADD,
    FMSUB,
    FNMSUB,
    FNMADD,
    FSGNJ,
    FSGNJN,
    FSGNJX,
    FMIN,
    FMAX,
    FEQ,
    FLT,
    FLE,
    FCLASS,
    FCVT_W_S,
    FCVT_WU_S,
    FCVT_S_W,
    FCVT_S_WU,
    CSRRW,
    CSRRS,
    CSRRC
};
enum class ExecutionUnit { IU, LSU, WCU, FPU };

// 保存译码后的运算、执行单元、寄存器依赖与立即数信息。
struct Instr {
    Operation m_operation = Operation::FENCE; // 指定译码后的具体运算。
    ExecutionUnit m_unit = ExecutionUnit::IU; // 指定接收指令的目标执行单元。
    unsigned m_rd = 0; // 指定目的通用寄存器编号。
    unsigned m_rs1 = 0; // 指定第一源通用寄存器编号。
    unsigned m_rs2 = 0; // 指定第二源通用寄存器编号。
    unsigned m_rs3 = 0; // 指定第三源通用寄存器编号。
    unsigned m_rounding_mode = 0; // 保存指令舍入控制编码，7 表示使用线程动态舍入模式。
    bool m_uses_rs3 = false; // 标记第三源寄存器是否参与读取与依赖检查。
    bool m_uses_fcsr = false; // 标记指令是否访问线程浮点状态并需要串行化依赖。
    std::uint32_t m_immediate = 0; // 保存解码后的32位立即数位模式。
    bool m_uses_rs1 = false; // 标记第一源寄存器是否参与读取与依赖检查。
    bool m_uses_rs2 = false; // 标记第二源寄存器是否参与读取与依赖检查。
    bool m_writes_rd = false; // 标记指令是否产生目的寄存器写回。
    bool m_immediate_operand = false; // 标记整数运算是否以立即数替代第二源操作数。
};

// 将解码指令与其发射路径绑定，供指令缓冲和执行单元传递。
struct BufferedInstr {
    ScheduledWarp m_path; // 保留指令所属 warp、PC 与执行线程掩码。
    Instr m_instr; // 保存该路径待执行的解码指令。
};

// 保存发射时的任务参数、解码信息与各线程源操作数快照。
struct IssuedInstr {
    std::uint32_t m_thread_count = 0; // 保存当前任务启动的线程总数。
    std::uint32_t m_param_base = 0; // 保存当前任务参数区的字节基地址。
    BufferedInstr m_decoded; // 保留发射时的解码指令与线程路径。
    std::array<std::uint32_t, WARP_SIZE> m_src1{}; // 按 lane 保存第一源寄存器读取值。
    std::array<std::uint32_t, WARP_SIZE> m_src2{}; // 按 lane 保存第二源寄存器读取值。
    std::array<std::uint32_t, WARP_SIZE> m_src3{}; // 按 lane 保存第三源寄存器读取值。
    std::array<std::uint32_t, WARP_SIZE> m_fcsr{}; // 按 lane 保存执行前读取的浮点控制状态。
};

// 保存指令组待提交的寄存器、浮点状态和路径控制结果。
struct ExecutionResult {
    BufferedInstr m_decoded; // 保留原指令与发射掩码，供写回和路径提交使用。
    std::array<std::uint32_t, WARP_SIZE> m_values{}; // 按 lane 保存待写入目的寄存器的结果。
    std::array<std::uint32_t, WARP_SIZE> m_next_pc{}; // 按 lane 保存执行完成后的指令字节地址。
    ThreadMask m_exit_mask = 0; // 标记本次指令请求退出的线程。
    bool m_writes_fcsr = false; // 标记本次结果是否需要提交浮点控制状态。
    std::array<std::uint32_t, WARP_SIZE> m_fcsr{}; // 按 lane 保存待提交的浮点控制状态。
};
}
