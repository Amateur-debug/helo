module helo_rast_coverage (
    input wire               clk,
    input wire               reset,
    input wire               start_valid,
    output reg               start_ready,
    input wire [15:0]        width,
    input wire [15:0]        height,
    input wire signed [19:0] x0,
    input wire signed [19:0] y0,
    input wire signed [19:0] x1,
    input wire signed [19:0] y1,
    input wire signed [19:0] x2,
    input wire signed [19:0] y2,
    output reg               out_valid,
    input wire               out_ready,
    output reg [15:0]        out_x,
    output reg [15:0]        out_y,
    output reg [63:0]        out_edge0,
    output reg [63:0]        out_edge1,
    output reg [63:0]        out_edge2,
    output reg [63:0]        out_area,
    output reg               busy,
    output reg               done
);
    localparam [1:0] IDLE = 2'd0, SCAN = 2'd1, DRAIN = 2'd2;
    reg [1:0] state;
    reg signed [19:0] vx [0:2];
    reg signed [19:0] vy [0:2];
    reg [15:0] min_x;
    reg [15:0] max_x;
    reg [15:0] max_y;
    reg [15:0] scan_x;
    reg [15:0] scan_y;
    reg signed [63:0] min_qx;
    reg signed [63:0] max_qx;
    reg signed [63:0] min_qy;
    reg signed [63:0] max_qy;
    reg signed [63:0] lo_x;
    reg signed [63:0] hi_x;
    reg signed [63:0] lo_y;
    reg signed [63:0] hi_y;
    reg signed [63:0] area;
    reg signed [63:0] pixel_x;
    reg signed [63:0] pixel_y;
    reg signed [63:0] edge0;
    reg signed [63:0] edge1;
    reg signed [63:0] edge2;
    reg empty;
    reg covered;
    integer i;

    function automatic signed [63:0] extend_q4(input signed [19:0] value);
        extend_q4 = {{44{value[19]}}, value};
    endfunction

    function automatic signed [63:0] edge_value(
        input signed [19:0] ax, input signed [19:0] ay,
        input signed [19:0] bx, input signed [19:0] by,
        input signed [63:0] px, input signed [63:0] py
    );
        edge_value = (extend_q4(bx) - extend_q4(ax)) * (py - extend_q4(ay)) -
                     (extend_q4(by) - extend_q4(ay)) * (px - extend_q4(ax));
    endfunction

    function automatic top_left(
        input signed [19:0] ax, input signed [19:0] ay,
        input signed [19:0] bx, input signed [19:0] by
    );
        top_left = by < ay || (by == ay && bx > ax);
    endfunction

    always @* begin
        start_ready = state == IDLE && !reset;
        busy = state != IDLE;
        area = edge_value(x0, y0, x1, y1, extend_q4(x2), extend_q4(y2));
        min_qx = extend_q4(x0);
        max_qx = extend_q4(x0);
        min_qy = extend_q4(y0);
        max_qy = extend_q4(y0);
        if (extend_q4(x1) < min_qx) min_qx = extend_q4(x1);
        if (extend_q4(x2) < min_qx) min_qx = extend_q4(x2);
        if (extend_q4(x1) > max_qx) max_qx = extend_q4(x1);
        if (extend_q4(x2) > max_qx) max_qx = extend_q4(x2);
        if (extend_q4(y1) < min_qy) min_qy = extend_q4(y1);
        if (extend_q4(y2) < min_qy) min_qy = extend_q4(y2);
        if (extend_q4(y1) > max_qy) max_qy = extend_q4(y1);
        if (extend_q4(y2) > max_qy) max_qy = extend_q4(y2);

        // 算术右移是向负无穷取整，负号组合给出对应的向上取整。
        lo_x = -((-(min_qx - 64'sd8)) >>> 4);
        hi_x = (max_qx - 64'sd8) >>> 4;
        lo_y = -((-(min_qy - 64'sd8)) >>> 4);
        hi_y = (max_qy - 64'sd8) >>> 4;
        if (lo_x < 0) lo_x = 0;
        if (lo_y < 0) lo_y = 0;
        if (hi_x >= $signed({48'b0, width})) hi_x = $signed({48'b0, width}) - 64'sd1;
        if (hi_y >= $signed({48'b0, height})) hi_y = $signed({48'b0, height}) - 64'sd1;
        empty = width == 0 || height == 0 || area == 0 || lo_x > hi_x || lo_y > hi_y;

        pixel_x = $signed({1'b0, scan_x}) * 64'sd16 + 64'sd8;
        pixel_y = $signed({1'b0, scan_y}) * 64'sd16 + 64'sd8;
        edge0 = edge_value(vx[1], vy[1], vx[2], vy[2], pixel_x, pixel_y);
        edge1 = edge_value(vx[2], vy[2], vx[0], vy[0], pixel_x, pixel_y);
        edge2 = edge_value(vx[0], vy[0], vx[1], vy[1], pixel_x, pixel_y);
        covered = (edge0 > 0 || (edge0 == 0 && top_left(vx[1], vy[1], vx[2], vy[2]))) &&
                  (edge1 > 0 || (edge1 == 0 && top_left(vx[2], vy[2], vx[0], vy[0]))) &&
                  (edge2 > 0 || (edge2 == 0 && top_left(vx[0], vy[0], vx[1], vy[1])));
    end

    always @(posedge clk) begin
        if (reset) begin
            state <= IDLE;
            out_valid <= 1'b0;
            out_x <= 0;
            out_y <= 0;
            out_edge0 <= 0;
            out_edge1 <= 0;
            out_edge2 <= 0;
            out_area <= 0;
            done <= 1'b0;
            min_x <= 0;
            max_x <= 0;
            max_y <= 0;
            scan_x <= 0;
            scan_y <= 0;
            for (i = 0; i < 3; i = i + 1) begin
                vx[i] <= 0;
                vy[i] <= 0;
            end
        end else begin
            done <= 1'b0;
            case (state)
                IDLE: if (start_valid) begin
                    vx[0] <= x0;
                    vy[0] <= y0;
                    vx[1] <= area < 0 ? x2 : x1;
                    vy[1] <= area < 0 ? y2 : y1;
                    vx[2] <= area < 0 ? x1 : x2;
                    vy[2] <= area < 0 ? y1 : y2;
                    min_x <= lo_x[15:0];
                    max_x <= hi_x[15:0];
                    max_y <= hi_y[15:0];
                    scan_x <= lo_x[15:0];
                    scan_y <= lo_y[15:0];
                    out_valid <= 1'b0;
                    out_area <= area < 0 ? -area : area;
                    state <= empty ? DRAIN : SCAN;
                end
                SCAN: if (!out_valid || out_ready) begin
                    out_valid <= covered;
                    if (covered) begin
                        out_x <= scan_x;
                        out_y <= scan_y;
                        out_edge0 <= edge0;
                        out_edge1 <= edge1;
                        out_edge2 <= edge2;
                    end
                    if (scan_x == max_x) begin
                        scan_x <= min_x;
                        if (scan_y == max_y) begin
                            state <= DRAIN;
                        end else begin
                            scan_y <= scan_y + 16'd1;
                        end
                    end else begin
                        scan_x <= scan_x + 16'd1;
                    end
                end
                DRAIN: if (!out_valid || out_ready) begin
                    out_valid <= 1'b0;
                    done <= 1'b1;
                    state <= IDLE;
                end
                default: state <= IDLE;
            endcase
        end
    end
endmodule
