module helo_shader_core #(
    parameter integer MAX_BYTES = 4096
) (
    input wire        clk,
    input wire        reset,
    input wire [31:0] capacity_bytes,
    input wire        host_valid,
    input wire        host_write,
    input wire [31:0] host_addr,
    input wire [2:0]  host_size,
    input wire [31:0] host_wdata,
    output wire [31:0] host_rdata,
    output wire        host_error,
    input wire        launch_valid,
    input wire [31:0] launch_entry,
    input wire [8:0]  launch_registers,
    output reg        launch_error,
    output reg        running,
    output reg        completed,
    output reg        fault,
    output reg        retired,
    output reg [31:0] pc,
    output wire [7:0] decoded_rs3,
    output wire [31:0] decoded_immediate,
    output wire [2:0] decoded_rounding_mode,
    input wire        debug_read_valid,
    input wire        debug_write_valid,
    input wire [7:0]  debug_reg,
    input wire [31:0] debug_wdata,
    output reg [31:0] debug_rdata,
    output reg [7:0]  debug_fcsr,
    output reg        debug_error
);
    localparam [2:0] IDLE = 3'd0, FETCH = 3'd1, READ1 = 3'd2,
                     READ2 = 3'd3, EXECUTE = 3'd4, HALT = 3'd5, FAILED = 3'd6;
    reg [2:0] state;
    reg launch_attempt;
    reg configure_valid;
    wire configure_error;
    wire [7:0] active_warps;
    wire [8:0] register_count;
    reg fetch_valid;
    reg [31:0] fetch_addr;
    wire [63:0] fetch_instr;
    wire fetch_error;
    reg [63:0] encoding;
    reg [31:0] src1;
    reg [31:0] src2;
    wire [7:0] rd;
    wire [7:0] rs1;
    wire [7:0] rs2;
    wire uses_rs1;
    wire uses_rs2;
    wire uses_rs3;
    wire uses_fcsr;
    wire lane_valid;
    wire lane_fault;
    wire lane_unsupported;
    wire [31:0] lane_result;
    wire [31:0] lane_next_pc;
    wire lane_writeback;
    wire lane_exit;
    wire lane_barrier;
    wire data_valid;
    wire data_write;
    wire [31:0] data_addr;
    wire [2:0] data_size;
    wire [31:0] data_wdata;
    wire [31:0] data_rdata;
    wire data_error;
    reg read_valid;
    reg [7:0] read_reg;
    wire [31:0] read_data;
    wire [7:0] read_fcsr;
    wire read_error;
    wire fcsr_read_error;
    reg write_valid;
    reg [7:0] write_reg;
    reg [31:0] write_data;
    wire write_error;
    wire fcsr_write_error;
    reg debug_ready;
    reg execute_ready;

    always @* begin
        debug_ready = !reset && (state == IDLE || state == HALT || state == FAILED);
        launch_attempt = launch_valid && debug_ready;
        configure_valid = launch_attempt && !fetch_error;
        launch_error = launch_valid && (!debug_ready || fetch_error || configure_error);
        running = state == FETCH || state == READ1 || state == READ2 || state == EXECUTE;
        completed = state == HALT;
        fault = state == FAILED;
        fetch_valid = state == FETCH || launch_attempt;
        fetch_addr = launch_attempt ? launch_entry : pc;
        execute_ready = active_warps == 8'd1 && lane_valid && !lane_fault &&
                        !lane_unsupported && !uses_rs3 && !uses_fcsr &&
                        {1'b0, rd} < register_count;

        read_valid = (state == READ1 && uses_rs1) || (state == READ2 && uses_rs2) ||
                     (debug_ready && debug_read_valid);
        read_reg = state == READ1 ? rs1 : state == READ2 ? rs2 : debug_reg;
        debug_rdata = debug_ready ? read_data : 0;
        debug_fcsr = debug_ready ? read_fcsr : 0;
        write_valid = (state == EXECUTE && execute_ready && lane_writeback) ||
                      (debug_ready && debug_write_valid && !launch_attempt);
        write_reg = state == EXECUTE ? rd : debug_reg;
        write_data = state == EXECUTE ? lane_result : debug_wdata;
        debug_error = (debug_read_valid && (!debug_ready || read_error || fcsr_read_error)) ||
                      (debug_write_valid && (!debug_ready || launch_attempt || write_error ||
                                             fcsr_write_error));
    end

    helo_regfile #(.MAX_WARPS(1), .GPR_BYTES(32768)) registers (
        .clk(clk), .reset(reset), .clear(1'b0),
        .configure_valid(configure_valid), .configure_warps(8'd1),
        .configure_registers(launch_registers), .configure_error(configure_error),
        .active_warps(active_warps), .register_count(register_count),
        .read_valid(read_valid), .read_warp(8'd0), .read_lane(5'd0),
        .read_reg(read_reg), .read_data(read_data), .read_fcsr(read_fcsr),
        .read_error(read_error), .fcsr_read_error(fcsr_read_error),
        .write_valid(write_valid), .write_warp(8'd0), .write_lane(5'd0),
        .write_reg(write_reg), .write_data(write_data),
        .write_fcsr_valid(1'b0), .write_fcsr_data(8'd0),
        .write_error(write_error), .fcsr_write_error(fcsr_write_error)
    );
    helo_shader_lane lane (
        .encoding(encoding), .pc(pc), .src1(src1), .src2(src2),
        .live_mask(32'd1), .launch_mask(32'd1),
        .barrier_pending(1'b0), .barrier_pc(32'd0),
        .data_rdata(data_rdata), .data_error(data_error),
        .data_valid(data_valid), .data_write(data_write), .data_addr(data_addr),
        .data_size(data_size), .data_wdata(data_wdata),
        .valid(lane_valid), .fault(lane_fault), .unsupported(lane_unsupported),
        .rd(rd), .rs1(rs1), .rs2(rs2), .rs3(decoded_rs3),
        .immediate(decoded_immediate), .rounding_mode(decoded_rounding_mode),
        .uses_rs1(uses_rs1), .uses_rs2(uses_rs2),
        .uses_rs3(uses_rs3), .uses_fcsr(uses_fcsr),
        .result(lane_result), .next_pc(lane_next_pc), .writeback(lane_writeback),
        .exit_thread(lane_exit), .barrier_arrive(lane_barrier)
    );
    helo_memory #(.MAX_BYTES(MAX_BYTES)) memory (
        .clk(clk), .capacity_bytes(capacity_bytes),
        .host_valid(host_valid), .host_write(host_write), .host_addr(host_addr),
        .host_size(host_size), .host_wdata(host_wdata),
        .host_rdata(host_rdata), .host_error(host_error),
        .data_valid(data_valid && state == EXECUTE && {1'b0, rd} < register_count && !reset),
        .data_write(data_write), .data_addr(data_addr), .data_size(data_size),
        .data_wdata(data_wdata), .data_rdata(data_rdata), .data_error(data_error),
        .fetch_valid(fetch_valid), .fetch_addr(fetch_addr),
        .fetch_instr(fetch_instr), .fetch_error(fetch_error)
    );

    // 取指、双源读取与提交分时进行，访存只在最后一级打开。
    always @(posedge clk) begin
        if (reset) begin
            state <= IDLE;
            pc <= 0;
            encoding <= 0;
            src1 <= 0;
            src2 <= 0;
            retired <= 1'b0;
        end else begin
            retired <= 1'b0;
            if (launch_attempt && !launch_error) begin
                pc <= launch_entry;
                state <= FETCH;
            end else begin
                case (state)
                    FETCH: begin
                        if (fetch_error) begin
                            state <= FAILED;
                        end else begin
                            encoding <= fetch_instr;
                            state <= READ1;
                        end
                    end
                    READ1: begin
                        if (read_error || fcsr_read_error) begin
                            state <= FAILED;
                        end else begin
                            src1 <= uses_rs1 ? read_data : 0;
                            state <= READ2;
                        end
                    end
                    READ2: begin
                        if (read_error || fcsr_read_error) begin
                            state <= FAILED;
                        end else begin
                            src2 <= uses_rs2 ? read_data : 0;
                            state <= EXECUTE;
                        end
                    end
                    EXECUTE: begin
                        if (!execute_ready || write_error) begin
                            state <= FAILED;
                        end else begin
                            retired <= 1'b1;
                            pc <= lane_barrier ? pc + 32'd8 : lane_next_pc;
                            state <= lane_exit ? HALT : FETCH;
                        end
                    end
                    default: begin end
                endcase
            end
        end
    end
endmodule
