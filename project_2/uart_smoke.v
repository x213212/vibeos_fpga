`timescale 1ns/1ps

module uart_smoke #(
    parameter integer CLK_FREQ_HZ = 100000000,
    parameter integer UART_BAUD = 115200,
    parameter integer SEND_INTERVAL_HZ = 1
)(
    input clk,
    input resetn,
    output uart_tx,
    output reg [7:0] led
);

localparam integer SEND_INTERVAL_CYCLES = CLK_FREQ_HZ / SEND_INTERVAL_HZ;
localparam integer MSG_LEN = 9;

reg [31:0] interval_count;
reg [3:0] msg_index;
reg sending;
reg [7:0] tx_data;
reg tx_start;
wire tx_ready;

simple_uart_tx #(
    .CLK_FREQ_HZ(CLK_FREQ_HZ),
    .UART_BAUD(UART_BAUD)
) u_uart_tx (
    .clk(clk),
    .rst(!resetn),
    .tx_data(tx_data),
    .tx_start(tx_start),
    .tx_ready(tx_ready),
    .tx(uart_tx)
);

function [7:0] msg_byte;
    input [3:0] idx;
    begin
        case (idx)
        4'd0: msg_byte = "U";
        4'd1: msg_byte = "A";
        4'd2: msg_byte = "R";
        4'd3: msg_byte = "T";
        4'd4: msg_byte = "_";
        4'd5: msg_byte = "O";
        4'd6: msg_byte = "K";
        4'd7: msg_byte = 8'h0d;
        default: msg_byte = 8'h0a;
        endcase
    end
endfunction

always @(posedge clk) begin
    if (!resetn) begin
        interval_count <= 32'd0;
        msg_index <= 4'd0;
        sending <= 1'b0;
        tx_data <= 8'h00;
        tx_start <= 1'b0;
        led <= 8'h01;
    end else begin
        tx_start <= 1'b0;

        if (!sending) begin
            if (interval_count == SEND_INTERVAL_CYCLES - 1) begin
                interval_count <= 32'd0;
                sending <= 1'b1;
                msg_index <= 4'd0;
                led <= led + 8'd1;
            end else begin
                interval_count <= interval_count + 32'd1;
            end
        end else if (tx_ready && !tx_start) begin
            tx_data <= msg_byte(msg_index);
            tx_start <= 1'b1;
            if (msg_index == MSG_LEN - 1)
                sending <= 1'b0;
            else
                msg_index <= msg_index + 4'd1;
        end
    end
end

endmodule
