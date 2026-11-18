module helo_rast_color_q8 (
    input wire [63:0] edge0,
    input wire [63:0] edge1,
    input wire [63:0] edge2,
    input wire [16:0] inv_w0_q16,
    input wire [16:0] inv_w1_q16,
    input wire [16:0] inv_w2_q16,
    input wire [31:0] rgba0,
    input wire [31:0] rgba1,
    input wire [31:0] rgba2,
    output wire [31:0] rgba
);
    wire [127:0] weight0;
    wire [127:0] weight1;
    wire [127:0] weight2;
    wire [127:0] denominator;

    function automatic [7:0] interpolate_channel(
        input [127:0] w0,
        input [127:0] w1,
        input [127:0] w2,
        input [127:0] sum,
        input [7:0] c0,
        input [7:0] c1,
        input [7:0] c2
    );
        reg [127:0] rounded;
        begin
            rounded = sum == 0 ? 128'd0 :
                      (w0 * {120'd0, c0} + w1 * {120'd0, c1} +
                       w2 * {120'd0, c2} + (sum >> 1)) / sum;
            interpolate_channel = rounded > 128'd255 ? 8'hff : rounded[7:0];
        end
    endfunction

    assign weight0 = {64'd0, edge0} * {111'd0, inv_w0_q16};
    assign weight1 = {64'd0, edge1} * {111'd0, inv_w1_q16};
    assign weight2 = {64'd0, edge2} * {111'd0, inv_w2_q16};
    assign denominator = weight0 + weight1 + weight2;
    assign rgba = {
        interpolate_channel(weight0, weight1, weight2, denominator,
                            rgba0[31:24], rgba1[31:24], rgba2[31:24]),
        interpolate_channel(weight0, weight1, weight2, denominator,
                            rgba0[23:16], rgba1[23:16], rgba2[23:16]),
        interpolate_channel(weight0, weight1, weight2, denominator,
                            rgba0[15:8], rgba1[15:8], rgba2[15:8]),
        interpolate_channel(weight0, weight1, weight2, denominator,
                            rgba0[7:0], rgba1[7:0], rgba2[7:0])
    };
endmodule
