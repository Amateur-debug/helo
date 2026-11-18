module helo_iu (
    input wire [4:0]  operation,
    input wire [31:0] src1,
    input wire [31:0] src2,
    input wire [31:0] immediate,
    input wire        immediate_operand,
    input wire [31:0] pc,
    output reg [31:0] result,
    output reg        illegal
);
    `include "helo_iu_ops.vh"

    reg [31:0] b;
    reg [4:0] shift;
    reg signed [63:0] multiplier_a;
    reg signed [63:0] multiplier_b;
    reg [63:0] product;

    always @* begin
        b = immediate_operand ? immediate : src2;
        shift = b[4:0];
        multiplier_a = operation == IU_MULHU ? {32'b0, src1} : {{32{src1[31]}}, src1};
        multiplier_b = operation == IU_MULH ? {{32{b[31]}}, b} : {32'b0, b};
        product = multiplier_a * multiplier_b;
        result = 0;
        illegal = 1'b0;
        case (operation)
            IU_LUI:    result = immediate;
            IU_AUIPC:  result = pc + immediate;
            IU_ADD:    result = src1 + b;
            IU_SUB:    result = src1 - b;
            IU_SLL:    result = src1 << shift;
            IU_SLT:    result = {31'b0, ($signed(src1) < $signed(b))};
            IU_SLTU:   result = {31'b0, (src1 < b)};
            IU_XOR:    result = src1 ^ b;
            IU_SRL:    result = src1 >> shift;
            IU_SRA:    result = $signed(src1) >>> shift;
            IU_OR:     result = src1 | b;
            IU_AND:    result = src1 & b;
            IU_MUL:    result = product[31:0];
            IU_MULHU, IU_MULH, IU_MULHSU: result = product[63:32];
            IU_DIVU:   result = b == 0 ? 32'hffffffff : src1 / b;
            IU_REMU:   result = b == 0 ? src1 : src1 % b;
            IU_DIV: begin
                if (b == 0) begin
                    result = 32'hffffffff;
                end else if (src1 == 32'h80000000 && b == 32'hffffffff) begin
                    result = 32'h80000000;
                end else begin
                    result = $signed(src1) / $signed(b);
                end
            end
            IU_REM: begin
                if (b == 0) begin
                    result = src1;
                end else if (src1 == 32'h80000000 && b == 32'hffffffff) begin
                    result = 0;
                end else begin
                    result = $signed(src1) % $signed(b);
                end
            end
            IU_MOV: result = src1;
            IU_NOP, IU_FENCE: result = 0;
            default: illegal = 1'b1;
        endcase
    end
endmodule
