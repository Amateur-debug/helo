module helo_regfile #(
    parameter integer MAX_WARPS = 2,
    parameter integer GPR_BYTES = 8192
) (
    input wire        clk,
    input wire        reset,
    input wire        clear,
    input wire        configure_valid,
    input wire [7:0]  configure_warps,
    input wire [8:0]  configure_registers,
    output reg        configure_error,
    output reg [7:0]  active_warps,
    output reg [8:0]  register_count,
    input wire        read_valid,
    input wire [7:0]  read_warp,
    input wire [4:0]  read_lane,
    input wire [7:0]  read_reg,
    output reg [31:0] read_data,
    output reg [7:0]  read_fcsr,
    output reg        read_error,
    output reg        fcsr_read_error,
    input wire        write_valid,
    input wire [7:0]  write_warp,
    input wire [4:0]  write_lane,
    input wire [7:0]  write_reg,
    input wire [31:0] write_data,
    input wire        write_fcsr_valid,
    input wire [7:0]  write_fcsr_data,
    output reg        write_error,
    output reg        fcsr_write_error
);
    localparam integer SLOTS = MAX_WARPS * 32;
    localparam integer WORDS = GPR_BYTES > 0 ? GPR_BYTES / 4 : 1;
    reg [31:0] gpr [0:WORDS-1];
    reg [7:0] fcsr [0:SLOTS-1];
    reg [63:0] required_bytes;
    integer slot;
    integer word_index;

    // 按任务声明将warp/lane/非零寄存器映射到紧凑GPR存储。
    function automatic [31:0] gpr_index(
        input [7:0] warp, input [4:0] lane, input [7:0] reg_index);
        gpr_index = (warp * 32 + {27'b0, lane}) *
                    ({23'b0, register_count} - 32'd1) + {24'b0, reg_index} - 32'd1;
    endfunction

    always @* begin
        required_bytes = configure_warps * 64'd128 *
                         ({55'b0, configure_registers} - 64'd1);
        configure_error = configure_valid &&
            (configure_registers == 0 || configure_registers > 9'd256 ||
             configure_warps > MAX_WARPS[7:0] ||
             required_bytes > {32'b0, GPR_BYTES[31:0]});
        fcsr_read_error = read_valid && read_warp >= active_warps;
        read_error = fcsr_read_error ||
                     (read_valid && {1'b0, read_reg} >= register_count);
        fcsr_write_error = write_fcsr_valid && write_warp >= active_warps;
        write_error = write_valid &&
                      (write_warp >= active_warps || {1'b0, write_reg} >= register_count);
        read_data = 0;
        read_fcsr = 0;
        if (read_valid && !fcsr_read_error) begin
            read_fcsr = fcsr[read_warp * 32 + {27'b0, read_lane}];
        end
        if (read_valid && !read_error && read_reg != 0) begin
            read_data = gpr[gpr_index(read_warp, read_lane, read_reg)];
        end
    end

    always @(posedge clk) begin
        if (reset) begin
            active_warps <= 0;
            register_count <= 9'd256;
        end else if (configure_valid && !configure_error && !clear) begin
            active_warps <= configure_warps;
            register_count <= configure_registers;
        end
        // 复位和成功任务切换均清零，普通clear保留原任务布局。
        if (reset || clear || (configure_valid && !configure_error)) begin
            for (slot = 0; slot < SLOTS; slot = slot + 1) begin
                fcsr[slot] <= 0;
            end
            for (word_index = 0; word_index < WORDS; word_index = word_index + 1) begin
                gpr[word_index] <= 0;
            end
        end else begin
            if (write_valid && !write_error && write_reg != 0) begin
                gpr[gpr_index(write_warp, write_lane, write_reg)] <= write_data;
            end
            if (write_fcsr_valid && !fcsr_write_error) begin
                fcsr[write_warp * 32 + {27'b0, write_lane}] <= write_fcsr_data;
            end
        end
    end
endmodule
