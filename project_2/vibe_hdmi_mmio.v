`timescale 1ns/1ps

module vibe_hdmi_mmio (
    input         clk,
    input         resetn,

    input         s_axi_awvalid,
    input  [31:0] s_axi_awaddr,
    input  [3:0]  s_axi_awid,
    output        s_axi_awready,
    input         s_axi_wvalid,
    input  [31:0] s_axi_wdata,
    input  [3:0]  s_axi_wstrb,
    input         s_axi_wlast,
    output        s_axi_wready,
    output        s_axi_bvalid,
    output [1:0]  s_axi_bresp,
    output [3:0]  s_axi_bid,
    input         s_axi_bready,

    input         s_axi_arvalid,
    input  [31:0] s_axi_araddr,
    input  [3:0]  s_axi_arid,
    output        s_axi_arready,
    output        s_axi_rvalid,
    output [31:0] s_axi_rdata,
    output [1:0]  s_axi_rresp,
    output [3:0]  s_axi_rid,
    output        s_axi_rlast,
    input         s_axi_rready,

    output        hdmi_tx_clk_n,
    output        hdmi_tx_clk_p,
    output [2:0]  hdmi_tx_n,
    output [2:0]  hdmi_tx_p
);

localparam [31:0] VGA_REG_BASE = 32'h40000000;
localparam [31:0] VRAM_BASE    = 32'h50000000;
localparam FB_W = 512;
localparam FB_H = 384;
localparam FB_PIXELS = FB_W * FB_H;

wire rst = !resetn;

reg [31:0] awaddr_q;
reg        awready_q;
reg        wready_q;
reg        bvalid_q;
reg [3:0]  bid_q;
reg        rvalid_q;
reg [3:0]  rid_q;
reg [31:0] rdata_q;

assign s_axi_awready = awready_q;
assign s_axi_wready  = wready_q;
assign s_axi_bvalid  = bvalid_q;
assign s_axi_bresp   = 2'b00;
assign s_axi_bid     = bid_q;

assign s_axi_arready = !rvalid_q;
assign s_axi_rvalid  = rvalid_q;
assign s_axi_rdata   = rdata_q;
assign s_axi_rresp   = 2'b00;
assign s_axi_rid     = rid_q;
assign s_axi_rlast   = 1'b1;

(* ram_style = "block" *) reg [31:0] fb [0:(FB_PIXELS/4)-1];
reg [31:0] write_count_q;
reg [31:0] last_write_addr_q;
reg [31:0] last_write_data_q;
reg        has_written_q;

wire vram_sample_write_w = s_axi_awvalid && s_axi_wvalid && awready_q &&
                           ((s_axi_awaddr & 32'hfff00000) == VRAM_BASE) &&
                           (s_axi_awaddr[18:0] < 19'h30000) &&
                           (|s_axi_wstrb);

function [23:0] color8_to_rgb;
    input [7:0] c;
    reg [7:0] r;
    reg [7:0] g;
    reg [7:0] b;
    begin
        if (c < 8'd16) begin
            case (c[3:0])
            4'h0: color8_to_rgb = 24'h08090b;
            4'h1: color8_to_rgb = 24'h101113;
            4'h2: color8_to_rgb = 24'h16181b;
            4'h3: color8_to_rgb = 24'h1d2024;
            4'h4: color8_to_rgb = 24'h25292e;
            4'h5: color8_to_rgb = 24'h2e3338;
            4'h6: color8_to_rgb = 24'h383e44;
            4'h7: color8_to_rgb = 24'h43494f;
            4'h8: color8_to_rgb = 24'h4e555b;
            4'h9: color8_to_rgb = 24'h5a6168;
            4'ha: color8_to_rgb = 24'h676e76;
            4'hb: color8_to_rgb = 24'h767d85;
            4'hc: color8_to_rgb = 24'h868d95;
            4'hd: color8_to_rgb = 24'h989ea5;
            4'he: color8_to_rgb = 24'hb3b8bd;
            default: color8_to_rgb = 24'hf0f2f4;
            endcase
        end else begin
            r = {c[7:5], c[7:5], c[7:6]};
            g = {c[4:2], c[4:2], c[4:3]};
            b = {c[1:0], c[1:0], c[1:0], c[1:0]};
            color8_to_rgb = {r, g, b};
        end
    end
endfunction

always @(posedge clk or posedge rst) begin
    if (rst) begin
        awready_q <= 1'b1;
        wready_q <= 1'b1;
        bvalid_q <= 1'b0;
        bid_q <= 4'd0;
        awaddr_q <= 32'd0;
        rvalid_q <= 1'b0;
        rid_q <= 4'd0;
        rdata_q <= 32'd0;
        write_count_q <= 32'd0;
        last_write_addr_q <= 32'd0;
        last_write_data_q <= 32'd0;
        has_written_q <= 1'b0;
    end else begin
        // Write Path
        if (s_axi_awvalid && s_axi_wvalid && awready_q) begin
            awready_q <= 1'b0;
            wready_q <= 1'b0;
            bvalid_q <= 1'b1;
            bid_q <= s_axi_awid;
            awaddr_q <= s_axi_awaddr;
            last_write_addr_q <= s_axi_awaddr;
            last_write_data_q <= s_axi_wdata;
            if (vram_sample_write_w) begin
                write_count_q <= write_count_q + 32'd1;
                has_written_q <= 1'b1;
            end
        end else if (s_axi_bready && bvalid_q) begin
            bvalid_q <= 1'b0;
            awready_q <= 1'b1;
            wready_q <= 1'b1;
        end

        // Read Path
        if (s_axi_arvalid && !rvalid_q) begin
            rvalid_q <= 1'b1;
            rid_q <= s_axi_arid;
            if ((s_axi_araddr & 32'hffff0000) == VGA_REG_BASE) begin
                case (s_axi_araddr[7:2])
                6'h00: rdata_q <= 32'h56474148;
                6'h01: rdata_q <= 32'd512;
                6'h02: rdata_q <= 32'd384;
                6'h03: rdata_q <= write_count_q;
                6'h04: rdata_q <= last_write_addr_q;
                6'h05: rdata_q <= last_write_data_q;
                default: rdata_q <= 32'd0;
                endcase
            end else begin
                rdata_q <= 32'd0;
            end
        end else if (s_axi_rready) begin
            rvalid_q <= 1'b0;
        end
    end
end

always @(posedge clk) begin
    if (vram_sample_write_w) begin
        if (s_axi_wstrb[0]) fb[{s_axi_awaddr[17:9], s_axi_awaddr[8:2]}][7:0] <= s_axi_wdata[7:0];
        if (s_axi_wstrb[1]) fb[{s_axi_awaddr[17:9], s_axi_awaddr[8:2]}][15:8] <= s_axi_wdata[15:8];
        if (s_axi_wstrb[2]) fb[{s_axi_awaddr[17:9], s_axi_awaddr[8:2]}][23:16] <= s_axi_wdata[23:16];
        if (s_axi_wstrb[3]) fb[{s_axi_awaddr[17:9], s_axi_awaddr[8:2]}][31:24] <= s_axi_wdata[31:24];
    end
end
wire pix_clk;
wire pix_clk_5x;
wire clk_lock;

display_clocks #(
`ifdef HDMI_FCLK_50
    .MULT_MASTER(13.0),
    .DIV_MASTER(1),
    .DIV_5X(2.0),
    .DIV_1X(10),
    .IN_PERIOD(20.0)
