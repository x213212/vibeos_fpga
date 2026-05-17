module tb;

reg clk = 0;
reg resetn = 0;
wire uart_tx;
wire [7:0] led;

// clock (100MHz -> 10ns)
always #5 clk = ~clk;

top uut (
    .clk(clk),
    .resetn(resetn),
    .uart_tx(uart_tx),
    .led(led)
);

initial begin
    // reset
    #100;
    resetn = 1;

    // run long enough for early firmware UART output
    #1000000;

    $finish;
end

endmodule
