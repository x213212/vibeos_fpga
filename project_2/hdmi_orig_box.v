`timescale 1ns/1ps

module hdmi_orig_box(
    input  wire CLK,
    input  wire RST_BTN,
    output wire hdmi_tx_clk_n,
    output wire hdmi_tx_clk_p,
    output wire [2:0] hdmi_tx_n,
    output wire [2:0] hdmi_tx_p
);
    wire pix_clk;
    wire pix_clk_5x;
    wire clk_lock;

    display_clocks #(
        .MULT_MASTER(15.0),
        .DIV_MASTER(1),
        .DIV_5X(2.0),
        .DIV_1X(10),
        .IN_PERIOD(20.0)
    ) u_display_clocks (
        .i_clk(CLK),
        .i_rst(1'b0),
        .o_clk_1x(pix_clk),
        .o_clk_5x(pix_clk_5x),
        .o_locked(clk_lock)
    );

    wire signed [15:0] sx;
    wire signed [15:0] sy;
    wire h_sync;
    wire v_sync;
    wire de;
    wire frame;

    display_timings #(
        .H_RES(1280),
        .V_RES(720),
        .H_FP(110),
        .H_SYNC(40),
        .H_BP(220),
        .V_FP(5),
        .V_SYNC(5),
        .V_BP(20),
        .H_POL(1),
        .V_POL(1)
    ) u_display_timings (
        .i_pix_clk(pix_clk),
        .i_rst(!clk_lock),
        .o_hs(h_sync),
        .o_vs(v_sync),
        .o_de(de),
        .o_frame(frame),
        .o_sx(sx),
        .o_sy(sy)
    );

    localparam [10:0] BOX_SIZE = 11'd160;
    localparam [10:0] BOX_Y = 11'd280;
    reg [10:0] box_x = 11'd40;
    reg dir_x = 1'b1;
    wire signed [15:0] box_x_s = {5'd0, box_x};
    wire signed [15:0] box_y_s = {5'd0, BOX_Y};
    wire signed [15:0] box_right_s = {5'd0, box_x + BOX_SIZE};
    wire signed [15:0] box_bottom_s = {5'd0, BOX_Y + BOX_SIZE};

    always @(posedge pix_clk) begin
        if (!clk_lock) begin
            box_x <= 11'd40;
            dir_x <= 1'b1;
        end else if (frame) begin
            if (dir_x) begin
                if (box_x >= 11'd1280 - BOX_SIZE - 11'd8) begin
                    box_x <= 11'd1280 - BOX_SIZE;
                    dir_x <= 1'b0;
                end else begin
                    box_x <= box_x + 11'd8;
                end
            end else begin
                if (box_x <= 11'd8) begin
                    box_x <= 11'd0;
                    dir_x <= 1'b1;
                end else begin
                    box_x <= box_x - 11'd8;
                end
            end
        end
    end

    wire box = de &&
        (sx >= box_x_s) && (sx < box_right_s) &&
        (sy >= box_y_s) && (sy < box_bottom_s);
    wire border = box &&
        ((sx < box_x_s + 16'sd8) || (sx >= box_right_s - 16'sd8) ||
         (sy < box_y_s + 16'sd8) || (sy >= box_bottom_s - 16'sd8));
    wire center_cross = de && ((sx >= 16'sd636 && sx < 16'sd644) || (sy >= 16'sd356 && sy < 16'sd364));
    wire [2:0] band = sx[10:8];

    wire [7:0] bg_red =
        (band == 3'd0) ? 8'h40 :
        (band == 3'd1) ? 8'h00 :
        (band == 3'd2) ? 8'h00 :
        (band == 3'd3) ? 8'h40 :
        8'h08;
    wire [7:0] bg_green =
        (band == 3'd0) ? 8'h00 :
        (band == 3'd1) ? 8'h40 :
        (band == 3'd2) ? 8'h00 :
        (band == 3'd3) ? 8'h40 :
        8'h08;
    wire [7:0] bg_blue =
        (band == 3'd0) ? 8'h00 :
        (band == 3'd1) ? 8'h00 :
        (band == 3'd2) ? 8'h40 :
        (band == 3'd3) ? 8'h00 :
        8'h18;

    wire [7:0] red   = border ? 8'hff : (box ? 8'hff : (center_cross ? 8'hff : bg_red));
    wire [7:0] green = border ? 8'hff : (box ? 8'h00 : (center_cross ? 8'hff : bg_green));
    wire [7:0] blue  = border ? 8'hff : (box ? 8'hff : (center_cross ? 8'hff : bg_blue));

    wire tmds_ch0_serial;
    wire tmds_ch1_serial;
    wire tmds_ch2_serial;
    wire tmds_chc_serial;

    dvi_generator u_dvi (
        .i_pix_clk(pix_clk),
        .i_pix_clk_5x(pix_clk_5x),
        .i_rst(!clk_lock),
        .i_de(de),
        .i_data_ch0(blue),
        .i_data_ch1(green),
        .i_data_ch2(red),
        .i_ctrl_ch0({v_sync, h_sync}),
        .i_ctrl_ch1(2'b00),
        .i_ctrl_ch2(2'b00),
        .o_tmds_ch0_serial(tmds_ch0_serial),
        .o_tmds_ch1_serial(tmds_ch1_serial),
        .o_tmds_ch2_serial(tmds_ch2_serial),
        .o_tmds_chc_serial(tmds_chc_serial)
    );

    OBUFDS #(.IOSTANDARD("TMDS_33")) tmds_buf_ch0(.I(tmds_ch0_serial), .O(hdmi_tx_p[0]), .OB(hdmi_tx_n[0]));
    OBUFDS #(.IOSTANDARD("TMDS_33")) tmds_buf_ch1(.I(tmds_ch1_serial), .O(hdmi_tx_p[1]), .OB(hdmi_tx_n[1]));
    OBUFDS #(.IOSTANDARD("TMDS_33")) tmds_buf_ch2(.I(tmds_ch2_serial), .O(hdmi_tx_p[2]), .OB(hdmi_tx_n[2]));
    OBUFDS #(.IOSTANDARD("TMDS_33")) tmds_buf_chc(.I(tmds_chc_serial), .O(hdmi_tx_clk_p), .OB(hdmi_tx_clk_n));
endmodule
