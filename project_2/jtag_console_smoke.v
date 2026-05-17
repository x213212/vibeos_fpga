module jtag_console_smoke;

wire capture_w;
wire drck_w;
wire reset_w;
wire runtest_w;
wire sel_w;
wire shift_w;
wire tck_w;
wire tdi_w;
wire tms_w;
wire update_w;
reg [9:0] shift_reg;
reg [3:0] shift_count;
reg [1:0] char_index;
reg [7:0] char_data;
wire tdo_w;

assign tdo_w = shift_reg[0];

always @* begin
    case (char_index)
    2'd0: char_data = 8'h42; // B
    2'd1: char_data = 8'h46; // F
    2'd2: char_data = 8'h52; // R
    default: char_data = 8'h57; // W
    endcase
end

BSCANE2 #(
    .JTAG_CHAIN(1)
) u_bscan (
    .CAPTURE(capture_w),
    .DRCK(drck_w),
    .RESET(reset_w),
    .RUNTEST(runtest_w),
    .SEL(sel_w),
    .SHIFT(shift_w),
    .TCK(tck_w),
    .TDI(tdi_w),
    .TMS(tms_w),
    .UPDATE(update_w),
    .TDO(tdo_w)
);

always @(posedge drck_w or posedge capture_w or posedge reset_w) begin
    if (reset_w) begin
        shift_reg <= 10'h000;
        shift_count <= 4'd0;
        char_index <= 2'd0;
    end else if (capture_w) begin
        shift_reg <= {1'b0, 1'b1, char_data};
        shift_count <= 4'd0;
    end else if (shift_w) begin
        if (shift_count == 4'd9)
            char_index <= char_index + 2'd1;
        shift_count <= shift_count + 4'd1;
        shift_reg <= {tdi_w, shift_reg[9:1]};
    end
end

endmodule