`else
    .MULT_MASTER(54.1667),
    .DIV_MASTER(5),
    .DIV_5X(2.0),
    .DIV_1X(10),
    .IN_PERIOD(16.6667)
`endif
) u_display_clocks (
    .i_clk(clk),
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
    .H_RES(1024),
    .V_RES(768),
    .H_FP(24),
    .H_SYNC(136),
    .H_BP(160),
    .V_FP(3),
    .V_SYNC(6),
    .V_BP(29),
    .H_POL(0),
    .V_POL(0)
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

reg [31:0] rd_word_q;
reg [23:0] rgb_q;
wire [8:0] rd_x_w = sx[9:1];
wire [8:0] rd_y_w = sy[9:1];
wire [15:0] rd_word_idx_w = {rd_y_w, rd_x_w[8:2]};
wire [1:0] rd_byte_sel_w = rd_x_w[1:0];
wire visible_w = de && sx >= 0 && sy >= 0 && sx < 1024 && sy < 768;

reg visible_q;
reg empty_pattern_q;
reg [15:0] sx_q;
reg [15:0] sy_q;
reg [1:0] rd_byte_sel_q;
(* ASYNC_REG = "TRUE" *) reg has_written_meta_q;
(* ASYNC_REG = "TRUE" *) reg has_written_pix_q;

always @(posedge pix_clk) begin
    rd_word_q <= fb[rd_word_idx_w]; 
    rd_byte_sel_q <= rd_byte_sel_w;
    visible_q <= visible_w;
    has_written_meta_q <= has_written_q;
    has_written_pix_q <= has_written_meta_q;
    empty_pattern_q <= !has_written_pix_q;
    sx_q <= sx;
    sy_q <= sy;

    if (!visible_q) begin
        rgb_q <= 24'h000000;
    end else if (empty_pattern_q) begin
        rgb_q <= {sx_q[7:0], sy_q[7:0], 8'h40};
    end else begin
        case (rd_byte_sel_q)
            2'b00: rgb_q <= color8_to_rgb(rd_word_q[7:0]);
            2'b01: rgb_q <= color8_to_rgb(rd_word_q[15:8]);
            2'b10: rgb_q <= color8_to_rgb(rd_word_q[23:16]);
            2'b11: rgb_q <= color8_to_rgb(rd_word_q[31:24]);
        endcase
    end
end

wire tmds_ch0_serial;
wire tmds_ch1_serial;
wire tmds_ch2_serial;
wire tmds_chc_serial;

dvi_generator u_dvi (
    .i_pix_clk(pix_clk),
    .i_pix_clk_5x(pix_clk_5x),
    .i_rst(!clk_lock),
    .i_de(de),
    .i_data_ch0(rgb_q[7:0]),
    .i_data_ch1(rgb_q[15:8]),
    .i_data_ch2(rgb_q[23:16]),
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
