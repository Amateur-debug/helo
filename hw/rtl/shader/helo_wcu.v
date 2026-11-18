module helo_wcu (
    input wire [7:0]  family,
    input wire [7:0]  funct,
    input wire [31:0] pc,
    input wire [31:0] src1,
    input wire [31:0] src2,
    input wire [31:0] immediate,
    input wire [31:0] live_mask,
    input wire [31:0] launch_mask,
    input wire        barrier_pending,
    input wire [31:0] barrier_pc,
    output reg [31:0] next_pc,
    output reg [31:0] link_value,
    output reg        link_write,
    output reg        exit_thread,
    output reg        barrier_arrive,
    output reg        fault,
    output reg        illegal
);
    reg taken;
    always @* begin
        next_pc = pc + 32'd8;
        link_value = pc + 32'd8;
        link_write = 1'b0;
        exit_thread = 1'b0;
        barrier_arrive = 1'b0;
        fault = 1'b0;
        illegal = 1'b0;
        taken = 1'b0;
        case (family)
            8'h08: begin
                case (funct)
                    8'h00: taken = src1 == src2;
                    8'h01: taken = src1 != src2;
                    8'h02: taken = $signed(src1) < $signed(src2);
                    8'h03: taken = $signed(src1) >= $signed(src2);
                    8'h04: taken = src1 < src2;
                    8'h05: taken = src1 >= src2;
                    default: illegal = 1'b1;
                endcase
                if (taken) next_pc = pc + immediate;
            end
            8'h09: begin
                if (funct != 0) illegal = 1'b1;
                next_pc = pc + immediate;
                link_write = 1'b1;
            end
            8'h0a: begin
                if (funct != 0) illegal = 1'b1;
                next_pc = src1 + immediate;
                link_write = 1'b1;
            end
            8'h0c: begin
                case (funct)
                    8'h01: begin
                        exit_thread = 1'b1;
                        if (barrier_pending) fault = 1'b1;
                    end
                    8'h02: fault = 1'b1;
                    8'h04: begin
                        barrier_arrive = 1'b1;
                        next_pc = pc;
                        if (live_mask != launch_mask ||
                            (barrier_pending && barrier_pc != pc)) fault = 1'b1;
                    end
                    default: illegal = 1'b1;
                endcase
            end
            default: illegal = 1'b1;
        endcase
        if (next_pc[2:0] != 0) fault = 1'b1;
        if (fault || illegal) begin
            link_write = 1'b0;
            exit_thread = 1'b0;
            barrier_arrive = 1'b0;
        end
    end
endmodule
