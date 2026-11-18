module helo_rop_fragment #(
    parameter integer MAX_BYTES = 4096
) (
    input wire        clk,
    input wire        reset,
    input wire [31:0] capacity_bytes,
    input wire        request_valid,
    output reg        request_ready,
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
    output reg        response_valid,
    output reg        response_pass,
    output reg        response_error,
    output reg        data_valid,
    output reg        data_write,
    output reg [31:0] data_addr,
    output reg [2:0]  data_size,
    output reg [31:0] data_wdata,
    input wire [31:0] data_rdata,
    input wire        data_error
);
    localparam [3:0] IDLE = 4'd0, READ_STENCIL = 4'd1,
                     READ_DEPTH = 4'd2, DECIDE = 4'd3,
                     READ_COLOR = 4'd4, COMMIT = 4'd5,
                     WRITE_STENCIL = 4'd6, WRITE_DEPTH = 4'd7,
                     WRITE_COLOR = 4'd8, COMPLETE = 4'd9;
    reg [3:0] state;
    reg [63:0] pixels;
    reg [63:0] color_end;
    reg [63:0] depth_end;
    reg [63:0] stencil_end;
    reg [31:0] pixel_index;
    reg descriptor_ok;
    reg depth_ok;
    reg [31:0] color_addr;
    reg [31:0] depth_addr;
    reg [31:0] stencil_addr;
    reg [31:0] source_depth;
    reg [31:0] source_color;
    reg [31:0] output_color;
    reg [7:0] old_stencil;
    reg [31:0] old_depth;
    reg saved_depth_test;
    reg saved_depth_write;
    reg [2:0] saved_depth_compare;
    reg saved_stencil_test;
    reg [2:0] saved_stencil_compare;
    reg [7:0] saved_reference;
    reg [7:0] saved_read_mask;
    reg [7:0] saved_write_mask;
    reg [2:0] saved_stencil_fail;
    reg [2:0] saved_depth_fail;
    reg [2:0] saved_pass_op;
    reg saved_color_write;
    reg saved_blend;
    reg stencil_passed;
    reg depth_passed;
    reg fragment_passed;
    reg [2:0] chosen_op;
    reg [7:0] updated_stencil;
    reg [7:0] unmasked_stencil;
    reg [31:0] limit_bytes;

    function automatic fp_less(input [31:0] a, input [31:0] b);
        if (a[30:0] == 0 && b[30:0] == 0) fp_less = 1'b0;
        else if (a[31] != b[31]) fp_less = a[31];
        else fp_less = a[31] ? a[30:0] > b[30:0] : a[30:0] < b[30:0];
    endfunction

    function automatic compare_values(
        input [2:0] operation,
        input [31:0] a,
        input [31:0] b,
        input floating
    );
        reg unordered;
        reg equal_value;
        reg less_value;
        unordered = floating && (a[30:23] == 8'hff || b[30:23] == 8'hff) &&
                    ((a[22:0] != 0 && a[30:23] == 8'hff) ||
                     (b[22:0] != 0 && b[30:23] == 8'hff));
        equal_value = !unordered && (floating ?
            (a == b || (a[30:0] == 0 && b[30:0] == 0)) : a == b);
        less_value = !unordered && (floating ? fp_less(a, b) : a < b);
        case (operation)
            3'd0: compare_values = 1'b0;
            3'd1: compare_values = less_value;
            3'd2: compare_values = equal_value;
            3'd3: compare_values = less_value || equal_value;
            3'd4: compare_values = !unordered && !less_value && !equal_value;
            3'd5: compare_values = !equal_value;
            3'd6: compare_values = !unordered && !less_value;
            default: compare_values = 1'b1;
        endcase
    endfunction

    function automatic [7:0] stencil_value(
        input [2:0] operation,
        input [7:0] previous,
        input [7:0] reference_value
    );
        case (operation)
            3'd0: stencil_value = previous;
            3'd1: stencil_value = 8'd0;
            3'd2: stencil_value = reference_value;
            3'd3: stencil_value = previous == 8'hff ? 8'hff : previous + 8'd1;
            3'd4: stencil_value = previous == 8'h00 ? 8'h00 : previous - 8'd1;
            3'd5: stencil_value = ~previous;
            3'd6: stencil_value = previous + 8'd1;
            default: stencil_value = previous - 8'd1;
        endcase
    endfunction

    // Packed UNORM8 inputs have an odd denominator, so nearest rounding has no ties.
    function automatic [7:0] blend_channel(
        input [7:0] source, input [7:0] destination,
        input [7:0] alpha, input is_alpha
    );
        reg [16:0] numerator;
        reg [16:0] rounded;
        begin
            numerator = {9'd0, source} * {9'd0, alpha};
            if (is_alpha) numerator = {9'd0, alpha} * 17'd255;
            numerator = numerator + {9'd0, destination} *
                        (17'd255 - {9'd0, alpha});
            rounded = (numerator + 17'd127) / 17'd255;
            blend_channel = (|rounded[16:8]) ? 8'hff : rounded[7:0];
        end
    endfunction

    function automatic [31:0] blend_word(
        input [31:0] source, input [31:0] destination
    );
        begin
            blend_word = {
                blend_channel(source[31:24], destination[31:24], source[31:24], 1'b1),
                blend_channel(source[23:16], destination[23:16], source[31:24], 1'b0),
                blend_channel(source[15:8], destination[15:8], source[31:24], 1'b0),
                blend_channel(source[7:0], destination[7:0], source[31:24], 1'b0)
            };
        end
    endfunction

    always @* begin
        limit_bytes = capacity_bytes < MAX_BYTES ? capacity_bytes : MAX_BYTES;
        pixels = width * height;
        color_end = {32'b0, color_base} + pixels * 64'd4;
        depth_end = {32'b0, depth_base} + pixels * 64'd4;
        stencil_end = {32'b0, stencil_base} + pixels;
        pixel_index = {16'b0, fragment_y} * {16'b0, width} + {16'b0, fragment_x};
        descriptor_ok = width != 0 && height != 0 && pixels <= 64'h40000000 &&
                        color_base[1:0] == 0 && color_end <= {32'b0, limit_bytes} &&
                        (!has_depth || (depth_base[1:0] == 0 &&
                          depth_end <= {32'b0, limit_bytes} &&
                          (color_end <= {32'b0, depth_base} ||
                           depth_end <= {32'b0, color_base}))) &&
                        (!has_stencil || (stencil_end <= {32'b0, limit_bytes} &&
                          (color_end <= {32'b0, stencil_base} ||
                           stencil_end <= {32'b0, color_base}) &&
                          (!has_depth || (depth_end <= {32'b0, stencil_base} ||
                                          stencil_end <= {32'b0, depth_base})))) &&
                        (!depth_test && !depth_write || has_depth) &&
                        (!stencil_test || has_stencil);
        depth_ok = (fragment_depth[30:0] == 0 || !fragment_depth[31]) &&
                   fragment_depth[30:0] <= 31'h3f800000;
        request_ready = state == IDLE && !reset;

        stencil_passed = !saved_stencil_test ||
            compare_values(saved_stencil_compare,
                {24'd0, (saved_reference & saved_read_mask)},
                {24'd0, (old_stencil & saved_read_mask)}, 1'b0);
        depth_passed = !saved_depth_test ||
            compare_values(saved_depth_compare, source_depth, old_depth, 1'b1);
        fragment_passed = stencil_passed && depth_passed;
        chosen_op = !stencil_passed ? saved_stencil_fail :
                    !depth_passed ? saved_depth_fail : saved_pass_op;
        unmasked_stencil = stencil_value(chosen_op, old_stencil, saved_reference);
        updated_stencil = (old_stencil & ~saved_write_mask) |
                          (unmasked_stencil & saved_write_mask);

        data_valid = !reset && (state == READ_STENCIL || state == READ_DEPTH ||
                     state == READ_COLOR ||
                     state == WRITE_STENCIL || state == WRITE_DEPTH || state == WRITE_COLOR);
        data_write = state == WRITE_STENCIL || state == WRITE_DEPTH || state == WRITE_COLOR;
        data_addr = 0;
        data_size = 3'd4;
        data_wdata = 0;
        case (state)
            READ_STENCIL: begin data_addr = stencil_addr; data_size = 3'd1; end
            READ_DEPTH: data_addr = depth_addr;
            READ_COLOR: data_addr = color_addr;
            WRITE_STENCIL: begin
                data_addr = stencil_addr;
                data_size = 3'd1;
                data_wdata = {24'd0, updated_stencil};
            end
            WRITE_DEPTH: begin data_addr = depth_addr; data_wdata = source_depth; end
            WRITE_COLOR: begin data_addr = color_addr; data_wdata = output_color; end
            default: begin end
        endcase
    end

    always @(posedge clk) begin
        if (reset) begin
            state <= IDLE;
            response_valid <= 1'b0;
            response_pass <= 1'b0;
            response_error <= 1'b0;
            color_addr <= 0;
            depth_addr <= 0;
            stencil_addr <= 0;
            source_depth <= 0;
            source_color <= 0;
            output_color <= 0;
            old_stencil <= 0;
            old_depth <= 0;
            saved_depth_test <= 1'b0;
            saved_depth_write <= 1'b0;
            saved_depth_compare <= 0;
            saved_stencil_test <= 1'b0;
            saved_stencil_compare <= 0;
            saved_reference <= 0;
            saved_read_mask <= 0;
            saved_write_mask <= 0;
            saved_stencil_fail <= 0;
            saved_depth_fail <= 0;
            saved_pass_op <= 0;
            saved_color_write <= 1'b0;
            saved_blend <= 1'b0;
        end else begin
            response_valid <= 1'b0;
            case (state)
                IDLE: if (request_valid) begin
                    response_pass <= 1'b0;
                    response_error <= !descriptor_ok;
                    if (!descriptor_ok || fragment_x >= width || fragment_y >= height ||
                        !depth_ok) begin
                        state <= COMPLETE;
                    end else begin
                        color_addr <= color_base + pixel_index * 32'd4;
                        depth_addr <= depth_base + pixel_index * 32'd4;
                        stencil_addr <= stencil_base + pixel_index;
                        source_depth <= fragment_depth;
                        source_color <= fragment_rgba;
                        output_color <= fragment_rgba;
                        saved_depth_test <= depth_test;
                        saved_depth_write <= depth_write;
                        saved_depth_compare <= depth_compare;
                        saved_stencil_test <= stencil_test;
                        saved_stencil_compare <= stencil_compare;
                        saved_reference <= stencil_reference;
                        saved_read_mask <= stencil_read_mask;
                        saved_write_mask <= stencil_write_mask;
                        saved_stencil_fail <= stencil_fail;
                        saved_depth_fail <= stencil_depth_fail;
                        saved_pass_op <= stencil_pass;
                        saved_color_write <= color_write;
                        saved_blend <= blend;
                        state <= stencil_test ? READ_STENCIL :
                                 depth_test ? READ_DEPTH : DECIDE;
                    end
                end
                READ_STENCIL: begin
                    if (data_error) begin
                        response_error <= 1'b1;
                        state <= COMPLETE;
                    end else begin
                        old_stencil <= data_rdata[7:0];
                        state <= saved_depth_test ? READ_DEPTH : DECIDE;
                    end
                end
                READ_DEPTH: begin
                    if (data_error) begin
                        response_error <= 1'b1;
                        state <= COMPLETE;
                    end else begin
                        old_depth <= data_rdata;
                        state <= DECIDE;
                    end
                end
                DECIDE: begin
                    response_pass <= fragment_passed;
                    state <= fragment_passed && saved_color_write && saved_blend ?
                             READ_COLOR : COMMIT;
                end
                READ_COLOR: begin
                    if (data_error) begin
                        response_error <= 1'b1;
                        response_pass <= 1'b0;
                        state <= COMPLETE;
                    end else begin
                        output_color <= blend_word(source_color, data_rdata);
                        state <= COMMIT;
                    end
                end
                COMMIT: begin
                    state <= saved_stencil_test ? WRITE_STENCIL :
                             fragment_passed && saved_depth_write ? WRITE_DEPTH :
                             fragment_passed && saved_color_write ? WRITE_COLOR : COMPLETE;
                end
                WRITE_STENCIL: begin
                    if (data_error) begin
                        response_error <= 1'b1;
                        response_pass <= 1'b0;
                        state <= COMPLETE;
                    end else begin
                        state <= fragment_passed && saved_depth_write ? WRITE_DEPTH :
                                 fragment_passed && saved_color_write ? WRITE_COLOR : COMPLETE;
                    end
                end
                WRITE_DEPTH: begin
                    if (data_error) begin
                        response_error <= 1'b1;
                        response_pass <= 1'b0;
                        state <= COMPLETE;
                    end else begin
                        state <= saved_color_write ? WRITE_COLOR : COMPLETE;
                    end
                end
                WRITE_COLOR: begin
                    if (data_error) begin
                        response_error <= 1'b1;
                        response_pass <= 1'b0;
                    end
                    state <= COMPLETE;
                end
                COMPLETE: begin
                    response_valid <= 1'b1;
                    state <= IDLE;
                end
                default: state <= IDLE;
            endcase
        end
    end
endmodule
