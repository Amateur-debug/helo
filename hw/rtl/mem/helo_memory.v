module helo_memory #(
    parameter integer MAX_BYTES = 4096
) (
    input wire        clk,
    input wire [31:0] capacity_bytes,
    input wire        host_valid,
    input wire        host_write,
    input wire [31:0] host_addr,
    input wire [2:0]  host_size,
    input wire [31:0] host_wdata,
    output reg [31:0] host_rdata,
    output reg        host_error,
    input wire        data_valid,
    input wire        data_write,
    input wire [31:0] data_addr,
    input wire [2:0]  data_size,
    input wire [31:0] data_wdata,
    output reg [31:0] data_rdata,
    output reg        data_error,
    input wire        fetch_valid,
    input wire [31:0] fetch_addr,
    output reg [63:0] fetch_instr,
    output reg        fetch_error
);
    reg [7:0] storage [0:MAX_BYTES-1];
    reg host_ok;
    reg data_ok;
    reg fetch_ok;
    integer host_read_i;
    integer data_read_i;
    integer fetch_read_i;
    integer init_i;
    integer host_write_i;
    integer data_write_i;

    // 检查宽度、自然对齐及完整地址范围，避免32位加法回绕。
    function automatic access_ok(input [31:0] addr, input [2:0] size);
        reg width_ok;
        reg aligned;
        reg [32:0] end_addr;
        width_ok = size == 3'd1 || size == 3'd2 || size == 3'd4;
        aligned = size == 3'd1 || (size == 3'd2 && !addr[0]) ||
                  (size == 3'd4 && addr[1:0] == 2'b00);
        end_addr = {1'b0, addr} + {30'b0, size};
        access_ok = width_ok && aligned && end_addr <= {1'b0, capacity_bytes} &&
                    end_addr <= {1'b0, MAX_BYTES[31:0]};
    endfunction

    always @* begin
        host_ok = access_ok(host_addr, host_size);
        data_ok = access_ok(data_addr, data_size);
        fetch_ok = fetch_addr[2:0] == 3'b000 &&
                   ({1'b0, fetch_addr} + 33'd8) <= {1'b0, capacity_bytes} &&
                   ({1'b0, fetch_addr} + 33'd8) <= {1'b0, MAX_BYTES[31:0]};
        host_error = host_valid && !host_ok;
        data_error = data_valid && !data_ok;
        fetch_error = fetch_valid && !fetch_ok;
        host_rdata = 0;
        data_rdata = 0;
        fetch_instr = 0;
        if (host_valid && host_ok && !host_write) begin
            for (host_read_i = 0; host_read_i < 4; host_read_i = host_read_i + 1) begin
                if (host_read_i < host_size) begin
                    host_rdata[host_read_i*8 +: 8] = storage[host_addr + host_read_i];
                end
            end
        end
        if (data_valid && data_ok && !data_write) begin
            for (data_read_i = 0; data_read_i < 4; data_read_i = data_read_i + 1) begin
                if (data_read_i < data_size) begin
                    data_rdata[data_read_i*8 +: 8] = storage[data_addr + data_read_i];
                end
            end
        end
        if (fetch_valid && fetch_ok) begin
            for (fetch_read_i = 0; fetch_read_i < 8; fetch_read_i = fetch_read_i + 1) begin
                fetch_instr[fetch_read_i*8 +: 8] = storage[fetch_addr + fetch_read_i];
            end
        end
    end

    // 测试仿真从零内容启动；复位设备状态时不擦除已上传资源。
    initial begin
        for (init_i = 0; init_i < MAX_BYTES; init_i = init_i + 1) begin
            storage[init_i] = 0;
        end
    end

    // 同周期双写重叠时数据端口优先；无效写入不改变任何字节。
    always @(posedge clk) begin
        if (host_valid && host_write && host_ok) begin
            for (host_write_i = 0; host_write_i < 4; host_write_i = host_write_i + 1) begin
                if (host_write_i < host_size) begin
                    storage[host_addr + host_write_i] <= host_wdata[host_write_i*8 +: 8];
                end
            end
        end
        if (data_valid && data_write && data_ok) begin
            for (data_write_i = 0; data_write_i < 4; data_write_i = data_write_i + 1) begin
                if (data_write_i < data_size) begin
                    storage[data_addr + data_write_i] <= data_wdata[data_write_i*8 +: 8];
                end
            end
        end
    end
endmodule
