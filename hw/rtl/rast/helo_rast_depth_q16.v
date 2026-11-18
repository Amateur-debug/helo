module helo_rast_depth_q16 (
    input wire [63:0] edge0,
    input wire [63:0] edge1,
    input wire [63:0] edge2,
    input wire [63:0] area,
    input wire [16:0] z0,
    input wire [16:0] z1,
    input wire [16:0] z2,
    output reg [31:0] depth_bits
);
    reg [63:0] numerator;
    reg [63:0] rounded;
    reg [16:0] depth_q16;
    reg [4:0] leading;
    reg [7:0] exponent;
    reg [22:0] fraction;
    integer bit_index;

    always @* begin
        numerator = edge0 * {47'b0, z0} + edge1 * {47'b0, z1} +
                    edge2 * {47'b0, z2};
        rounded = area == 0 ? 64'd0 : (numerator + (area >> 1)) / area;
        depth_q16 = rounded > 64'd65536 ? 17'd65536 : rounded[16:0];
        leading = 0;
        for (bit_index = 0; bit_index < 17; bit_index = bit_index + 1) begin
            if (depth_q16[bit_index]) leading = bit_index[4:0];
        end
        exponent = 8'd111 + {3'b0, leading};
        fraction = {6'd0, depth_q16} << (5'd23 - leading);
        depth_bits = depth_q16 == 0 ? 32'd0 :
                     {1'b0, exponent, fraction};
    end
endmodule
