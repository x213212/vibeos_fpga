`timescale 1ns/1ps

module pl_mouse_mmio (
    input         clk,
    input         resetn,

    input         dbg_wr_valid,
    input  [7:0]  dbg_wr_addr,
    input  [31:0] dbg_wr_data,

    input         s_axi_awvalid,
    input  [31:0] s_axi_awaddr,
    input  [3:0]  s_axi_awid,
    output        s_axi_awready,
    input         s_axi_wvalid,
    input  [31:0] s_axi_wdata,
    input  [3:0]  s_axi_wstrb,
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

    output [31:0] debug_state,
    output [31:0] debug0,
    output [31:0] debug1,
    output [31:0] debug2,
    output [31:0] debug3,
    output [31:0] debug4,
    output [31:0] debug5,
    output [31:0] debug6,
    output [31:0] debug7
);

reg        bvalid_q;
reg [3:0]  bid_q;
reg        rvalid_q;
reg [3:0]  rid_q;
reg [31:0] rdata_q;
reg        aw_full_q;
reg [31:0] awaddr_q;
reg [3:0]  awid_q;
reg        w_full_q;
reg [31:0] wdata_q;
reg [3:0]  wstrb_q;
reg [15:0] mouse_x_q;
reg [15:0] mouse_y_q;
reg [7:0]  mouse_buttons_q;
reg signed [7:0] mouse_wheel_q;
reg [15:0] mouse_seq_q;
reg [31:0] debug_q [0:7];

assign s_axi_awready = !bvalid_q && !aw_full_q;
assign s_axi_wready  = !bvalid_q && !w_full_q;
assign s_axi_bvalid  = bvalid_q;
assign s_axi_bresp   = 2'b00;
assign s_axi_bid     = bid_q;
assign s_axi_arready = !rvalid_q;
assign s_axi_rvalid  = rvalid_q;
assign s_axi_rdata   = rdata_q;
assign s_axi_rresp   = 2'b00;
assign s_axi_rid     = rid_q;
assign s_axi_rlast   = 1'b1;
assign debug_state   = {mouse_buttons_q, mouse_wheel_q, mouse_seq_q};
assign debug0 = debug_q[0];
assign debug1 = debug_q[1];
assign debug2 = debug_q[2];
assign debug3 = debug_q[3];
assign debug4 = debug_q[4];
assign debug5 = debug_q[5];
assign debug6 = debug_q[6];
assign debug7 = debug_q[7];

wire aw_accept_w = s_axi_awready && s_axi_awvalid;
wire w_accept_w  = s_axi_wready && s_axi_wvalid;
wire wr_fire_w   = !bvalid_q && (aw_full_q || aw_accept_w) && (w_full_q || w_accept_w);
wire [31:0] wr_addr_w = aw_full_q ? awaddr_q : s_axi_awaddr;
wire [3:0]  wr_id_w   = aw_full_q ? awid_q   : s_axi_awid;
wire [31:0] wr_data_w = w_full_q  ? wdata_q  : s_axi_wdata;
wire [3:0]  wr_strb_w = w_full_q  ? wstrb_q  : s_axi_wstrb;

function [15:0] clamp_coord;
    input [31:0] v;
    input [15:0] max_v;
    begin
        clamp_coord = (v > max_v) ? max_v : v[15:0];
    end
endfunction

always @(posedge clk or negedge resetn) begin
    if (!resetn) begin
        bvalid_q <= 1'b0;
        bid_q <= 4'd0;
        rvalid_q <= 1'b0;
        rid_q <= 4'd0;
        rdata_q <= 32'd0;
        aw_full_q <= 1'b0;
        awaddr_q <= 32'd0;
        awid_q <= 4'd0;
        w_full_q <= 1'b0;
        wdata_q <= 32'd0;
        wstrb_q <= 4'd0;
        mouse_x_q <= 16'd256;
        mouse_y_q <= 16'd192;
        mouse_buttons_q <= 8'd0;
        mouse_wheel_q <= 8'sd0;
        mouse_seq_q <= 16'd1;
        debug_q[0] <= 32'd0;
        debug_q[1] <= 32'd0;
        debug_q[2] <= 32'd0;
        debug_q[3] <= 32'd0;
        debug_q[4] <= 32'd0;
        debug_q[5] <= 32'd0;
        debug_q[6] <= 32'd0;
        debug_q[7] <= 32'd0;
    end else begin
        if (dbg_wr_valid) begin
            case (dbg_wr_addr[7:2])
            6'h2c: mouse_x_q <= clamp_coord(dbg_wr_data, 16'd511);
            6'h2d: mouse_y_q <= clamp_coord(dbg_wr_data, 16'd383);
            6'h2e: mouse_buttons_q <= dbg_wr_data[7:0];
            6'h2f: begin
                mouse_wheel_q <= dbg_wr_data[7:0];
                mouse_seq_q <= mouse_seq_q + 16'd1;
            end
            6'h30: begin
                mouse_buttons_q <= dbg_wr_data[31:24];
                mouse_wheel_q <= dbg_wr_data[23:16];
                mouse_y_q <= clamp_coord({16'd0, dbg_wr_data[15:8]}, 16'd383);
                mouse_x_q <= clamp_coord({16'd0, dbg_wr_data[7:0]}, 16'd511);
                mouse_seq_q <= mouse_seq_q + 16'd1;
            end
            default: begin end
            endcase
            if (dbg_wr_addr[7:2] == 6'h2c || dbg_wr_addr[7:2] == 6'h2d || dbg_wr_addr[7:2] == 6'h2e)
                mouse_seq_q <= mouse_seq_q + 16'd1;
        end

        if (aw_accept_w) begin
            aw_full_q <= 1'b1;
            awaddr_q <= s_axi_awaddr;
            awid_q <= s_axi_awid;
        end
        if (w_accept_w) begin
            w_full_q <= 1'b1;
            wdata_q <= s_axi_wdata;
            wstrb_q <= s_axi_wstrb;
        end

        if (wr_fire_w) begin
            bid_q <= wr_id_w;
            bvalid_q <= 1'b1;
            aw_full_q <= 1'b0;
            w_full_q <= 1'b0;
            case (wr_addr_w[5:2])
            4'h1: if (|wr_strb_w) begin mouse_x_q <= clamp_coord(wr_data_w, 16'd511); mouse_seq_q <= mouse_seq_q + 16'd1; end
            4'h2: if (|wr_strb_w) begin mouse_y_q <= clamp_coord(wr_data_w, 16'd383); mouse_seq_q <= mouse_seq_q + 16'd1; end
            4'h3: if (|wr_strb_w) begin mouse_buttons_q <= wr_data_w[7:0]; mouse_seq_q <= mouse_seq_q + 16'd1; end
            4'h4: if (|wr_strb_w) begin mouse_wheel_q <= wr_data_w[7:0]; mouse_seq_q <= mouse_seq_q + 16'd1; end
            4'h8: if (|wr_strb_w) debug_q[0] <= wr_data_w;
            4'h9: if (|wr_strb_w) debug_q[1] <= wr_data_w;
            4'ha: if (|wr_strb_w) debug_q[2] <= wr_data_w;
            4'hb: if (|wr_strb_w) debug_q[3] <= wr_data_w;
            4'hc: if (|wr_strb_w) debug_q[4] <= wr_data_w;
            4'hd: if (|wr_strb_w) debug_q[5] <= wr_data_w;
            4'he: if (|wr_strb_w) debug_q[6] <= wr_data_w;
            4'hf: if (|wr_strb_w) debug_q[7] <= wr_data_w;
            default: begin end
            endcase
        end else if (bvalid_q && s_axi_bready) begin
            bvalid_q <= 1'b0;
        end

        if (s_axi_arvalid && !rvalid_q) begin
            rid_q <= s_axi_arid;
            rvalid_q <= 1'b1;
            case (s_axi_araddr[5:2])
            4'h0: rdata_q <= 32'h4d4f5553; // MOUS
            4'h1: rdata_q <= {16'd0, mouse_x_q};
            4'h2: rdata_q <= {16'd0, mouse_y_q};
            4'h3: rdata_q <= {24'd0, mouse_buttons_q};
            4'h4: rdata_q <= {{24{mouse_wheel_q[7]}}, mouse_wheel_q};
            4'h5: rdata_q <= {16'd0, mouse_seq_q};
            4'h6: rdata_q <= {mouse_y_q, mouse_x_q};
            4'h7: rdata_q <= {mouse_buttons_q, mouse_wheel_q, mouse_seq_q};
            4'h8: rdata_q <= debug_q[0];
            4'h9: rdata_q <= debug_q[1];
            4'ha: rdata_q <= debug_q[2];
            4'hb: rdata_q <= debug_q[3];
            4'hc: rdata_q <= debug_q[4];
            4'hd: rdata_q <= debug_q[5];
            4'he: rdata_q <= debug_q[6];
            4'hf: rdata_q <= debug_q[7];
            default: rdata_q <= 32'd0;
            endcase
            if (s_axi_araddr[5:2] == 4'h4 || s_axi_araddr[5:2] == 4'h7)
                mouse_wheel_q <= 8'sd0;
        end else if (rvalid_q && s_axi_rready) begin
            rvalid_q <= 1'b0;
        end
    end
end

endmodule
