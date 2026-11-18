module helo_lsu (
    input wire        enable,
    input wire [7:0]  family,
    input wire [7:0]  funct,
    input wire [31:0] pc,
    input wire [31:0] src1,
    input wire [31:0] src2,
    input wire [31:0] immediate,
    input wire [31:0] data_rdata,
    input wire        data_error,
    output reg        data_valid,
    output reg        data_write,
    output reg [31:0] data_addr,
    output reg [2:0]  data_size,
    output reg [31:0] data_wdata,
    output reg [31:0] result,
    output reg [31:0] next_pc,
    output reg        writeback,
    output reg        fault
);
    reg operation_ok;
    reg signed_load;

    always @* begin
        data_valid = 1'b0;
        data_write = 1'b0;
        data_addr = src1 + immediate;
        data_size = 0;
        data_wdata = src2;
        result = 0;
        next_pc = pc;
        writeback = 1'b0;
        signed_load = 1'b0;
        operation_ok = 1'b1;
        case (family)
            8'h04: begin
                case (funct)
                    8'h00: begin data_size = 3'd1; signed_load = 1'b1; end
                    8'h01: begin data_size = 3'd2; signed_load = 1'b1; end
                    8'h02: data_size = 3'd4;
                    8'h03: data_size = 3'd1;
                    8'h04: data_size = 3'd2;
                    default: operation_ok = 1'b0;
                endcase
            end
            8'h05: begin
                data_write = 1'b1;
                case (funct)
                    8'h00: data_size = 3'd1;
                    8'h01: data_size = 3'd2;
                    8'h02: data_size = 3'd4;
                    default: operation_ok = 1'b0;
                endcase
            end
            default: operation_ok = 1'b0;
        endcase
        data_valid = enable && operation_ok;
        fault = enable && (!operation_ok || data_error);
        if (data_valid && !data_error) begin
            next_pc = pc + 32'd8;
            writeback = !data_write;
            if (!data_write) begin
                case (data_size)
                    3'd1: result = signed_load ? {{24{data_rdata[7]}}, data_rdata[7:0]} :
                                                  {24'b0, data_rdata[7:0]};
                    3'd2: result = signed_load ? {{16{data_rdata[15]}}, data_rdata[15:0]} :
                                                  {16'b0, data_rdata[15:0]};
                    3'd4: result = data_rdata;
                    default: result = 0;
                endcase
            end
        end
    end
endmodule
