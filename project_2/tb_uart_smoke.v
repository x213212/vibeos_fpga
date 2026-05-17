`timescale 1ns/1ps

module tb_uart_smoke;

localparam integer CLK_FREQ_HZ = 1000000;
localparam integer UART_BAUD = 100000;
localparam integer CLK_HALF_NS = 500;
localparam integer BIT_CYCLES = CLK_FREQ_HZ / UART_BAUD;

reg clk = 1'b0;
reg resetn = 1'b0;
wire uart_tx;
wire [7:0] led;
integer i;
integer chars_seen = 0;
reg [7:0] rx_byte;

always #CLK_HALF_NS clk = ~clk;

uart_smoke #(
    .CLK_FREQ_HZ(CLK_FREQ_HZ),
    .UART_BAUD(UART_BAUD),
    .SEND_INTERVAL_HZ(1000)
) uut (
    .clk(clk),
    .resetn(resetn),
    .uart_tx(uart_tx),
    .led(led)
);

task recv_uart_byte;
    output [7:0] data;
    begin
        @(negedge uart_tx);
        repeat (BIT_CYCLES + (BIT_CYCLES / 2)) @(negedge clk);
        for (i = 0; i < 8; i = i + 1) begin
            data[i] = uart_tx;
            repeat (BIT_CYCLES) @(negedge clk);
        end
    end
endtask

function [7:0] expected_byte;
    input [3:0] idx;
    begin
        case (idx)
        4'd0: expected_byte = "U";
        4'd1: expected_byte = "A";
        4'd2: expected_byte = "R";
        4'd3: expected_byte = "T";
        4'd4: expected_byte = "_";
        4'd5: expected_byte = "O";
        4'd6: expected_byte = "K";
        4'd7: expected_byte = 8'h0d;
        default: expected_byte = 8'h0a;
        endcase
    end
endfunction

initial begin
    repeat (20) @(posedge clk);
    resetn = 1'b1;

    repeat (9) begin
        recv_uart_byte(rx_byte);
        $write("%c", rx_byte);
        if (rx_byte !== expected_byte(chars_seen[3:0])) begin
            $display("");
            $display("UART_SMOKE_FAIL expected=%02x got=%02x index=%0d",
                     expected_byte(chars_seen[3:0]), rx_byte, chars_seen);
            $finish;
        end
        chars_seen = chars_seen + 1;
    end

    if (chars_seen != 9) begin
        $display("UART_SMOKE_FAIL");
        $finish;
    end

    $display("UART_SMOKE_DONE");
    $finish;
end

endmodule
