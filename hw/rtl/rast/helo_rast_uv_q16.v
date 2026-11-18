module helo_rast_uv_q16 (
    input wire [63:0] edge0,
    input wire [63:0] edge1,
    input wire [63:0] edge2,
    input wire [16:0] inv_w0_q16,
    input wire [16:0] inv_w1_q16,
    input wire [16:0] inv_w2_q16,
    input wire signed [31:0] u0_q16,
    input wire signed [31:0] u1_q16,
    input wire signed [31:0] u2_q16,
    input wire signed [31:0] v0_q16,
    input wire signed [31:0] v1_q16,
    input wire signed [31:0] v2_q16,
    output reg signed [31:0] u_q16,
    output reg signed [31:0] v_q16
);
    reg signed [127:0] weight0;
    reg signed [127:0] weight1;
    reg signed [127:0] weight2;
    reg signed [127:0] denominator;
    reg signed [127:0] numerator_u;
    reg signed [127:0] numerator_v;
    reg signed [127:0] rounded_u;
    reg signed [127:0] rounded_v;

    function automatic signed [127:0] extend_uv(input signed [31:0] uv);
        extend_uv = {{96{uv[31]}}, uv};
    endfunction

    always @* begin
        weight0 = $signed({64'd0, edge0}) * $signed({111'd0, inv_w0_q16});
        weight1 = $signed({64'd0, edge1}) * $signed({111'd0, inv_w1_q16});
        weight2 = $signed({64'd0, edge2}) * $signed({111'd0, inv_w2_q16});
        denominator = weight0 + weight1 + weight2;
        numerator_u = weight0 * extend_uv(u0_q16) + weight1 * extend_uv(u1_q16) +
                      weight2 * extend_uv(u2_q16);
        numerator_v = weight0 * extend_uv(v0_q16) + weight1 * extend_uv(v1_q16) +
                      weight2 * extend_uv(v2_q16);
        rounded_u = 0;
        rounded_v = 0;
        if (denominator != 0) begin
            rounded_u = numerator_u < 0 ?
                        -((-numerator_u + (denominator >> 1)) / denominator) :
                        (numerator_u + (denominator >> 1)) / denominator;
            rounded_v = numerator_v < 0 ?
                        -((-numerator_v + (denominator >> 1)) / denominator) :
                        (numerator_v + (denominator >> 1)) / denominator;
        end
        if (rounded_u > 128'sd2147483647) u_q16 = 32'sh7fffffff;
        else if (rounded_u < -128'sd2147483648) u_q16 = 32'sh80000000;
        else u_q16 = rounded_u[31:0];
        if (rounded_v > 128'sd2147483647) v_q16 = 32'sh7fffffff;
        else if (rounded_v < -128'sd2147483648) v_q16 = 32'sh80000000;
        else v_q16 = rounded_v[31:0];
    end
endmodule
