module helo_tex_nearest_q16 (
    input wire [31:0] base,
    input wire [15:0] width,
    input wire [15:0] height,
    input wire signed [31:0] u_q16,
    input wire signed [31:0] v_q16,
    input wire repeat_u,
    input wire repeat_v,
    output wire [31:0] address
);
    wire [31:0] x;
    wire [31:0] y;

    function automatic [31:0] nearest_index(
        input signed [31:0] coordinate,
        input [15:0] size,
        input repeat_axis
    );
        begin
            if (!repeat_axis && coordinate <= 0) nearest_index = 0;
            else if (!repeat_axis && coordinate >= 32'sd65536) begin
                nearest_index = {16'd0, size} - 32'd1;
            end else begin
                nearest_index = ({16'd0, coordinate[15:0]} *
                                 {16'd0, size}) >> 16;
            end
        end
    endfunction

    assign x = nearest_index(u_q16, width, repeat_u);
    assign y = nearest_index(v_q16, height, repeat_v);
    assign address = base + (y * {16'd0, width} + x) * 32'd4;
endmodule
