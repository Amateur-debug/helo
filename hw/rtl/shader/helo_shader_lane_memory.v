module helo_shader_lane_memory #(
    parameter integer MAX_BYTES = 4096
) (
    input wire        clk,
    input wire [31:0] capacity_bytes,
    input wire        host_valid,
    input wire        host_write,
    input wire [31:0] host_addr,
    input wire [2:0]  host_size,
    input wire [31:0] host_wdata,
    output wire [31:0] host_rdata,
    output wire        host_error,
    input wire        fetch_valid,
    input wire [31:0] fetch_addr,
    output wire [63:0] fetch_instr,
    output wire        fetch_error,
    input wire [63:0] encoding,
    input wire [31:0] pc,
    input wire [31:0] src1,
    input wire [31:0] src2,
    input wire [31:0] live_mask,
    input wire [31:0] launch_mask,
    input wire        barrier_pending,
    input wire [31:0] barrier_pc,
    output wire       valid,
    output wire       fault,
    output wire       unsupported,
    output wire [7:0] rd,
    output wire [7:0] rs1,
    output wire [7:0] rs2,
    output wire [7:0] rs3,
    output wire [31:0] immediate,
    output wire [2:0] rounding_mode,
    output wire       uses_rs1,
    output wire       uses_rs2,
    output wire       uses_rs3,
    output wire       uses_fcsr,
    output wire [31:0] result,
    output wire [31:0] next_pc,
    output wire       writeback,
    output wire       exit_thread,
    output wire       barrier_arrive
);
    wire data_valid;
    wire data_write;
    wire [31:0] data_addr;
    wire [2:0] data_size;
    wire [31:0] data_wdata;
    wire [31:0] data_rdata;
    wire data_error;

    helo_shader_lane lane (
        .encoding(encoding), .pc(pc), .src1(src1), .src2(src2),
        .live_mask(live_mask), .launch_mask(launch_mask),
        .barrier_pending(barrier_pending), .barrier_pc(barrier_pc),
        .data_rdata(data_rdata), .data_error(data_error),
        .data_valid(data_valid), .data_write(data_write), .data_addr(data_addr),
        .data_size(data_size), .data_wdata(data_wdata),
        .valid(valid), .fault(fault), .unsupported(unsupported),
        .rd(rd), .rs1(rs1), .rs2(rs2), .rs3(rs3),
        .immediate(immediate), .rounding_mode(rounding_mode),
        .uses_rs1(uses_rs1), .uses_rs2(uses_rs2), .uses_rs3(uses_rs3),
        .uses_fcsr(uses_fcsr), .result(result), .next_pc(next_pc),
        .writeback(writeback), .exit_thread(exit_thread), .barrier_arrive(barrier_arrive)
    );
    helo_memory #(.MAX_BYTES(MAX_BYTES)) memory (
        .clk(clk), .capacity_bytes(capacity_bytes),
        .host_valid(host_valid), .host_write(host_write), .host_addr(host_addr),
        .host_size(host_size), .host_wdata(host_wdata),
        .host_rdata(host_rdata), .host_error(host_error),
        .data_valid(data_valid), .data_write(data_write), .data_addr(data_addr),
        .data_size(data_size), .data_wdata(data_wdata),
        .data_rdata(data_rdata), .data_error(data_error),
        .fetch_valid(fetch_valid), .fetch_addr(fetch_addr),
        .fetch_instr(fetch_instr), .fetch_error(fetch_error)
    );
endmodule
