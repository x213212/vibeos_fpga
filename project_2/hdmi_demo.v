`timescale 1ns/1ps

module tmds_encoder_dvi(
    input wire clk,
    input wire rst,
    input wire de,
    input wire [7:0] data,
    input wire [1:0] ctrl,
    output reg [9:0] out
);
    reg signed [4:0] disparity;
    reg [8:0] q_m;
    reg [3:0] ones_data;
    reg [3:0] ones_qm;
    integer i;

    always @* begin
        ones_data = data[0] + data[1] + data[2] + data[3] + data[4] + data[5] + data[6] + data[7];
        q_m[0] = data[0];
        if (ones_data > 4 || (ones_data == 4 && data[0] == 1'b0)) begin
            q_m[1] = q_m[0] ~^ data[1];
            q_m[2] = q_m[1] ~^ data[2];
            q_m[3] = q_m[2] ~^ data[3];
            q_m[4] = q_m[3] ~^ data[4];
            q_m[5] = q_m[4] ~^ data[5];
            q_m[6] = q_m[5] ~^ data[6];
            q_m[7] = q_m[6] ~^ data[7];
            q_m[8] = 1'b0;
        end else begin
            q_m[1] = q_m[0] ^ data[1];
            q_m[2] = q_m[1] ^ data[2];
            q_m[3] = q_m[2] ^ data[3];
            q_m[4] = q_m[3] ^ data[4];
            q_m[5] = q_m[4] ^ data[5];
            q_m[6] = q_m[5] ^ data[6];
            q_m[7] = q_m[6] ^ data[7];
            q_m[8] = 1'b1;
        end

        ones_qm = 0;
        for (i = 0; i < 8; i = i + 1)
            ones_qm = ones_qm + q_m[i];
    end

    always @(posedge clk) begin
        if (rst) begin
            out <= 10'b1101010100;
            disparity <= 0;
        end else if (!de) begin
            disparity <= 0;
            case (ctrl)
            2'b00: out <= 10'b1101010100;
            2'b01: out <= 10'b0010101011;
            2'b10: out <= 10'b0101010100;
            default: out <= 10'b1010101011;
            endcase
        end else if (disparity == 0 || ones_qm == 4) begin
            out <= {~q_m[8], q_m[8], q_m[8] ? q_m[7:0] : ~q_m[7:0]};
            if (q_m[8])
                disparity <= disparity + $signed({1'b0, ones_qm}) - $signed(5'd4);
            else
                disparity <= disparity + $signed(5'd4) - $signed({1'b0, ones_qm});
        end else if ((disparity > 0 && ones_qm > 4) || (disparity < 0 && ones_qm < 4)) begin
            out <= {1'b1, q_m[8], ~q_m[7:0]};
            disparity <= disparity + $signed({4'd0, q_m[8]}) + $signed(5'd4) - $signed({1'b0, ones_qm});
        end else begin
            out <= {1'b0, q_m[8], q_m[7:0]};
            disparity <= disparity - $signed({4'd0, ~q_m[8]}) + $signed({1'b0, ones_qm}) - $signed(5'd4);
        end
    end
endmodule

module hdmi_demo(
    input wire pl_clk_50m,
    output wire hdmi_clk_p,
    output wire hdmi_clk_n,
    output wire hdmi_d2_p,
    output wire hdmi_d2_n,
    output wire hdmi_d1_p,
    output wire hdmi_d1_n,
    output wire hdmi_d0_p,
    output wire hdmi_d0_n
);
    wire clkfb_mmcm;
    wire clkfb;
    wire pixclk_mmcm;
    wire serclk_mmcm;
    wire pixclk;
    wire serclk;
    wire locked;

    MMCME2_BASE #(
        .CLKIN1_PERIOD(20.0),
        .CLKFBOUT_MULT_F(20.0),
        .DIVCLK_DIVIDE(1),
        .CLKOUT0_DIVIDE_F(40.0),
        .CLKOUT1_DIVIDE(8)
    ) u_mmcm (
        .CLKIN1(pl_clk_50m),
        .CLKFBIN(clkfb),
        .CLKFBOUT(clkfb_mmcm),
        .CLKOUT0(pixclk_mmcm),
        .CLKOUT1(serclk_mmcm),
        .LOCKED(locked),
        .PWRDWN(1'b0),
        .RST(1'b0)
    );

    BUFG u_fb_buf(.I(clkfb_mmcm), .O(clkfb));
    BUFG u_pix_buf(.I(pixclk_mmcm), .O(pixclk));
    BUFG u_ser_buf(.I(serclk_mmcm), .O(serclk));

    reg [9:0] h = 0;
    reg [9:0] v = 0;
    wire active = (h < 640) && (v < 480);
    wire hsync = (h >= 656) && (h < 752);
    wire vsync = (v >= 490) && (v < 492);

    localparam [9:0] BOX_SIZE = 10'd64;
    reg [9:0] box_x = 10'd32;
    reg [9:0] box_y = 10'd32;
    reg dir_x = 1'b1;
    reg dir_y = 1'b1;

    wire box = active &&
        (h >= box_x) && (h < box_x + BOX_SIZE) &&
        (v >= box_y) && (v < box_y + BOX_SIZE);
    wire border = box &&
        ((h < box_x + 10'd4) || (h >= box_x + BOX_SIZE - 10'd4) ||
         (v < box_y + 10'd4) || (v >= box_y + BOX_SIZE - 10'd4));
    wire grid = active && ((h[5:0] == 6'd0) || (v[5:0] == 6'd0));

    wire [7:0] red = border ? 8'hff : (box ? 8'h10 : (grid ? 8'h18 : 8'h00));
    wire [7:0] green = box ? 8'hd8 : (grid ? 8'h18 : 8'h00);
    wire [7:0] blue = border ? 8'h00 : (box ? 8'h30 : (grid ? 8'h28 : 8'h08));

    always @(posedge pixclk) begin
        if (!locked) begin
            h <= 0;
            v <= 0;
            box_x <= 10'd32;
            box_y <= 10'd32;
            dir_x <= 1'b1;
            dir_y <= 1'b1;
        end else if (h == 799) begin
            h <= 0;
            if (v == 524) begin
                v <= 0;

                if (dir_x) begin
                    if (box_x >= 10'd640 - BOX_SIZE - 10'd3) begin
                        box_x <= 10'd640 - BOX_SIZE;
                        dir_x <= 1'b0;
                    end else begin
                        box_x <= box_x + 10'd3;
                    end
                end else begin
                    if (box_x <= 10'd3) begin
                        box_x <= 10'd0;
                        dir_x <= 1'b1;
                    end else begin
                        box_x <= box_x - 10'd3;
                    end
                end

                if (dir_y) begin
                    if (box_y >= 10'd480 - BOX_SIZE - 10'd2) begin
                        box_y <= 10'd480 - BOX_SIZE;
                        dir_y <= 1'b0;
                    end else begin
                        box_y <= box_y + 10'd2;
                    end
                end else begin
                    if (box_y <= 10'd2) begin
                        box_y <= 10'd0;
                        dir_y <= 1'b1;
                    end else begin
                        box_y <= box_y - 10'd2;
                    end
                end
            end else begin
                v <= v + 1'b1;
            end
        end else begin
            h <= h + 1'b1;
        end
    end

    wire [9:0] tmds_r;
    wire [9:0] tmds_g;
    wire [9:0] tmds_b;
    tmds_encoder_dvi enc_r(.clk(pixclk), .rst(!locked), .de(active), .data(red), .ctrl(2'b00), .out(tmds_r));
    tmds_encoder_dvi enc_g(.clk(pixclk), .rst(!locked), .de(active), .data(green), .ctrl(2'b00), .out(tmds_g));
    tmds_encoder_dvi enc_b(.clk(pixclk), .rst(!locked), .de(active), .data(blue), .ctrl({vsync, hsync}), .out(tmds_b));

    reg [2:0] phase = 0;
    reg [9:0] sh_r = 0;
    reg [9:0] sh_g = 0;
    reg [9:0] sh_b = 0;
    reg r_a = 0, r_b = 0, g_a = 0, g_b = 0, b_a = 0, b_b = 0;

    always @(posedge serclk) begin
        if (!locked) begin
            phase <= 0;
            sh_r <= 0;
            sh_g <= 0;
            sh_b <= 0;
        end else if (phase == 0) begin
            r_a <= tmds_r[0]; r_b <= tmds_r[1];
            g_a <= tmds_g[0]; g_b <= tmds_g[1];
            b_a <= tmds_b[0]; b_b <= tmds_b[1];
            sh_r <= {2'b00, tmds_r[9:2]};
            sh_g <= {2'b00, tmds_g[9:2]};
            sh_b <= {2'b00, tmds_b[9:2]};
            phase <= 1;
        end else begin
            r_a <= sh_r[0]; r_b <= sh_r[1];
            g_a <= sh_g[0]; g_b <= sh_g[1];
            b_a <= sh_b[0]; b_b <= sh_b[1];
            sh_r <= {2'b00, sh_r[9:2]};
            sh_g <= {2'b00, sh_g[9:2]};
            sh_b <= {2'b00, sh_b[9:2]};
            phase <= (phase == 4) ? 0 : phase + 1'b1;
        end
    end

    wire clk_tmds;
    wire r_tmds;
    wire g_tmds;
    wire b_tmds;

    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) clk_oddr(.C(pixclk), .CE(1'b1), .D1(1'b1), .D2(1'b0), .Q(clk_tmds), .R(1'b0), .S(1'b0));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) r_oddr(.C(serclk), .CE(1'b1), .D1(r_a), .D2(r_b), .Q(r_tmds), .R(1'b0), .S(1'b0));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) g_oddr(.C(serclk), .CE(1'b1), .D1(g_a), .D2(g_b), .Q(g_tmds), .R(1'b0), .S(1'b0));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) b_oddr(.C(serclk), .CE(1'b1), .D1(b_a), .D2(b_b), .Q(b_tmds), .R(1'b0), .S(1'b0));

    OBUFDS #(.IOSTANDARD("TMDS_33")) clk_buf(.I(clk_tmds), .O(hdmi_clk_p), .OB(hdmi_clk_n));
    OBUFDS #(.IOSTANDARD("TMDS_33")) r_buf(.I(r_tmds), .O(hdmi_d2_p), .OB(hdmi_d2_n));
    OBUFDS #(.IOSTANDARD("TMDS_33")) g_buf(.I(g_tmds), .O(hdmi_d1_p), .OB(hdmi_d1_n));
    OBUFDS #(.IOSTANDARD("TMDS_33")) b_buf(.I(b_tmds), .O(hdmi_d0_p), .OB(hdmi_d0_n));
endmodule
