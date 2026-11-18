module helo_idu (
    input wire [63:0] encoding,
    output reg        legal,
    output reg [7:0]  family,
    output reg [7:0]  funct,
    output reg [1:0]  unit,
    output reg [4:0]  iu_operation,
    output reg [7:0]  rd,
    output reg [7:0]  rs1,
    output reg [7:0]  rs2,
    output reg [7:0]  rs3,
    output reg [31:0] immediate,
    output reg [2:0]  rounding_mode,
    output reg        uses_rs1,
    output reg        uses_rs2,
    output reg        uses_rs3,
    output reg        uses_fcsr,
    output reg        writes_rd,
    output reg        immediate_operand
);
    `include "helo_iu_ops.vh"

    localparam [1:0] UNIT_IU = 2'd0, UNIT_LSU = 2'd1,
                           UNIT_WCU = 2'd2, UNIT_FPU = 2'd3;
    localparam [11:0] F_RD = 12'h001, F_RS1 = 12'h002,
                            F_RS2 = 12'h004, F_RS3 = 12'h008,
                            F_IMM = 12'h010, F_FCSR = 12'h020,
                            F_RM = 12'h040, F_CSR = 12'h080,
                            F_CSR_IMM = 12'h100, F_OFFSET = 12'h200,
                            F_SHIFT = 12'h400, F_TRAP = 12'h800;
    reg [11:0] flags;
    reg [19:0] raw_immediate;
    reg [31:0] decoded_immediate;
    reg [7:0] ctrl;
    reg format_ok;

    always @* begin
        family = encoding[7:0];
        funct = 0;
        rd = encoding[15:8];
        rs1 = encoding[23:16];
        rs2 = encoding[31:24];
        rs3 = 0;
        ctrl = 0;
        raw_immediate = 0;
        decoded_immediate = 0;
        format_ok = 1'b0;
        // 先确定物理字段，S/B低字段重新映射到源寄存器。
        case (family)
            8'h01, 8'h03, 8'h06: begin
                format_ok = encoding[63:56] == 0 && encoding[39:32] == 0;
                funct = encoding[47:40];
                ctrl = encoding[55:48];
            end
            8'h07: begin
                format_ok = encoding[63:56] == 0;
                funct = encoding[47:40];
                ctrl = encoding[55:48];
                rs3 = encoding[39:32];
            end
            8'h02, 8'h04, 8'h0a, 8'h0c: begin
                format_ok = encoding[63:44] == 0;
                funct = encoding[31:24];
                rs2 = 0;
                raw_immediate = {8'b0, encoding[43:32]};
                decoded_immediate = {{20{encoding[43]}}, encoding[43:32]};
            end
            8'h05: begin
                format_ok = encoding[63:44] == 0;
                funct = encoding[31:24];
                rs2 = encoding[15:8];
                rd = 0;
                raw_immediate = {8'b0, encoding[43:32]};
                decoded_immediate = {{20{encoding[43]}}, encoding[43:32]};
            end
            8'h08: begin
                format_ok = encoding[63:44] == 0;
                funct = encoding[31:24];
                rs2 = encoding[23:16];
                rs1 = encoding[15:8];
                rd = 0;
                raw_immediate = {8'b0, encoding[43:32]};
                decoded_immediate = {{19{encoding[43]}}, encoding[43:32], 1'b0};
            end
            8'h09, 8'h0b: begin
                format_ok = encoding[63:52] == 0 && encoding[31:24] == 0;
                funct = encoding[23:16];
                rs1 = 0;
                rs2 = 0;
                raw_immediate = encoding[51:32];
                if (family == 8'h09) begin
                    decoded_immediate = {{11{encoding[51]}}, encoding[51:32], 1'b0};
                end else begin
                    decoded_immediate = {encoding[51:32], 12'b0};
                end
            end
            default: begin end
        endcase

        legal = format_ok;
        flags = 0;
        unit = UNIT_IU;
        iu_operation = IU_NOP;
        // 每项给出有效操作数字段；其余字段在末尾统一检查为零。
        case (family)
            8'h01: begin
                flags = F_RD | F_RS1 | F_RS2;
                case (funct)
                    8'h00: iu_operation = IU_ADD;
                    8'h01: iu_operation = IU_SUB;
                    8'h02: iu_operation = IU_AND;
                    8'h03: iu_operation = IU_OR;
                    8'h04: iu_operation = IU_XOR;
                    8'h05: iu_operation = IU_SLL;
                    8'h06: iu_operation = IU_SRL;
                    8'h07: iu_operation = IU_SRA;
                    8'h08: iu_operation = IU_SLT;
                    8'h09: iu_operation = IU_SLTU;
                    8'h0a: begin
                        iu_operation = IU_MOV;
                        flags = F_RD | F_RS1;
                    end
                    default: legal = 1'b0;
                endcase
            end
            8'h02: begin
                flags = F_RD | F_RS1 | F_IMM;
                case (funct)
                    8'h00: iu_operation = IU_ADD;
                    8'h01: iu_operation = IU_AND;
                    8'h02: iu_operation = IU_OR;
                    8'h03: iu_operation = IU_XOR;
                    8'h04: iu_operation = IU_SLL;
                    8'h05: iu_operation = IU_SRL;
                    8'h06: iu_operation = IU_SRA;
                    8'h07: iu_operation = IU_SLT;
                    8'h08: iu_operation = IU_SLTU;
                    default: legal = 1'b0;
                endcase
                if (funct == 8'h04 || funct == 8'h05 || funct == 8'h06) begin
                    flags = flags | F_SHIFT;
                end
            end
            8'h03: begin
                flags = F_RD | F_RS1 | F_RS2;
                case (funct)
                    8'h00: iu_operation = IU_MUL;
                    8'h01: iu_operation = IU_MULH;
                    8'h02: iu_operation = IU_MULHSU;
                    8'h03: iu_operation = IU_MULHU;
                    8'h04: iu_operation = IU_DIV;
                    8'h05: iu_operation = IU_DIVU;
                    8'h06: iu_operation = IU_REM;
                    8'h07: iu_operation = IU_REMU;
                    default: legal = 1'b0;
                endcase
            end
            8'h04: begin
                unit = UNIT_LSU;
                flags = F_RD | F_RS1 | F_OFFSET;
                if (funct > 8'h04) legal = 1'b0;
            end
            8'h05: begin
                unit = UNIT_LSU;
                flags = F_RS1 | F_RS2 | F_OFFSET;
                if (funct > 8'h02) legal = 1'b0;
            end
            8'h06: begin
                unit = UNIT_FPU;
                flags = F_RD | F_RS1 | F_FCSR;
                case (funct)
                    8'h00, 8'h01, 8'h02, 8'h03: flags = flags | F_RS2 | F_RM;
                    8'h04, 8'h0e, 8'h0f, 8'h10, 8'h11: flags = flags | F_RM;
                    8'h05, 8'h06, 8'h07, 8'h08, 8'h09, 8'h0a, 8'h0b, 8'h0c:
                        flags = flags | F_RS2;
                    8'h0d: begin end
                    default: legal = 1'b0;
                endcase
            end
            8'h07: begin
                unit = UNIT_FPU;
                flags = F_RD | F_RS1 | F_RS2 | F_RS3 | F_FCSR | F_RM;
                if (funct > 8'h03) legal = 1'b0;
            end
            8'h08: begin
                unit = UNIT_WCU;
                flags = F_RS1 | F_RS2 | F_OFFSET;
                if (funct > 8'h05) legal = 1'b0;
            end
            8'h09: begin
                unit = UNIT_WCU;
                flags = F_RD | F_OFFSET;
                if (funct != 0) legal = 1'b0;
            end
            8'h0a: begin
                unit = UNIT_WCU;
                flags = F_RD | F_RS1 | F_OFFSET;
                if (funct != 0) legal = 1'b0;
            end
            8'h0b: begin
                flags = F_RD | F_OFFSET;
                case (funct)
                    8'h00: iu_operation = IU_LUI;
                    8'h01: iu_operation = IU_AUIPC;
                    default: legal = 1'b0;
                endcase
            end
            8'h0c: begin
                case (funct)
                    8'h00: iu_operation = IU_NOP;
                    8'h01: unit = UNIT_WCU;
                    8'h02: begin
                        unit = UNIT_WCU;
                        flags = F_TRAP;
                    end
                    8'h03: iu_operation = IU_FENCE;
                    8'h04: unit = UNIT_WCU;
                    8'h07, 8'h08, 8'h09: begin
                        unit = UNIT_FPU;
                        flags = F_RD | F_FCSR | F_CSR | F_RS1;
                    end
                    8'h0a, 8'h0b, 8'h0c: begin
                        unit = UNIT_FPU;
                        flags = F_RD | F_FCSR | F_CSR | F_CSR_IMM;
                    end
                    default: legal = 1'b0;
                endcase
            end
            default: legal = 1'b0;
        endcase

        if ((flags & F_RD) == 0 && rd != 0) legal = 1'b0;
        if ((flags & (F_RS1 | F_CSR_IMM)) == 0 && rs1 != 0) legal = 1'b0;
        if ((flags & F_RS2) == 0 && rs2 != 0) legal = 1'b0;
        if ((flags & F_RS3) == 0 && rs3 != 0) legal = 1'b0;
        if ((flags & (F_IMM | F_OFFSET | F_CSR | F_TRAP)) == 0 && raw_immediate != 0)
            legal = 1'b0;
        if ((flags & F_RM) == 0 && ctrl != 0) legal = 1'b0;
        if ((flags & F_RM) != 0 && (ctrl > 7 || ctrl == 5 || ctrl == 6)) legal = 1'b0;
        if ((flags & F_SHIFT) != 0 && raw_immediate > 31) legal = 1'b0;
        if ((flags & F_CSR_IMM) != 0 && rs1 > 31) legal = 1'b0;

        immediate = (flags & (F_CSR | F_TRAP)) != 0 ? {12'b0, raw_immediate} : decoded_immediate;
        rounding_mode = ctrl[2:0];
        uses_rs1 = (flags & F_RS1) != 0;
        uses_rs2 = (flags & F_RS2) != 0;
        uses_rs3 = (flags & F_RS3) != 0;
        uses_fcsr = (flags & F_FCSR) != 0;
        writes_rd = (flags & F_RD) != 0;
        immediate_operand = (flags & (F_IMM | F_CSR_IMM)) != 0;
    end
endmodule
