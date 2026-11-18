module helo_shader_lane (
    input wire [63:0] encoding,
    input wire [31:0] pc,
    input wire [31:0] src1,
    input wire [31:0] src2,
    input wire [31:0] live_mask,
    input wire [31:0] launch_mask,
    input wire        barrier_pending,
    input wire [31:0] barrier_pc,
    input wire [31:0] data_rdata,
    input wire        data_error,
    output wire       data_valid,
    output wire       data_write,
    output wire [31:0] data_addr,
    output wire [2:0] data_size,
    output wire [31:0] data_wdata,
    output reg        valid,
    output reg        fault,
    output reg        unsupported,
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
    output reg [31:0] result,
    output reg [31:0] next_pc,
    output reg        writeback,
    output reg        exit_thread,
    output reg        barrier_arrive
);
    wire legal;
    wire [7:0] family;
    wire [7:0] funct;
    wire [1:0] unit;
    wire [4:0] iu_operation;
    wire immediate_operand;
    wire writes_rd;
    wire [31:0] iu_result;
    wire iu_illegal;
    wire [31:0] link_value;
    wire [31:0] wcu_next_pc;
    wire wcu_link;
    wire wcu_exit;
    wire wcu_barrier;
    wire wcu_fault;
    wire wcu_illegal;
    wire [31:0] lsu_result;
    wire [31:0] lsu_next_pc;
    wire lsu_writeback;
    wire lsu_fault;

    helo_idu idu (
        .encoding(encoding), .legal(legal), .family(family), .funct(funct),
        .unit(unit), .iu_operation(iu_operation), .rd(rd), .rs1(rs1),
        .rs2(rs2), .rs3(rs3), .immediate(immediate), .rounding_mode(rounding_mode),
        .uses_rs1(uses_rs1), .uses_rs2(uses_rs2), .uses_rs3(uses_rs3),
        .uses_fcsr(uses_fcsr),
        .writes_rd(writes_rd), .immediate_operand(immediate_operand)
    );
    helo_iu iu (
        .operation(iu_operation), .src1(src1), .src2(src2),
        .immediate(immediate), .immediate_operand(immediate_operand),
        .pc(pc), .result(iu_result), .illegal(iu_illegal)
    );
    helo_wcu wcu (
        .family(family), .funct(funct), .pc(pc), .src1(src1), .src2(src2),
        .immediate(immediate), .live_mask(live_mask), .launch_mask(launch_mask),
        .barrier_pending(barrier_pending), .barrier_pc(barrier_pc),
        .next_pc(wcu_next_pc), .link_value(link_value), .link_write(wcu_link),
        .exit_thread(wcu_exit), .barrier_arrive(wcu_barrier),
        .fault(wcu_fault), .illegal(wcu_illegal)
    );
    helo_lsu lsu (
        .enable(legal && unit == 2'd1), .family(family), .funct(funct),
        .pc(pc), .src1(src1), .src2(src2), .immediate(immediate),
        .data_rdata(data_rdata), .data_error(data_error),
        .data_valid(data_valid), .data_write(data_write), .data_addr(data_addr),
        .data_size(data_size), .data_wdata(data_wdata), .result(lsu_result),
        .next_pc(lsu_next_pc), .writeback(lsu_writeback), .fault(lsu_fault)
    );

    always @* begin
        unsupported = legal && unit == 2'd3;
        fault = !legal || (unit == 2'd0 && iu_illegal) ||
                (unit == 2'd1 && lsu_fault) ||
                (unit == 2'd2 && (wcu_fault || wcu_illegal));
        valid = !fault && !unsupported;
        result = 0;
        next_pc = pc;
        writeback = 1'b0;
        exit_thread = 1'b0;
        barrier_arrive = 1'b0;
        if (valid) begin
            if (unit == 2'd0) begin
                result = iu_result;
                next_pc = pc + 32'd8;
                writeback = writes_rd;
            end else if (unit == 2'd1) begin
                result = lsu_result;
                next_pc = lsu_next_pc;
                writeback = lsu_writeback;
            end else if (unit == 2'd2) begin
                result = link_value;
                next_pc = wcu_next_pc;
                writeback = wcu_link;
                exit_thread = wcu_exit;
                barrier_arrive = wcu_barrier;
            end
        end
    end
endmodule
