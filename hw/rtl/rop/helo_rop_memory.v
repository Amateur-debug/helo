module helo_rop_memory #(
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
    input wire        fetch_valid,
    input wire [31:0] fetch_addr,
    output wire [63:0] fetch_instr,
    output wire        fetch_error,
    input wire        request_valid,
    output wire       request_ready,
    input wire [15:0] width,
    input wire [15:0] height,
    input wire [31:0] color_base,
    input wire [31:0] depth_base,
    input wire [31:0] stencil_base,
    input wire        has_depth,
    input wire        has_stencil,
    input wire        depth_test,
    input wire        depth_write,
    input wire [2:0]  depth_compare,
    input wire        stencil_test,
    input wire [2:0]  stencil_compare,
    input wire [7:0]  stencil_reference,
    input wire [7:0]  stencil_read_mask,
    input wire [7:0]  stencil_write_mask,
    input wire [2:0]  stencil_fail,
    input wire [2:0]  stencil_depth_fail,
    input wire [2:0]  stencil_pass,
    input wire        color_write,
    input wire        blend,
    input wire [15:0] fragment_x,
    input wire [15:0] fragment_y,
    input wire [31:0] fragment_depth,
    input wire [31:0] fragment_rgba,
    output wire       response_valid,
    output wire       response_pass,
    output wire       response_error
);
    wire data_valid;
    wire data_write;
    wire [31:0] data_addr;
    wire [2:0] data_size;
    wire [31:0] data_wdata;
    wire [31:0] data_rdata;
    wire data_error;
    helo_rop_fragment #(.MAX_BYTES(MAX_BYTES)) fragment (
        .clk(clk), .reset(reset), .capacity_bytes(capacity_bytes),
        .request_valid(request_valid), .request_ready(request_ready),
        .width(width), .height(height), .color_base(color_base),
        .depth_base(depth_base), .stencil_base(stencil_base),
        .has_depth(has_depth), .has_stencil(has_stencil),
        .depth_test(depth_test), .depth_write(depth_write),
        .depth_compare(depth_compare), .stencil_test(stencil_test),
        .stencil_compare(stencil_compare), .stencil_reference(stencil_reference),
        .stencil_read_mask(stencil_read_mask), .stencil_write_mask(stencil_write_mask),
        .stencil_fail(stencil_fail), .stencil_depth_fail(stencil_depth_fail),
        .stencil_pass(stencil_pass), .color_write(color_write), .blend(blend),
        .fragment_x(fragment_x), .fragment_y(fragment_y),
        .fragment_depth(fragment_depth), .fragment_rgba(fragment_rgba),
        .response_valid(response_valid), .response_pass(response_pass),
        .response_error(response_error), .data_valid(data_valid),
        .data_write(data_write), .data_addr(data_addr), .data_size(data_size),
        .data_wdata(data_wdata), .data_rdata(data_rdata), .data_error(data_error)
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
