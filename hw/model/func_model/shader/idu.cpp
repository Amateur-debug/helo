#include "idu.h"
#include "encoding.h"

namespace helo::func {
namespace {
using encoding::Fields;
using encoding::Format;

enum OperandFlags : unsigned {
    NONE = 0,
    RD = 1,
    RS1 = 2,
    RS2 = 4,
    RS3 = 8,
    IMMEDIATE = 16,
    FCSR = 32,
    ROUNDING = 64,
    CSR = 128,
    CSR_IMMEDIATE = 256,
    OFFSET = 512,
    SHIFT = 1024,
    TRAP_CODE = 2048
};

// 检查未使用操作数与控制字段，并生成执行单元需要的依赖和立即数信息。
Instr make_instruction(
    const Fields& fields, Operation operation, ExecutionUnit unit, unsigned flags) {
    using encoding::require_zero;
    if (!(flags & RD)) {
        require_zero(fields.m_rd);
    }
    if (!(flags & (RS1 | CSR_IMMEDIATE))) {
        require_zero(fields.m_rs1);
    }
    if (!(flags & RS2)) {
        require_zero(fields.m_rs2);
    }
    if (!(flags & RS3)) {
        require_zero(fields.m_rs3);
    }
    if (!(flags & (IMMEDIATE | OFFSET | CSR | TRAP_CODE))) {
        require_zero(fields.m_raw_immediate);
    }
    if (flags & ROUNDING) {
        if (fields.m_ctrl > 7 || fields.m_ctrl == 5 || fields.m_ctrl == 6) {
            throw std::runtime_error("illegal instruction: reserved rounding mode");
        }
    } else {
        require_zero(fields.m_ctrl);
    }
    if ((flags & SHIFT) && fields.m_raw_immediate > 31) {
        throw std::runtime_error("illegal instruction: reserved shift immediate");
    }
    if ((flags & CSR_IMMEDIATE) && fields.m_rs1 > 31) {
        throw std::runtime_error("illegal instruction: reserved CSR immediate source");
    }
    Instr out;
    out.m_operation = operation;
    out.m_unit = unit;
    out.m_writes_rd = (flags & RD) != 0;
    out.m_uses_rs1 = (flags & RS1) != 0;
    out.m_uses_rs2 = (flags & RS2) != 0;
    out.m_uses_rs3 = (flags & RS3) != 0;
    out.m_uses_fcsr = (flags & FCSR) != 0;
    out.m_rd = fields.m_rd;
    out.m_rs1 = fields.m_rs1;
    out.m_rs2 = fields.m_rs2;
    out.m_rs3 = fields.m_rs3;
    out.m_immediate = (flags & (CSR | TRAP_CODE)) ? fields.m_raw_immediate : fields.m_immediate;
    out.m_immediate_operand = (flags & (IMMEDIATE | CSR_IMMEDIATE)) != 0;
    out.m_rounding_mode = fields.m_ctrl;
    return out;
}
}

// 每个功能行明确列出功能码、运算、执行单元和有效操作数字段。
#define HELO_FUNCTION(code, operation, unit, flags)                                                \
    case code:                                                                                     \
        return make_instruction(fields, Operation::operation, ExecutionUnit::unit, flags)

// 先依据主操作码选择格式解析宏，再按指令族内的功能码完成译码。
Instr Idu::decode(std::uint64_t encoding) const {
    Fields fields;
    switch (encoding::classify(encoding)) {
    case Format::R:
        fields = HELO_DECODE_R(encoding);
        break;
    case Format::R4:
        fields = HELO_DECODE_R4(encoding);
        break;
    case Format::I:
        fields = HELO_DECODE_I(encoding);
        break;
    case Format::S:
        fields = HELO_DECODE_S(encoding);
        break;
    case Format::B:
        fields = HELO_DECODE_B(encoding);
        break;
    case Format::U:
        fields = HELO_DECODE_U(encoding);
        break;
    case Format::J:
        fields = HELO_DECODE_J(encoding);
        break;
    }
    switch (HELO_OPCODE(encoding)) {
    case HELO_OPCODE_OP:
        switch (fields.m_funct) {
            HELO_FUNCTION(0x00, ADD, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x01, SUB, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x02, AND, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x03, OR, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x04, XOR, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x05, SLL, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x06, SRL, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x07, SRA, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x08, SLT, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x09, SLTU, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x0a, MOV, IU, RD | RS1);
        }
        break;
    case HELO_OPCODE_OP_IMM:
        switch (fields.m_funct) {
            HELO_FUNCTION(0x00, ADD, IU, RD | RS1 | IMMEDIATE);
            HELO_FUNCTION(0x01, AND, IU, RD | RS1 | IMMEDIATE);
            HELO_FUNCTION(0x02, OR, IU, RD | RS1 | IMMEDIATE);
            HELO_FUNCTION(0x03, XOR, IU, RD | RS1 | IMMEDIATE);
            HELO_FUNCTION(0x04, SLL, IU, RD | RS1 | IMMEDIATE | SHIFT);
            HELO_FUNCTION(0x05, SRL, IU, RD | RS1 | IMMEDIATE | SHIFT);
            HELO_FUNCTION(0x06, SRA, IU, RD | RS1 | IMMEDIATE | SHIFT);
            HELO_FUNCTION(0x07, SLT, IU, RD | RS1 | IMMEDIATE);
            HELO_FUNCTION(0x08, SLTU, IU, RD | RS1 | IMMEDIATE);
        }
        break;
    case HELO_OPCODE_MULDIV:
        switch (fields.m_funct) {
            HELO_FUNCTION(0x00, MUL, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x01, MULH, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x02, MULHSU, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x03, MULHU, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x04, DIV, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x05, DIVU, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x06, REM, IU, RD | RS1 | RS2);
            HELO_FUNCTION(0x07, REMU, IU, RD | RS1 | RS2);
        }
        break;
    case HELO_OPCODE_LOAD:
        switch (fields.m_funct) {
            HELO_FUNCTION(0x00, LB, LSU, RD | RS1 | OFFSET);
            HELO_FUNCTION(0x01, LBU, LSU, RD | RS1 | OFFSET);
            HELO_FUNCTION(0x02, LH, LSU, RD | RS1 | OFFSET);
            HELO_FUNCTION(0x03, LHU, LSU, RD | RS1 | OFFSET);
            HELO_FUNCTION(0x04, LW, LSU, RD | RS1 | OFFSET);
        }
        break;
    case HELO_OPCODE_STORE:
        switch (fields.m_funct) {
            HELO_FUNCTION(0x00, SB, LSU, RS1 | RS2 | OFFSET);
            HELO_FUNCTION(0x01, SH, LSU, RS1 | RS2 | OFFSET);
            HELO_FUNCTION(0x02, SW, LSU, RS1 | RS2 | OFFSET);
        }
        break;
    case HELO_OPCODE_FP:
        switch (fields.m_funct) {
            HELO_FUNCTION(0x00, FADD, FPU, RD | RS1 | FCSR | RS2 | ROUNDING);
            HELO_FUNCTION(0x01, FSUB, FPU, RD | RS1 | FCSR | RS2 | ROUNDING);
            HELO_FUNCTION(0x02, FMUL, FPU, RD | RS1 | FCSR | RS2 | ROUNDING);
            HELO_FUNCTION(0x03, FDIV, FPU, RD | RS1 | FCSR | RS2 | ROUNDING);
            HELO_FUNCTION(0x04, FSQRT, FPU, RD | RS1 | FCSR | ROUNDING);
            HELO_FUNCTION(0x05, FSGNJ, FPU, RD | RS1 | FCSR | RS2);
            HELO_FUNCTION(0x06, FSGNJN, FPU, RD | RS1 | FCSR | RS2);
            HELO_FUNCTION(0x07, FSGNJX, FPU, RD | RS1 | FCSR | RS2);
            HELO_FUNCTION(0x08, FMIN, FPU, RD | RS1 | FCSR | RS2);
            HELO_FUNCTION(0x09, FMAX, FPU, RD | RS1 | FCSR | RS2);
            HELO_FUNCTION(0x0a, FEQ, FPU, RD | RS1 | FCSR | RS2);
            HELO_FUNCTION(0x0b, FLT, FPU, RD | RS1 | FCSR | RS2);
            HELO_FUNCTION(0x0c, FLE, FPU, RD | RS1 | FCSR | RS2);
            HELO_FUNCTION(0x0d, FCLASS, FPU, RD | RS1 | FCSR);
            HELO_FUNCTION(0x0e, FCVT_W_S, FPU, RD | RS1 | FCSR | ROUNDING);
            HELO_FUNCTION(0x0f, FCVT_WU_S, FPU, RD | RS1 | FCSR | ROUNDING);
            HELO_FUNCTION(0x10, FCVT_S_W, FPU, RD | RS1 | FCSR | ROUNDING);
            HELO_FUNCTION(0x11, FCVT_S_WU, FPU, RD | RS1 | FCSR | ROUNDING);
        }
        break;
    case HELO_OPCODE_FMA:
        switch (fields.m_funct) {
            HELO_FUNCTION(0x00, FMADD, FPU, RD | RS1 | RS2 | RS3 | FCSR | ROUNDING);
            HELO_FUNCTION(0x01, FMSUB, FPU, RD | RS1 | RS2 | RS3 | FCSR | ROUNDING);
            HELO_FUNCTION(0x02, FNMSUB, FPU, RD | RS1 | RS2 | RS3 | FCSR | ROUNDING);
            HELO_FUNCTION(0x03, FNMADD, FPU, RD | RS1 | RS2 | RS3 | FCSR | ROUNDING);
        }
        break;
    case HELO_OPCODE_BRANCH:
        switch (fields.m_funct) {
            HELO_FUNCTION(0x00, BEQ, WCU, RS1 | RS2 | OFFSET);
            HELO_FUNCTION(0x01, BNE, WCU, RS1 | RS2 | OFFSET);
            HELO_FUNCTION(0x02, BLT, WCU, RS1 | RS2 | OFFSET);
            HELO_FUNCTION(0x03, BGE, WCU, RS1 | RS2 | OFFSET);
            HELO_FUNCTION(0x04, BLTU, WCU, RS1 | RS2 | OFFSET);
            HELO_FUNCTION(0x05, BGEU, WCU, RS1 | RS2 | OFFSET);
        }
        break;
    case HELO_OPCODE_JUMP:
        switch (fields.m_funct) { HELO_FUNCTION(0x00, JAL, WCU, RD | OFFSET); }
        break;
    case HELO_OPCODE_JUMP_REG:
        switch (fields.m_funct) { HELO_FUNCTION(0x00, JALR, WCU, RD | RS1 | OFFSET); }
        break;
    case HELO_OPCODE_UPPER:
        switch (fields.m_funct) {
            HELO_FUNCTION(0x00, LUI, IU, RD | OFFSET);
            HELO_FUNCTION(0x01, AUIPC, IU, RD | OFFSET);
        }
        break;
    case HELO_OPCODE_SYSTEM:
        switch (fields.m_funct) {
            HELO_FUNCTION(0x00, NOP, IU, NONE);
            HELO_FUNCTION(0x01, EXIT, WCU, NONE);
            HELO_FUNCTION(0x02, TRAP, WCU, TRAP_CODE);
            HELO_FUNCTION(0x03, FENCE, IU, NONE);
            HELO_FUNCTION(0x04, BARW, WCU, NONE);
            HELO_FUNCTION(0x07, CSRRW, FPU, RD | FCSR | CSR | RS1);
            HELO_FUNCTION(0x08, CSRRS, FPU, RD | FCSR | CSR | RS1);
            HELO_FUNCTION(0x09, CSRRC, FPU, RD | FCSR | CSR | RS1);
            HELO_FUNCTION(0x0a, CSRRW, FPU, RD | FCSR | CSR | CSR_IMMEDIATE);
            HELO_FUNCTION(0x0b, CSRRS, FPU, RD | FCSR | CSR | CSR_IMMEDIATE);
            HELO_FUNCTION(0x0c, CSRRC, FPU, RD | FCSR | CSR | CSR_IMMEDIATE);
        }
        break;
    }
    throw std::runtime_error("illegal instruction: unassigned opcode/funct combination");
}

#undef HELO_FUNCTION
}
