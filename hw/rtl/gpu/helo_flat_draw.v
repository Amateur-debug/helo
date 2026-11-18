module helo_flat_draw #(
    parameter integer MAX_BYTES = 4096
) (
    input wire               clk,
    input wire               reset,
    input wire [31:0]        capacity_bytes,
    input wire               host_valid,
    input wire               host_write,
    input wire [31:0]        host_addr,
    input wire [2:0]         host_size,
    input wire [31:0]        host_wdata,
    output wire [31:0]       host_rdata,
    output wire              host_error,
    input wire               fetch_valid,
    input wire [31:0]        fetch_addr,
    output wire [63:0]       fetch_instr,
    output wire              fetch_error,
    input wire               draw_valid,
    output reg               draw_ready,
    input wire [15:0]        width,
    input wire [15:0]        height,
    input wire [31:0]        color_base,
    input wire [31:0]        depth_base,
    input wire signed [19:0] x0,
    input wire signed [19:0] y0,
    input wire signed [19:0] x1,
    input wire signed [19:0] y1,
    input wire signed [19:0] x2,
    input wire signed [19:0] y2,
    input wire [31:0]        flat_depth,
    input wire [31:0]        flat_rgba,
    input wire               blend,
    input wire               interpolate_color,
    input wire [31:0]        rgba0,
    input wire [31:0]        rgba1,
    input wire [31:0]        rgba2,
    input wire               interpolate_depth,
    input wire [16:0]        depth0_q16,
    input wire [16:0]        depth1_q16,
    input wire [16:0]        depth2_q16,
    input wire               texture_enable,
    input wire [31:0]        texture_base,
    input wire [15:0]        texture_width,
    input wire [15:0]        texture_height,
    input wire signed [31:0] texture_u_q16,
    input wire signed [31:0] texture_v_q16,
    input wire               texture_repeat_u,
    input wire               texture_repeat_v,
    input wire               texture_interpolate_uv,
    input wire [16:0]        inv_w0_q16,
    input wire [16:0]        inv_w1_q16,
    input wire [16:0]        inv_w2_q16,
    input wire signed [31:0] u0_q16,
    input wire signed [31:0] u1_q16,
    input wire signed [31:0] u2_q16,
    input wire signed [31:0] v0_q16,
    input wire signed [31:0] v1_q16,
    input wire signed [31:0] v2_q16,
    output reg               draw_done,
    output reg               draw_error,
    output reg [31:0]        covered_count,
    output reg [31:0]        passed_count,
    output wire              coverage_busy,
    output wire [63:0]       coverage_edge0,
    output wire [63:0]       coverage_edge1,
    output wire [63:0]       coverage_edge2,
    output wire [63:0]       coverage_area
);
    localparam [1:0] IDLE = 2'd0, ACTIVE = 2'd1, FAILED = 2'd2;
    reg [1:0] state;
    wire coverage_ready;
    wire coverage_valid;
    wire coverage_done;
    wire [15:0] pixel_x;
    wire [15:0] pixel_y;
    wire rop_ready;
    wire rop_response;
    wire rop_pass;
    wire rop_error;
    reg pending;
    reg texture_loaded;
    reg [31:0] sampled_texel;
    reg saved_texture_enable;
    reg [31:0] saved_texture_base;
    reg [15:0] saved_texture_width;
    reg [15:0] saved_texture_height;
    reg signed [31:0] saved_texture_u;
    reg signed [31:0] saved_texture_v;
    reg saved_texture_repeat_u;
    reg saved_texture_repeat_v;
    reg saved_interpolate_uv;
    reg [16:0] saved_inv_w0;
    reg [16:0] saved_inv_w1;
    reg [16:0] saved_inv_w2;
    reg signed [31:0] saved_u0;
    reg signed [31:0] saved_u1;
    reg signed [31:0] saved_u2;
    reg signed [31:0] saved_v0;
    reg signed [31:0] saved_v1;
    reg signed [31:0] saved_v2;
    wire signed [31:0] interpolated_u;
    wire signed [31:0] interpolated_v;
    wire [31:0] interpolated_rgba;
    wire [31:0] source_rgba;
    reg saved_interpolate_color;
    reg [31:0] saved_rgba0;
    reg [31:0] saved_rgba1;
    reg [31:0] saved_rgba2;
    wire [31:0] texture_address;
    wire texture_read;
    wire fragment_accept;
    wire rop_data_valid;
    wire rop_data_write;
    wire [31:0] rop_data_addr;
    wire [2:0] rop_data_size;
    wire [31:0] rop_data_wdata;
    wire [31:0] memory_rdata;
    wire memory_error;
    reg raster_finished;
    reg [31:0] saved_depth;
    reg [31:0] saved_rgba;
    reg saved_blend;
    reg saved_interpolate_depth;
    reg [16:0] saved_depth0;
    reg [16:0] saved_depth1;
    reg [16:0] saved_depth2;
    wire [31:0] interpolated_depth;
    reg [15:0] saved_width;
    reg [15:0] saved_height;
    reg [31:0] saved_color_base;
    reg [31:0] saved_depth_base;
    reg [31:0] limit_bytes;
    reg [63:0] pixels;
    reg [63:0] color_end;
    reg [63:0] depth_end;
    reg [63:0] texture_end;
    reg layout_ok;
    reg draw_config_ok;
    wire signed [63:0] orientation_area;

    function automatic signed [63:0] extend_q4(input signed [19:0] value);
        extend_q4 = {{44{value[19]}}, value};
    endfunction

    function automatic [7:0] modulate_channel(
        input [7:0] color, input [7:0] texel
    );
        reg [15:0] rounded;
        begin
            rounded = ({8'd0, color} * {8'd0, texel} + 16'd127) / 16'd255;
            modulate_channel = (|rounded[15:8]) ? 8'hff : rounded[7:0];
        end
    endfunction

    function automatic [31:0] modulate_color(
        input [31:0] color, input [31:0] texel
    );
        begin
            modulate_color = {
                modulate_channel(color[31:24], texel[31:24]),
                modulate_channel(color[23:16], texel[23:16]),
                modulate_channel(color[15:8], texel[15:8]),
                modulate_channel(color[7:0], texel[7:0])
            };
        end
    endfunction

    assign orientation_area =
        (extend_q4(x1) - extend_q4(x0)) * (extend_q4(y2) - extend_q4(y0)) -
        (extend_q4(y1) - extend_q4(y0)) * (extend_q4(x2) - extend_q4(x0));

    always @* begin
        limit_bytes = capacity_bytes < MAX_BYTES ? capacity_bytes : MAX_BYTES;
        pixels = width * height;
        color_end = {32'b0, color_base} + pixels * 64'd4;
        depth_end = {32'b0, depth_base} + pixels * 64'd4;
        texture_end = {32'b0, texture_base} +
                      ({48'b0, texture_width} * {48'b0, texture_height}) * 64'd4;
        layout_ok = width != 0 && height != 0 && pixels <= 64'h40000000 &&
                    color_base[1:0] == 0 && depth_base[1:0] == 0 &&
                    color_end <= {32'b0, limit_bytes} &&
                    depth_end <= {32'b0, limit_bytes} &&
                    (color_end <= {32'b0, depth_base} ||
                     depth_end <= {32'b0, color_base});
        draw_config_ok = layout_ok && (!texture_enable ||
                         (texture_width != 0 && texture_height != 0 &&
                          texture_base[1:0] == 0 &&
                          texture_end <= {32'b0, limit_bytes} &&
                          (!texture_interpolate_uv ||
                           (inv_w0_q16 != 0 && inv_w0_q16 <= 17'd65536 &&
                            inv_w1_q16 != 0 && inv_w1_q16 <= 17'd65536 &&
                            inv_w2_q16 != 0 && inv_w2_q16 <= 17'd65536)))) &&
                         (!interpolate_color ||
                          (inv_w0_q16 != 0 && inv_w0_q16 <= 17'd65536 &&
                           inv_w1_q16 != 0 && inv_w1_q16 <= 17'd65536 &&
                           inv_w2_q16 != 0 && inv_w2_q16 <= 17'd65536)) &&
                         (!interpolate_depth ||
                         (depth0_q16 <= 17'd65536 && depth1_q16 <= 17'd65536 &&
                          depth2_q16 <= 17'd65536));
        draw_ready = state == IDLE && !reset && coverage_ready && rop_ready;
    end

    helo_rast_depth_q16 depth_interp (
        .edge0(coverage_edge0), .edge1(coverage_edge1),
        .edge2(coverage_edge2), .area(coverage_area),
        .z0(saved_depth0), .z1(saved_depth1), .z2(saved_depth2),
        .depth_bits(interpolated_depth)
    );
    helo_rast_uv_q16 uv_interp (
        .edge0(coverage_edge0), .edge1(coverage_edge1), .edge2(coverage_edge2),
        .inv_w0_q16(saved_inv_w0), .inv_w1_q16(saved_inv_w1),
        .inv_w2_q16(saved_inv_w2), .u0_q16(saved_u0),
        .u1_q16(saved_u1), .u2_q16(saved_u2),
        .v0_q16(saved_v0), .v1_q16(saved_v1), .v2_q16(saved_v2),
        .u_q16(interpolated_u), .v_q16(interpolated_v)
    );
    helo_rast_color_q8 color_interp (
        .edge0(coverage_edge0), .edge1(coverage_edge1), .edge2(coverage_edge2),
        .inv_w0_q16(saved_inv_w0), .inv_w1_q16(saved_inv_w1),
        .inv_w2_q16(saved_inv_w2), .rgba0(saved_rgba0),
        .rgba1(saved_rgba1), .rgba2(saved_rgba2), .rgba(interpolated_rgba)
    );
    assign source_rgba = saved_interpolate_color ? interpolated_rgba : saved_rgba;
    helo_tex_nearest_q16 tex_address (
        .base(saved_texture_base), .width(saved_texture_width),
        .height(saved_texture_height),
        .u_q16(saved_interpolate_uv ? interpolated_u : saved_texture_u),
        .v_q16(saved_interpolate_uv ? interpolated_v : saved_texture_v),
        .repeat_u(saved_texture_repeat_u),
        .repeat_v(saved_texture_repeat_v), .address(texture_address)
    );

    assign texture_read = state == ACTIVE && coverage_valid && !pending &&
                          saved_texture_enable && !texture_loaded && !reset;
    assign fragment_accept = coverage_valid && rop_ready && !pending &&
                             (!saved_texture_enable || texture_loaded) && state == ACTIVE;

    helo_rast_coverage coverage (
        .clk(clk), .reset(reset), .start_valid(draw_valid && draw_ready && draw_config_ok),
        .start_ready(coverage_ready), .width(width), .height(height),
        .x0(x0), .y0(y0), .x1(x1), .y1(y1), .x2(x2), .y2(y2),
        .out_valid(coverage_valid), .out_ready(fragment_accept),
        .out_x(pixel_x), .out_y(pixel_y),
        .out_edge0(coverage_edge0), .out_edge1(coverage_edge1),
        .out_edge2(coverage_edge2), .out_area(coverage_area),
        .busy(coverage_busy), .done(coverage_done)
    );
    helo_rop_fragment #(.MAX_BYTES(MAX_BYTES)) rop (
        .clk(clk), .reset(reset), .capacity_bytes(capacity_bytes),
        .request_valid(fragment_accept),
        .request_ready(rop_ready), .width(saved_width), .height(saved_height),
        .color_base(saved_color_base), .depth_base(saved_depth_base),
        .stencil_base(32'd0), .has_depth(1'b1), .has_stencil(1'b0),
        .depth_test(1'b1), .depth_write(1'b1), .depth_compare(3'd1),
        .stencil_test(1'b0), .stencil_compare(3'd7),
        .stencil_reference(8'd0), .stencil_read_mask(8'd0),
        .stencil_write_mask(8'd0), .stencil_fail(3'd0),
        .stencil_depth_fail(3'd0), .stencil_pass(3'd0),
        .color_write(1'b1), .blend(saved_blend),
        .fragment_x(pixel_x), .fragment_y(pixel_y),
        .fragment_depth(saved_interpolate_depth ? interpolated_depth : saved_depth),
        .fragment_rgba(saved_texture_enable ?
                       modulate_color(source_rgba, sampled_texel) : source_rgba),
        .response_valid(rop_response), .response_pass(rop_pass),
        .response_error(rop_error), .data_valid(rop_data_valid),
        .data_write(rop_data_write), .data_addr(rop_data_addr),
        .data_size(rop_data_size), .data_wdata(rop_data_wdata),
        .data_rdata(memory_rdata), .data_error(memory_error)
    );
    helo_memory #(.MAX_BYTES(MAX_BYTES)) memory (
        .clk(clk), .capacity_bytes(capacity_bytes),
        .host_valid(host_valid), .host_write(host_write), .host_addr(host_addr),
        .host_size(host_size), .host_wdata(host_wdata),
        .host_rdata(host_rdata), .host_error(host_error),
        .fetch_valid(fetch_valid), .fetch_addr(fetch_addr),
        .fetch_instr(fetch_instr), .fetch_error(fetch_error),
        .data_valid(texture_read || rop_data_valid),
        .data_write(!texture_read && rop_data_write),
        .data_addr(texture_read ? texture_address : rop_data_addr),
        .data_size(texture_read ? 3'd4 : rop_data_size),
        .data_wdata(rop_data_wdata), .data_rdata(memory_rdata),
        .data_error(memory_error)
    );

    always @(posedge clk) begin
        if (reset) begin
            state <= IDLE;
            pending <= 1'b0;
            texture_loaded <= 1'b0;
            sampled_texel <= 0;
            saved_texture_enable <= 1'b0;
            saved_texture_base <= 0;
            saved_texture_width <= 0;
            saved_texture_height <= 0;
            saved_texture_u <= 0;
            saved_texture_v <= 0;
            saved_texture_repeat_u <= 1'b0;
            saved_texture_repeat_v <= 1'b0;
            saved_interpolate_uv <= 1'b0;
            saved_inv_w0 <= 0;
            saved_inv_w1 <= 0;
            saved_inv_w2 <= 0;
            saved_u0 <= 0;
            saved_u1 <= 0;
            saved_u2 <= 0;
            saved_v0 <= 0;
            saved_v1 <= 0;
            saved_v2 <= 0;
            raster_finished <= 1'b0;
            draw_done <= 1'b0;
            draw_error <= 1'b0;
            covered_count <= 0;
            passed_count <= 0;
            saved_depth <= 0;
            saved_rgba <= 0;
            saved_interpolate_color <= 1'b0;
            saved_rgba0 <= 0;
            saved_rgba1 <= 0;
            saved_rgba2 <= 0;
            saved_blend <= 1'b0;
            saved_interpolate_depth <= 1'b0;
            saved_depth0 <= 0;
            saved_depth1 <= 0;
            saved_depth2 <= 0;
            saved_width <= 0;
            saved_height <= 0;
            saved_color_base <= 0;
            saved_depth_base <= 0;
        end else begin
            draw_done <= 1'b0;
            if (draw_valid && draw_ready) begin
                state <= draw_config_ok ? ACTIVE : FAILED;
                saved_width <= width;
                saved_height <= height;
                saved_color_base <= color_base;
                saved_depth_base <= depth_base;
                saved_depth <= flat_depth;
                saved_rgba <= flat_rgba;
                saved_interpolate_color <= interpolate_color;
                saved_rgba0 <= rgba0;
                saved_rgba1 <= orientation_area < 0 ? rgba2 : rgba1;
                saved_rgba2 <= orientation_area < 0 ? rgba1 : rgba2;
                saved_blend <= blend;
                saved_interpolate_depth <= interpolate_depth;
                saved_depth0 <= depth0_q16;
                saved_depth1 <= orientation_area < 0 ? depth2_q16 : depth1_q16;
                saved_depth2 <= orientation_area < 0 ? depth1_q16 : depth2_q16;
                saved_texture_enable <= texture_enable;
                saved_texture_base <= texture_base;
                saved_texture_width <= texture_width;
                saved_texture_height <= texture_height;
                saved_texture_u <= texture_u_q16;
                saved_texture_v <= texture_v_q16;
                saved_texture_repeat_u <= texture_repeat_u;
                saved_texture_repeat_v <= texture_repeat_v;
                saved_interpolate_uv <= texture_enable && texture_interpolate_uv;
                saved_inv_w0 <= inv_w0_q16;
                saved_inv_w1 <= orientation_area < 0 ? inv_w2_q16 : inv_w1_q16;
                saved_inv_w2 <= orientation_area < 0 ? inv_w1_q16 : inv_w2_q16;
                saved_u0 <= u0_q16;
                saved_u1 <= orientation_area < 0 ? u2_q16 : u1_q16;
                saved_u2 <= orientation_area < 0 ? u1_q16 : u2_q16;
                saved_v0 <= v0_q16;
                saved_v1 <= orientation_area < 0 ? v2_q16 : v1_q16;
                saved_v2 <= orientation_area < 0 ? v1_q16 : v2_q16;
                covered_count <= 0;
                passed_count <= 0;
                pending <= 1'b0;
                texture_loaded <= 1'b0;
                raster_finished <= 1'b0;
                draw_error <= !draw_config_ok;
            end else if (state == ACTIVE) begin
                if (texture_read) begin
                    if (memory_error) begin
                        draw_error <= 1'b1;
                        state <= FAILED;
                    end else begin
                        sampled_texel <= memory_rdata;
                        texture_loaded <= 1'b1;
                    end
                end
                if (fragment_accept) begin
                    pending <= 1'b1;
                    texture_loaded <= 1'b0;
                    covered_count <= covered_count + 32'd1;
                end
                if (coverage_done) raster_finished <= 1'b1;
                if (rop_response) begin
                    pending <= 1'b0;
                    if (rop_pass && !rop_error) passed_count <= passed_count + 32'd1;
                    if (rop_error) begin
                        draw_error <= 1'b1;
                        state <= FAILED;
                    end
                end
                if (raster_finished && !pending && !rop_response) begin
                    draw_done <= 1'b1;
                    state <= IDLE;
                end
            end
        end
    end
endmodule
