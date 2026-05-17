`timescale 1ns/1ps

module riscv_ps_ddr_top #(
    parameter [31:0] RESET_VECTOR = 32'h80000000,
    parameter [31:0] DDR_CACHE_MIN = 32'h80000000,
    parameter [31:0] DDR_CACHE_MAX = 32'hbfffffff,
    parameter [31:0] PS_DDR_REMAP_BASE = 32'h80000000,
    parameter [31:0] PS_DDR_PHYS_BASE = 32'h01000000
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF M_AXI_I:M_AXI_D:M_AXI_IOP:S_AXI_DBG, ASSOCIATED_RESET resetn" *)
    input clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 resetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input resetn,
    input cpu_resetn,
    output [7:0] led,
    output pl_uart_tx,
    output hdmi_tx_clk_n,
    output hdmi_tx_clk_p,
    output [2:0] hdmi_tx_n,
    output [2:0] hdmi_tx_p,

    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I AWID" *) output [3:0] M_AXI_I_awid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I AWADDR" *) output [31:0] M_AXI_I_awaddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I AWLEN" *) output [7:0] M_AXI_I_awlen,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I AWSIZE" *) output [2:0] M_AXI_I_awsize,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I AWBURST" *) output [1:0] M_AXI_I_awburst,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I AWLOCK" *) output M_AXI_I_awlock,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I AWCACHE" *) output [3:0] M_AXI_I_awcache,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I AWPROT" *) output [2:0] M_AXI_I_awprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I AWVALID" *) output M_AXI_I_awvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I AWREADY" *) input M_AXI_I_awready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I WDATA" *) output [31:0] M_AXI_I_wdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I WSTRB" *) output [3:0] M_AXI_I_wstrb,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I WLAST" *) output M_AXI_I_wlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I WVALID" *) output M_AXI_I_wvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I WREADY" *) input M_AXI_I_wready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I BID" *) input [3:0] M_AXI_I_bid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I BRESP" *) input [1:0] M_AXI_I_bresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I BVALID" *) input M_AXI_I_bvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I BREADY" *) output M_AXI_I_bready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I ARID" *) output [3:0] M_AXI_I_arid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I ARADDR" *) output [31:0] M_AXI_I_araddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I ARLEN" *) output [7:0] M_AXI_I_arlen,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I ARSIZE" *) output [2:0] M_AXI_I_arsize,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I ARBURST" *) output [1:0] M_AXI_I_arburst,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I ARLOCK" *) output M_AXI_I_arlock,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I ARCACHE" *) output [3:0] M_AXI_I_arcache,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I ARPROT" *) output [2:0] M_AXI_I_arprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I ARVALID" *) output M_AXI_I_arvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I ARREADY" *) input M_AXI_I_arready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I RID" *) input [3:0] M_AXI_I_rid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I RDATA" *) input [31:0] M_AXI_I_rdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I RRESP" *) input [1:0] M_AXI_I_rresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I RLAST" *) input M_AXI_I_rlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I RVALID" *) input M_AXI_I_rvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_I RREADY" *) output M_AXI_I_rready,

    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D AWID" *) output [3:0] M_AXI_D_awid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D AWADDR" *) output [31:0] M_AXI_D_awaddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D AWLEN" *) output [7:0] M_AXI_D_awlen,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D AWSIZE" *) output [2:0] M_AXI_D_awsize,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D AWBURST" *) output [1:0] M_AXI_D_awburst,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D AWLOCK" *) output M_AXI_D_awlock,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D AWCACHE" *) output [3:0] M_AXI_D_awcache,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D AWPROT" *) output [2:0] M_AXI_D_awprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D AWVALID" *) output M_AXI_D_awvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D AWREADY" *) input M_AXI_D_awready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D WDATA" *) output [31:0] M_AXI_D_wdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D WSTRB" *) output [3:0] M_AXI_D_wstrb,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D WLAST" *) output M_AXI_D_wlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D WVALID" *) output M_AXI_D_wvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D WREADY" *) input M_AXI_D_wready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D BID" *) input [3:0] M_AXI_D_bid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D BRESP" *) input [1:0] M_AXI_D_bresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D BVALID" *) input M_AXI_D_bvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D BREADY" *) output M_AXI_D_bready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D ARID" *) output [3:0] M_AXI_D_arid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D ARADDR" *) output [31:0] M_AXI_D_araddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D ARLEN" *) output [7:0] M_AXI_D_arlen,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D ARSIZE" *) output [2:0] M_AXI_D_arsize,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D ARBURST" *) output [1:0] M_AXI_D_arburst,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D ARLOCK" *) output M_AXI_D_arlock,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D ARCACHE" *) output [3:0] M_AXI_D_arcache,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D ARPROT" *) output [2:0] M_AXI_D_arprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D ARVALID" *) output M_AXI_D_arvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D ARREADY" *) input M_AXI_D_arready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D RID" *) input [3:0] M_AXI_D_rid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D RDATA" *) input [31:0] M_AXI_D_rdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D RRESP" *) input [1:0] M_AXI_D_rresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D RLAST" *) input M_AXI_D_rlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D RVALID" *) input M_AXI_D_rvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_D RREADY" *) output M_AXI_D_rready,

    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP AWID" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME M_AXI_IOP, PROTOCOL AXI3, DATA_WIDTH 32, ID_WIDTH 4" *)
    output [3:0] M_AXI_IOP_awid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP AWADDR" *) output [31:0] M_AXI_IOP_awaddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP AWLEN" *) output [7:0] M_AXI_IOP_awlen,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP AWSIZE" *) output [2:0] M_AXI_IOP_awsize,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP AWBURST" *) output [1:0] M_AXI_IOP_awburst,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP AWLOCK" *) output M_AXI_IOP_awlock,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP AWCACHE" *) output [3:0] M_AXI_IOP_awcache,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP AWPROT" *) output [2:0] M_AXI_IOP_awprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP AWVALID" *) output M_AXI_IOP_awvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP AWREADY" *) input M_AXI_IOP_awready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP WID" *) output [3:0] M_AXI_IOP_wid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP WDATA" *) output [31:0] M_AXI_IOP_wdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP WSTRB" *) output [3:0] M_AXI_IOP_wstrb,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP WLAST" *) output M_AXI_IOP_wlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP WVALID" *) output M_AXI_IOP_wvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP WREADY" *) input M_AXI_IOP_wready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP BID" *) input [3:0] M_AXI_IOP_bid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP BRESP" *) input [1:0] M_AXI_IOP_bresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP BVALID" *) input M_AXI_IOP_bvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP BREADY" *) output M_AXI_IOP_bready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP ARID" *) output [3:0] M_AXI_IOP_arid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP ARADDR" *) output [31:0] M_AXI_IOP_araddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP ARLEN" *) output [7:0] M_AXI_IOP_arlen,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP ARSIZE" *) output [2:0] M_AXI_IOP_arsize,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP ARBURST" *) output [1:0] M_AXI_IOP_arburst,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP ARLOCK" *) output M_AXI_IOP_arlock,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP ARCACHE" *) output [3:0] M_AXI_IOP_arcache,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP ARPROT" *) output [2:0] M_AXI_IOP_arprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP ARVALID" *) output M_AXI_IOP_arvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP ARREADY" *) input M_AXI_IOP_arready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP RID" *) input [3:0] M_AXI_IOP_rid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP RDATA" *) input [31:0] M_AXI_IOP_rdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP RRESP" *) input [1:0] M_AXI_IOP_rresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP RLAST" *) input M_AXI_IOP_rlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP RVALID" *) input M_AXI_IOP_rvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI_IOP RREADY" *) output M_AXI_IOP_rready,

    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG AWADDR" *) input [31:0] S_AXI_DBG_awaddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG AWVALID" *) input S_AXI_DBG_awvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG AWREADY" *) output S_AXI_DBG_awready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG WDATA" *) input [31:0] S_AXI_DBG_wdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG WSTRB" *) input [3:0] S_AXI_DBG_wstrb,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG WVALID" *) input S_AXI_DBG_wvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG WREADY" *) output S_AXI_DBG_wready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG BRESP" *) output [1:0] S_AXI_DBG_bresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG BVALID" *) output S_AXI_DBG_bvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG BREADY" *) input S_AXI_DBG_bready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG ARADDR" *) input [31:0] S_AXI_DBG_araddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG ARVALID" *) input S_AXI_DBG_arvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG ARREADY" *) output S_AXI_DBG_arready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG RDATA" *) output [31:0] S_AXI_DBG_rdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG RRESP" *) output [1:0] S_AXI_DBG_rresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG RVALID" *) output S_AXI_DBG_rvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI_DBG RREADY" *) input S_AXI_DBG_rready,

    input eth_dbg_tx_clk,
    input eth_dbg_rx_clk,
    input eth_dbg_rx_dv,
    input [3:0] eth_dbg_rxd,
    input eth_dbg_tx_en,
    input [3:0] eth_dbg_txd
);

wire core_rst = !resetn || !cpu_resetn;
reg [23:0] alive_q;
(* ASYNC_REG = "TRUE" *) reg [2:0] eth_tx_clk_sync_q;
(* ASYNC_REG = "TRUE" *) reg [2:0] eth_rx_clk_sync_q;
(* ASYNC_REG = "TRUE" *) reg [2:0] eth_rx_dv_sync_q;
(* ASYNC_REG = "TRUE" *) reg [2:0] eth_tx_en_sync_q;
(* ASYNC_REG = "TRUE" *) reg [3:0] eth_rxd_sync0_q;
(* ASYNC_REG = "TRUE" *) reg [3:0] eth_rxd_sync1_q;
(* ASYNC_REG = "TRUE" *) reg [3:0] eth_txd_sync0_q;
(* ASYNC_REG = "TRUE" *) reg [3:0] eth_txd_sync1_q;
reg [31:0] eth_txclk_edge_count_q;
reg [31:0] eth_rxclk_edge_count_q;
reg [31:0] eth_rxdv_edge_count_q;
reg [31:0] eth_txen_edge_count_q;
reg [31:0] eth_rx_activity_count_q;
reg [31:0] eth_tx_activity_count_q;
reg [31:0] eth_pin_sample_q;
(* DONT_TOUCH = "TRUE" *) reg [31:0] eth_txclk_domain_count_q;
(* DONT_TOUCH = "TRUE" *) reg [31:0] eth_rxclk_domain_count_q;
reg [31:0] dbg_i_ar_count_q;
reg [31:0] dbg_i_r_count_q;
reg [31:0] dbg_d_aw_count_q;
reg [31:0] dbg_d_w_count_q;
reg [31:0] dbg_d_b_count_q;
reg [31:0] dbg_d_ar_count_q;
reg [31:0] dbg_d_r_count_q;
reg [31:0] dbg_mmio_aw_count_q;
reg [31:0] dbg_mmio_ar_count_q;
reg [31:0] dbg_iop_aw_count_q;
reg [31:0] dbg_iop_w_count_q;
reg [31:0] dbg_iop_b_count_q;
reg [31:0] dbg_iop_ar_count_q;
reg [31:0] dbg_iop_r_count_q;
reg [31:0] dbg_i_araddr_q;
reg [31:0] dbg_i_ps_araddr_q;
reg [31:0] dbg_i_rdata_q;
reg [31:0] dbg_i_rdata0_q;
reg [31:0] dbg_i_rdata1_q;
reg [31:0] dbg_i_rdata2_q;
reg [31:0] dbg_i_rdata3_q;
reg [1:0] dbg_i_rresp_or_q;
reg [31:0] dbg_d_awaddr_q;
reg [31:0] dbg_d_wdata_q;
reg [31:0] dbg_d_awlen_wlast_q;
reg [31:0] dbg_d_wlast_count_q;
reg [31:0] dbg_d_araddr_q;
reg [31:0] dbg_d_ps_araddr_q;
reg [31:0] dbg_d_rdata_q;
reg [1:0] dbg_d_bresp_or_q;
reg [1:0] dbg_d_rresp_or_q;
reg [31:0] dbg_mmio_awaddr_q;
reg [31:0] dbg_mmio_wdata_q;
reg [31:0] dbg_mmio_araddr_q;
reg [31:0] dbg_mmio_rdata_q;
reg [31:0] dbg_mmio_wdata0_q;
reg [31:0] dbg_mmio_wdata1_q;
reg [31:0] dbg_iop_awaddr_q;
reg [31:0] dbg_iop_wdata_q;
reg [31:0] dbg_iop_araddr_q;
reg [31:0] dbg_iop_rdata_q;
reg [1:0] dbg_iop_resp_or_q;
reg dbg_awready_q;
reg dbg_wready_q;
reg dbg_bvalid_q;
reg dbg_arready_q;
reg dbg_rvalid_q;
reg [31:0] dbg_rdata_q;
wire [31:0] axi_i_awaddr_w;
wire [31:0] axi_i_araddr_w;
wire [31:0] axi_d_awaddr_w;
wire [31:0] axi_d_araddr_w;
wire [31:0] cpu_axi_d_awaddr_w;
wire [31:0] cpu_axi_d_araddr_w;
wire [3:0] cpu_axi_d_awid_w;
wire [7:0] cpu_axi_d_awlen_w;
wire [1:0] cpu_axi_d_awburst_w;
wire cpu_axi_d_awvalid_w;
wire cpu_axi_d_awready_w;
wire [31:0] cpu_axi_d_wdata_w;
wire [3:0] cpu_axi_d_wstrb_w;
wire cpu_axi_d_wlast_w;
wire cpu_axi_d_wvalid_w;
wire cpu_axi_d_wready_w;
wire [3:0] cpu_axi_d_bid_w;
wire [1:0] cpu_axi_d_bresp_w;
wire cpu_axi_d_bvalid_w;
wire cpu_axi_d_bready_w;
wire [3:0] cpu_axi_d_arid_w;
wire [7:0] cpu_axi_d_arlen_w;
wire [1:0] cpu_axi_d_arburst_w;
wire cpu_axi_d_arvalid_w;
wire cpu_axi_d_arready_w;
wire [3:0] cpu_axi_d_rid_w;
wire [31:0] cpu_axi_d_rdata_w;
wire [1:0] cpu_axi_d_rresp_w;
wire cpu_axi_d_rlast_w;
wire cpu_axi_d_rvalid_w;
wire cpu_axi_d_rready_w;

wire console_aw_sel_w = ((cpu_axi_d_awaddr_w & 32'hfffffff0) == 32'h10000000) ||
                        ((cpu_axi_d_awaddr_w & 32'hffff0000) == 32'h02000000) ||
                        ((cpu_axi_d_awaddr_w & 32'hff000000) == 32'h0c000000);
wire console_ar_sel_w = ((cpu_axi_d_araddr_w & 32'hfffffff0) == 32'h10000000) ||
                        ((cpu_axi_d_araddr_w & 32'hffff0000) == 32'h02000000) ||
                        ((cpu_axi_d_araddr_w & 32'hff000000) == 32'h0c000000);
wire mouse_aw_sel_w = ((cpu_axi_d_awaddr_w & 32'hfffff000) == 32'h10004000);
wire mouse_ar_sel_w = ((cpu_axi_d_araddr_w & 32'hfffff000) == 32'h10004000);
wire video_aw_sel_w = ((cpu_axi_d_awaddr_w & 32'hffff0000) == 32'h40000000) ||
                      ((cpu_axi_d_awaddr_w & 32'hfff00000) == 32'h50000000);
wire video_ar_sel_w = ((cpu_axi_d_araddr_w & 32'hffff0000) == 32'h40000000) ||
                      ((cpu_axi_d_araddr_w & 32'hfff00000) == 32'h50000000);
wire mmio_aw_sel_w = console_aw_sel_w || mouse_aw_sel_w || video_aw_sel_w;
wire mmio_ar_sel_w = console_ar_sel_w || mouse_ar_sel_w || video_ar_sel_w;
wire ps_iop_aw_sel_w = ((cpu_axi_d_awaddr_w & 32'hff000000) == 32'he0000000) ||
                       ((cpu_axi_d_awaddr_w & 32'hff000000) == 32'hf8000000);
wire ps_iop_ar_sel_w = ((cpu_axi_d_araddr_w & 32'hff000000) == 32'he0000000) ||
                       ((cpu_axi_d_araddr_w & 32'hff000000) == 32'hf8000000);
reg d_aw_pending_q;
reg d_aw_mmio_q;
reg d_aw_iop_q;
reg d_aw_video_q;
reg d_aw_mouse_q;
reg [7:0] d_awlen_q;
reg [7:0] d_w_count_q;
reg ddr_awvalid_q;
reg [31:0] ddr_awaddr_q;
reg [3:0] ddr_awid_q;
reg [7:0] ddr_awlen_q;
reg [1:0] ddr_awburst_q;
reg iop_aw_buf_valid_q;
reg iop_w_buf_valid_q;
reg iop_aw_sent_q;
reg iop_w_sent_q;
reg [31:0] iop_awaddr_q;
reg [3:0] iop_awid_q;
reg [7:0] iop_awlen_q;
reg [1:0] iop_awburst_q;
reg [31:0] iop_wdata_q;
reg [3:0] iop_wstrb_q;
reg iop_wlast_q;
reg d_ar_pending_q;
reg d_ar_mmio_q;
reg d_ar_iop_q;
reg d_ar_video_q;
reg d_ar_mouse_q;
wire d_aw_route_mmio_w = d_aw_pending_q ? d_aw_mmio_q : mmio_aw_sel_w;
wire d_ar_route_mmio_w = d_ar_pending_q ? d_ar_mmio_q : mmio_ar_sel_w;
wire d_aw_route_iop_w = d_aw_pending_q ? d_aw_iop_q : ps_iop_aw_sel_w;
wire d_ar_route_iop_w = d_ar_pending_q ? d_ar_iop_q : ps_iop_ar_sel_w;
wire d_aw_route_video_w = d_aw_pending_q ? d_aw_video_q : video_aw_sel_w;
wire d_ar_route_video_w = d_ar_pending_q ? d_ar_video_q : video_ar_sel_w;
wire d_aw_route_mouse_w = d_aw_pending_q ? d_aw_mouse_q : mouse_aw_sel_w;
wire d_ar_route_mouse_w = d_ar_pending_q ? d_ar_mouse_q : mouse_ar_sel_w;
wire d_aw_route_console_w = d_aw_route_mmio_w && !d_aw_route_video_w && !d_aw_route_mouse_w;
wire d_ar_route_console_w = d_ar_route_mmio_w && !d_ar_route_video_w && !d_ar_route_mouse_w;
wire d_aw_route_ddr_w = !d_aw_route_mmio_w && !d_aw_route_iop_w;
wire d_ar_route_ddr_w = !d_ar_route_mmio_w && !d_ar_route_iop_w;
wire ddr_aw_sel_w = d_aw_route_ddr_w;
wire ddr_ar_sel_w = d_ar_route_ddr_w;
wire ddr_write_idle_w = !ddr_awvalid_q && !d_aw_pending_q;
wire ddr_write_accept_w = cpu_axi_d_awvalid_w && cpu_axi_d_wvalid_w &&
                          ddr_aw_sel_w && ddr_write_idle_w;
wire [7:0] d_active_awlen_w = d_aw_pending_q ? d_awlen_q : cpu_axi_d_awlen_w;
wire d_ddr_wlast_gen_w = d_aw_route_ddr_w && (d_w_count_q == d_active_awlen_w);
wire d_ddr_wlast_w = cpu_axi_d_wlast_w || d_ddr_wlast_gen_w;
wire iop_aw_capture_w = cpu_axi_d_awvalid_w && ps_iop_aw_sel_w &&
                        !iop_aw_buf_valid_q && !iop_w_buf_valid_q;
wire iop_w_capture_w = cpu_axi_d_wvalid_w && (d_aw_route_iop_w || ps_iop_aw_sel_w) &&
                       (iop_aw_buf_valid_q || iop_aw_capture_w) && !iop_w_buf_valid_q;
wire iop_write_ready_w = iop_aw_buf_valid_q && iop_w_buf_valid_q;
wire mmio_timer_intr_w;
wire mmio_ext_intr_w;
wire mmio_awready_w;
wire mmio_wready_w;
wire mmio_bvalid_w;
wire [1:0] mmio_bresp_w;
wire [3:0] mmio_bid_w;
wire mmio_arready_w;
wire mmio_rvalid_w;
wire [31:0] mmio_rdata_w;
wire [1:0] mmio_rresp_w;
wire [3:0] mmio_rid_w;
wire mmio_rlast_w;
wire mmio_rready_route_w;
wire [31:0] mmio_debug_fifo_status_w;
wire [31:0] mmio_debug_enqueue_count_w;
wire [31:0] mmio_debug_dequeue_count_w;
wire [31:0] mmio_debug_last_bytes_w;
wire [31:0] mmio_debug_jtag_state_w;
wire [31:0] mmio_debug_axi_counts_w;
wire [31:0] mmio_debug_uart_counts_w;
wire [31:0] mmio_debug_last_uart_decode_addr_w;
wire [31:0] mmio_debug_last_wstrb_wdata_w;
wire [31:0] mmio_debug_clint_mtime_w;
wire [31:0] mmio_debug_clint_mtimecmp_w;
wire [31:0] mmio_debug_clint_status_w;
wire [31:0] mmio_debug_plic_status_w;
wire [31:0] mmio_debug_plic_claim_w;
wire dbg_write_fire_w;
wire dbg_irq_clear_w;
wire mmio_ps_console_valid_w;
wire [7:0] mmio_ps_console_data_w;
reg mmio_ps_console_pop_q;
wire console_awready_w;
wire console_wready_w;
wire console_bvalid_w;
wire [1:0] console_bresp_w;
wire [3:0] console_bid_w;
wire console_arready_w;
wire console_rvalid_w;
wire [31:0] console_rdata_w;
wire [1:0] console_rresp_w;
wire [3:0] console_rid_w;
wire console_rlast_w;
wire video_awready_w;
wire video_wready_w;
wire video_bvalid_w;
wire [1:0] video_bresp_w;
wire [3:0] video_bid_w;
wire video_arready_w;
wire video_rvalid_w;
wire [31:0] video_rdata_w;
wire [1:0] video_rresp_w;
wire [3:0] video_rid_w;
wire video_rlast_w;
wire mouse_awready_w;
wire mouse_wready_w;
wire mouse_bvalid_w;
wire [1:0] mouse_bresp_w;
wire [3:0] mouse_bid_w;
wire mouse_arready_w;
wire mouse_rvalid_w;
wire [31:0] mouse_rdata_w;
wire [1:0] mouse_rresp_w;
wire [3:0] mouse_rid_w;
wire mouse_rlast_w;
wire [31:0] mouse_debug_state_w;
wire [31:0] mouse_debug0_w;
wire [31:0] mouse_debug1_w;
wire [31:0] mouse_debug2_w;
wire [31:0] mouse_debug3_w;
wire [31:0] mouse_debug4_w;
wire [31:0] mouse_debug5_w;
wire [31:0] mouse_debug6_w;
wire [31:0] mouse_debug7_w;

function [31:0] remap_ps_ddr_addr;
    input [31:0] addr;
    begin
        if (addr >= DDR_CACHE_MIN && addr <= DDR_CACHE_MAX)
            remap_ps_ddr_addr = addr - PS_DDR_REMAP_BASE + PS_DDR_PHYS_BASE;
        else
            remap_ps_ddr_addr = addr;
    end
endfunction

always @(posedge clk or negedge resetn) begin
    if (!resetn)
        alive_q <= 24'd0;
    else
        alive_q <= alive_q + 24'd1;
end

always @(posedge clk or negedge resetn) begin
    if (!resetn) begin
        eth_tx_clk_sync_q <= 3'd0;
        eth_rx_clk_sync_q <= 3'd0;
        eth_rx_dv_sync_q <= 3'd0;
        eth_tx_en_sync_q <= 3'd0;
        eth_rxd_sync0_q <= 4'd0;
        eth_rxd_sync1_q <= 4'd0;
        eth_txd_sync0_q <= 4'd0;
        eth_txd_sync1_q <= 4'd0;
        eth_txclk_edge_count_q <= 32'd0;
        eth_rxclk_edge_count_q <= 32'd0;
        eth_rxdv_edge_count_q <= 32'd0;
        eth_txen_edge_count_q <= 32'd0;
        eth_rx_activity_count_q <= 32'd0;
        eth_tx_activity_count_q <= 32'd0;
        eth_pin_sample_q <= 32'd0;
    end else begin
        eth_tx_clk_sync_q <= {eth_tx_clk_sync_q[1:0], eth_dbg_tx_clk};
        eth_rx_clk_sync_q <= {eth_rx_clk_sync_q[1:0], eth_dbg_rx_clk};
        eth_rx_dv_sync_q <= {eth_rx_dv_sync_q[1:0], eth_dbg_rx_dv};
        eth_tx_en_sync_q <= {eth_tx_en_sync_q[1:0], eth_dbg_tx_en};
        eth_rxd_sync0_q <= eth_dbg_rxd;
        eth_rxd_sync1_q <= eth_rxd_sync0_q;
        eth_txd_sync0_q <= eth_dbg_txd;
        eth_txd_sync1_q <= eth_txd_sync0_q;

        if (eth_tx_clk_sync_q[2] ^ eth_tx_clk_sync_q[1])
            eth_txclk_edge_count_q <= eth_txclk_edge_count_q + 32'd1;
        if (eth_rx_clk_sync_q[2] ^ eth_rx_clk_sync_q[1])
            eth_rxclk_edge_count_q <= eth_rxclk_edge_count_q + 32'd1;
        if (eth_rx_dv_sync_q[2] ^ eth_rx_dv_sync_q[1])
            eth_rxdv_edge_count_q <= eth_rxdv_edge_count_q + 32'd1;
        if (eth_tx_en_sync_q[2] ^ eth_tx_en_sync_q[1])
            eth_txen_edge_count_q <= eth_txen_edge_count_q + 32'd1;
        if (eth_rx_dv_sync_q[2])
            eth_rx_activity_count_q <= eth_rx_activity_count_q + 32'd1;
        if (eth_tx_en_sync_q[2])
            eth_tx_activity_count_q <= eth_tx_activity_count_q + 32'd1;

        eth_pin_sample_q <= {
            12'd0,
            eth_rx_clk_sync_q[2],
            eth_tx_clk_sync_q[2],
            eth_rx_dv_sync_q[2],
            eth_tx_en_sync_q[2],
            eth_rxd_sync1_q,
            eth_txd_sync1_q,
            8'd0
        };
    end
end

always @(posedge eth_dbg_tx_clk or negedge resetn) begin
    if (!resetn)
        eth_txclk_domain_count_q <= 32'd0;
    else
        eth_txclk_domain_count_q <= eth_txclk_domain_count_q + 32'd1;
end

always @(posedge eth_dbg_rx_clk or negedge resetn) begin
    if (!resetn)
        eth_rxclk_domain_count_q <= 32'd0;
    else
        eth_rxclk_domain_count_q <= eth_rxclk_domain_count_q + 32'd1;
end

wire eth_rx_irq_event_w = eth_rx_dv_sync_q[1] && !eth_rx_dv_sync_q[2];

always @(posedge clk or negedge resetn) begin
    if (!resetn || !cpu_resetn) begin
        d_aw_pending_q <= 1'b0;
        d_aw_mmio_q <= 1'b0;
        d_aw_iop_q <= 1'b0;
        d_aw_video_q <= 1'b0;
        d_aw_mouse_q <= 1'b0;
        d_awlen_q <= 8'd0;
        d_w_count_q <= 8'd0;
        ddr_awvalid_q <= 1'b0;
        ddr_awaddr_q <= 32'd0;
        ddr_awid_q <= 4'd0;
        ddr_awlen_q <= 8'd0;
        ddr_awburst_q <= 2'd0;
        iop_aw_buf_valid_q <= 1'b0;
        iop_w_buf_valid_q <= 1'b0;
        iop_aw_sent_q <= 1'b0;
        iop_w_sent_q <= 1'b0;
        iop_awaddr_q <= 32'd0;
        iop_awid_q <= 4'd0;
        iop_awlen_q <= 8'd0;
        iop_awburst_q <= 2'd0;
        iop_wdata_q <= 32'd0;
        iop_wstrb_q <= 4'd0;
        iop_wlast_q <= 1'b0;
        d_ar_pending_q <= 1'b0;
        d_ar_mmio_q <= 1'b0;
        d_ar_iop_q <= 1'b0;
        d_ar_video_q <= 1'b0;
        d_ar_mouse_q <= 1'b0;
        dbg_i_ar_count_q <= 32'd0;
        dbg_i_r_count_q <= 32'd0;
        dbg_d_aw_count_q <= 32'd0;
        dbg_d_w_count_q <= 32'd0;
        dbg_d_b_count_q <= 32'd0;
        dbg_d_ar_count_q <= 32'd0;
        dbg_d_r_count_q <= 32'd0;
        dbg_mmio_aw_count_q <= 32'd0;
        dbg_mmio_ar_count_q <= 32'd0;
        dbg_iop_aw_count_q <= 32'd0;
        dbg_iop_w_count_q <= 32'd0;
        dbg_iop_b_count_q <= 32'd0;
        dbg_iop_ar_count_q <= 32'd0;
        dbg_iop_r_count_q <= 32'd0;
        dbg_i_araddr_q <= 32'd0;
        dbg_i_ps_araddr_q <= 32'd0;
        dbg_i_rdata_q <= 32'd0;
        dbg_i_rdata0_q <= 32'd0;
        dbg_i_rdata1_q <= 32'd0;
        dbg_i_rdata2_q <= 32'd0;
        dbg_i_rdata3_q <= 32'd0;
        dbg_i_rresp_or_q <= 2'd0;
        dbg_d_awaddr_q <= 32'd0;
        dbg_d_wdata_q <= 32'd0;
        dbg_d_awlen_wlast_q <= 32'd0;
        dbg_d_wlast_count_q <= 32'd0;
        dbg_d_araddr_q <= 32'd0;
        dbg_d_ps_araddr_q <= 32'd0;
        dbg_d_rdata_q <= 32'd0;
        dbg_d_bresp_or_q <= 2'd0;
        dbg_d_rresp_or_q <= 2'd0;
        dbg_mmio_awaddr_q <= 32'd0;
        dbg_mmio_wdata_q <= 32'd0;
        dbg_mmio_araddr_q <= 32'd0;
        dbg_mmio_rdata_q <= 32'd0;
        dbg_mmio_wdata0_q <= 32'd0;
        dbg_mmio_wdata1_q <= 32'd0;
        dbg_iop_awaddr_q <= 32'd0;
        dbg_iop_wdata_q <= 32'd0;
        dbg_iop_araddr_q <= 32'd0;
        dbg_iop_rdata_q <= 32'd0;
        dbg_iop_resp_or_q <= 2'd0;
    end else begin
        if ((M_AXI_D_wvalid && M_AXI_D_wready && M_AXI_D_wlast) ||
            (M_AXI_D_bvalid && M_AXI_D_bready))
            ddr_awvalid_q <= 1'b0;
        if (ddr_write_accept_w) begin
            ddr_awvalid_q <= 1'b1;
            ddr_awaddr_q <= axi_d_awaddr_w;
            ddr_awid_q <= cpu_axi_d_awid_w;
            ddr_awlen_q <= cpu_axi_d_awlen_w;
            ddr_awburst_q <= cpu_axi_d_awburst_w;
        end

        if (iop_aw_capture_w) begin
            iop_aw_buf_valid_q <= 1'b1;
            iop_aw_sent_q <= 1'b0;
            iop_awaddr_q <= cpu_axi_d_awaddr_w;
            iop_awid_q <= cpu_axi_d_awid_w;
            iop_awlen_q <= cpu_axi_d_awlen_w;
            iop_awburst_q <= cpu_axi_d_awburst_w;
        end
        if (iop_w_capture_w) begin
            iop_w_buf_valid_q <= 1'b1;
            iop_w_sent_q <= 1'b0;
            iop_wdata_q <= cpu_axi_d_wdata_w;
            iop_wstrb_q <= cpu_axi_d_wstrb_w;
            iop_wlast_q <= cpu_axi_d_wlast_w ||
                           ((iop_aw_capture_w ? cpu_axi_d_awlen_w : iop_awlen_q) == 8'd0);
        end
        if (M_AXI_IOP_awvalid && M_AXI_IOP_awready)
            iop_aw_sent_q <= 1'b1;
        if (M_AXI_IOP_wvalid && M_AXI_IOP_wready)
            iop_w_sent_q <= 1'b1;
        if (M_AXI_IOP_bvalid && M_AXI_IOP_bready) begin
            iop_aw_buf_valid_q <= 1'b0;
            iop_w_buf_valid_q <= 1'b0;
            iop_aw_sent_q <= 1'b0;
            iop_w_sent_q <= 1'b0;
        end

        if (cpu_axi_d_awvalid_w && cpu_axi_d_awready_w) begin
            d_aw_pending_q <= 1'b1;
            d_aw_mmio_q <= mmio_aw_sel_w;
            d_aw_iop_q <= ps_iop_aw_sel_w;
            d_aw_video_q <= video_aw_sel_w;
            d_aw_mouse_q <= mouse_aw_sel_w;
            d_awlen_q <= cpu_axi_d_awlen_w;
            d_w_count_q <= 8'd0;
        end else if (cpu_axi_d_bvalid_w && cpu_axi_d_bready_w) begin
            d_aw_pending_q <= 1'b0;
            d_aw_mmio_q <= 1'b0;
            d_aw_iop_q <= 1'b0;
            d_aw_video_q <= 1'b0;
            d_aw_mouse_q <= 1'b0;
        end

        if (M_AXI_D_wvalid && M_AXI_D_wready) begin
            if (M_AXI_D_wlast)
                d_w_count_q <= 8'd0;
            else
                d_w_count_q <= d_w_count_q + 8'd1;
        end

        if (cpu_axi_d_arvalid_w && cpu_axi_d_arready_w) begin
            d_ar_pending_q <= 1'b1;
            d_ar_mmio_q <= mmio_ar_sel_w;
            d_ar_iop_q <= ps_iop_ar_sel_w;
            d_ar_video_q <= video_ar_sel_w;
            d_ar_mouse_q <= mouse_ar_sel_w;
        end else if (cpu_axi_d_rvalid_w && cpu_axi_d_rready_w && cpu_axi_d_rlast_w) begin
            d_ar_pending_q <= 1'b0;
            d_ar_iop_q <= 1'b0;
            d_ar_video_q <= 1'b0;
            d_ar_mouse_q <= 1'b0;
        end

        if (M_AXI_I_arvalid && M_AXI_I_arready) begin
            dbg_i_ar_count_q <= dbg_i_ar_count_q + 32'd1;
            dbg_i_araddr_q <= axi_i_araddr_w;
            dbg_i_ps_araddr_q <= M_AXI_I_araddr;
        end
        if (M_AXI_I_rvalid && M_AXI_I_rready) begin
            dbg_i_r_count_q <= dbg_i_r_count_q + 32'd1;
            dbg_i_rdata_q <= M_AXI_I_rdata;
            dbg_i_rresp_or_q <= dbg_i_rresp_or_q | M_AXI_I_rresp;
            if (dbg_i_r_count_q == 32'd0)
                dbg_i_rdata0_q <= M_AXI_I_rdata;
            else if (dbg_i_r_count_q == 32'd1)
                dbg_i_rdata1_q <= M_AXI_I_rdata;
            else if (dbg_i_r_count_q == 32'd2)
                dbg_i_rdata2_q <= M_AXI_I_rdata;
            else if (dbg_i_r_count_q == 32'd3)
                dbg_i_rdata3_q <= M_AXI_I_rdata;
        end
        if (M_AXI_D_awvalid && M_AXI_D_awready) begin
            dbg_d_aw_count_q <= dbg_d_aw_count_q + 32'd1;
            dbg_d_awaddr_q <= M_AXI_D_awaddr;
            dbg_d_awlen_wlast_q <= {
                8'd0,
                d_aw_pending_q,
                d_aw_route_mmio_w,
                cpu_axi_d_wlast_w,
                d_ddr_wlast_gen_w,
                M_AXI_D_wlast,
                3'd0,
                cpu_axi_d_awlen_w,
                d_w_count_q
            };
        end
        if (M_AXI_D_wvalid && M_AXI_D_wready) begin
            dbg_d_w_count_q <= dbg_d_w_count_q + 32'd1;
            dbg_d_wdata_q <= M_AXI_D_wdata;
            dbg_d_awlen_wlast_q <= {
                8'd0,
                d_aw_pending_q,
                d_aw_route_mmio_w,
                cpu_axi_d_wlast_w,
                d_ddr_wlast_gen_w,
                M_AXI_D_wlast,
                3'd0,
                d_active_awlen_w,
                d_w_count_q
            };
            if (M_AXI_D_wlast)
                dbg_d_wlast_count_q <= dbg_d_wlast_count_q + 32'd1;
        end
        if (M_AXI_D_bvalid && M_AXI_D_bready)
            dbg_d_b_count_q <= dbg_d_b_count_q + 32'd1;
        if (M_AXI_D_bvalid && M_AXI_D_bready)
            dbg_d_bresp_or_q <= dbg_d_bresp_or_q | M_AXI_D_bresp;
        if (M_AXI_D_arvalid && M_AXI_D_arready) begin
            dbg_d_ar_count_q <= dbg_d_ar_count_q + 32'd1;
            dbg_d_araddr_q <= cpu_axi_d_araddr_w;
            dbg_d_ps_araddr_q <= M_AXI_D_araddr;
        end
        if (M_AXI_D_rvalid && M_AXI_D_rready) begin
            dbg_d_r_count_q <= dbg_d_r_count_q + 32'd1;
            dbg_d_rdata_q <= M_AXI_D_rdata;
            dbg_d_rresp_or_q <= dbg_d_rresp_or_q | M_AXI_D_rresp;
        end
        if (M_AXI_IOP_awvalid && M_AXI_IOP_awready) begin
            dbg_iop_aw_count_q <= dbg_iop_aw_count_q + 32'd1;
            dbg_iop_awaddr_q <= M_AXI_IOP_awaddr;
        end
        if (M_AXI_IOP_wvalid && M_AXI_IOP_wready) begin
            dbg_iop_w_count_q <= dbg_iop_w_count_q + 32'd1;
            dbg_iop_wdata_q <= M_AXI_IOP_wdata;
        end
        if (M_AXI_IOP_bvalid && M_AXI_IOP_bready) begin
            dbg_iop_b_count_q <= dbg_iop_b_count_q + 32'd1;
            dbg_iop_resp_or_q <= dbg_iop_resp_or_q | M_AXI_IOP_bresp;
        end
        if (M_AXI_IOP_arvalid && M_AXI_IOP_arready) begin
            dbg_iop_ar_count_q <= dbg_iop_ar_count_q + 32'd1;
            dbg_iop_araddr_q <= M_AXI_IOP_araddr;
        end
        if (M_AXI_IOP_rvalid && M_AXI_IOP_rready) begin
            dbg_iop_r_count_q <= dbg_iop_r_count_q + 32'd1;
            dbg_iop_rdata_q <= M_AXI_IOP_rdata;
            dbg_iop_resp_or_q <= dbg_iop_resp_or_q | M_AXI_IOP_rresp;
        end
        if (cpu_axi_d_awvalid_w && mmio_aw_sel_w && mmio_awready_w) begin
            dbg_mmio_aw_count_q <= dbg_mmio_aw_count_q + 32'd1;
            dbg_mmio_awaddr_q <= cpu_axi_d_awaddr_w;
            dbg_mmio_wdata_q <= cpu_axi_d_wdata_w;
            if (dbg_mmio_aw_count_q == 32'd0)
                dbg_mmio_wdata0_q <= cpu_axi_d_wdata_w;
            else if (dbg_mmio_aw_count_q == 32'd1)
                dbg_mmio_wdata1_q <= cpu_axi_d_wdata_w;
        end
        if (cpu_axi_d_arvalid_w && mmio_ar_sel_w && mmio_arready_w) begin
            dbg_mmio_ar_count_q <= dbg_mmio_ar_count_q + 32'd1;
            dbg_mmio_araddr_q <= cpu_axi_d_araddr_w;
        end
        if (mmio_rvalid_w && mmio_rready_route_w) begin
            dbg_mmio_rdata_q <= mmio_rdata_w;
        end
    end
end

always @(posedge clk or negedge resetn) begin
    if (!resetn) begin
        dbg_awready_q <= 1'b0;
        dbg_wready_q <= 1'b0;
        dbg_bvalid_q <= 1'b0;
        dbg_arready_q <= 1'b0;
        dbg_rvalid_q <= 1'b0;
        dbg_rdata_q <= 32'd0;
        mmio_ps_console_pop_q <= 1'b0;
    end else begin
        mmio_ps_console_pop_q <= 1'b0;
        dbg_awready_q <= S_AXI_DBG_awvalid && S_AXI_DBG_wvalid && !dbg_bvalid_q;
        dbg_wready_q <= S_AXI_DBG_awvalid && S_AXI_DBG_wvalid && !dbg_bvalid_q;
        if (dbg_awready_q && dbg_wready_q)
            dbg_bvalid_q <= 1'b1;
        else if (S_AXI_DBG_bready)
            dbg_bvalid_q <= 1'b0;

        dbg_arready_q <= S_AXI_DBG_arvalid && !dbg_rvalid_q;
        if (S_AXI_DBG_arvalid && !dbg_rvalid_q) begin
            dbg_rvalid_q <= 1'b1;
            case (S_AXI_DBG_araddr[8:2])
                6'h00: dbg_rdata_q <= 32'h44524731;
                6'h01: dbg_rdata_q <= {
                    8'd0,
                    cpu_axi_d_rready_w,
                    cpu_axi_d_rvalid_w,
                    d_ar_route_mmio_w,
                    d_ar_pending_q,
                    M_AXI_D_rlast,
                    M_AXI_D_rready,
                    M_AXI_D_rvalid,
                    M_AXI_D_arready,
                    M_AXI_D_arvalid,
                    mmio_awready_w,
                    mmio_aw_sel_w,
                    M_AXI_D_bready,
                    M_AXI_D_bvalid,
                    M_AXI_D_wready,
                    M_AXI_D_wvalid,
                    M_AXI_D_awready,
                    M_AXI_D_awvalid,
                    M_AXI_I_rready,
                    M_AXI_I_rvalid,
                    M_AXI_I_arready,
                    M_AXI_I_arvalid,
                    core_rst,
                    cpu_resetn,
                    resetn
                };
                6'h02: dbg_rdata_q <= dbg_i_ar_count_q;
                6'h03: dbg_rdata_q <= dbg_i_r_count_q;
                6'h04: dbg_rdata_q <= dbg_d_aw_count_q;
                6'h05: dbg_rdata_q <= dbg_d_w_count_q;
                6'h06: dbg_rdata_q <= dbg_d_b_count_q;
                6'h07: dbg_rdata_q <= dbg_mmio_aw_count_q;
                6'h08: dbg_rdata_q <= dbg_i_araddr_q;
                6'h09: dbg_rdata_q <= dbg_i_ps_araddr_q;
                6'h0a: dbg_rdata_q <= dbg_i_rdata_q;
                6'h0b: dbg_rdata_q <= dbg_d_awaddr_q;
                6'h0c: dbg_rdata_q <= dbg_d_wdata_q;
                6'h0d: dbg_rdata_q <= dbg_mmio_awaddr_q;
                6'h0e: dbg_rdata_q <= dbg_mmio_wdata_q;
                6'h0f: dbg_rdata_q <= {8'd0, alive_q};
                6'h10: dbg_rdata_q <= dbg_i_rdata0_q;
                6'h11: dbg_rdata_q <= dbg_i_rdata1_q;
                6'h12: dbg_rdata_q <= dbg_i_rdata2_q;
                6'h13: dbg_rdata_q <= dbg_i_rdata3_q;
                6'h14: dbg_rdata_q <= {24'd0, dbg_d_rresp_or_q, dbg_d_bresp_or_q, dbg_i_rresp_or_q};
                6'h15: dbg_rdata_q <= mmio_debug_fifo_status_w;
                6'h16: dbg_rdata_q <= mmio_debug_enqueue_count_w;
                6'h17: dbg_rdata_q <= mmio_debug_dequeue_count_w;
                6'h18: dbg_rdata_q <= mmio_debug_last_bytes_w;
                6'h19: dbg_rdata_q <= mmio_debug_jtag_state_w;
                6'h1a: dbg_rdata_q <= dbg_mmio_wdata0_q;
                6'h1b: dbg_rdata_q <= dbg_mmio_wdata1_q;
                6'h1c: dbg_rdata_q <= mmio_debug_axi_counts_w;
                6'h1d: dbg_rdata_q <= mmio_debug_uart_counts_w;
                6'h1e: dbg_rdata_q <= mmio_debug_last_uart_decode_addr_w;
                6'h1f: dbg_rdata_q <= mmio_debug_last_wstrb_wdata_w;
                6'h20: dbg_rdata_q <= dbg_d_ar_count_q;
                6'h21: dbg_rdata_q <= dbg_d_r_count_q;
                6'h22: dbg_rdata_q <= dbg_d_araddr_q;
                6'h23: dbg_rdata_q <= dbg_d_ps_araddr_q;
                6'h24: dbg_rdata_q <= dbg_d_rdata_q;
                6'h25: dbg_rdata_q <= dbg_mmio_ar_count_q;
                6'h26: dbg_rdata_q <= dbg_mmio_araddr_q;
                6'h27: dbg_rdata_q <= dbg_mmio_rdata_q;
                6'h28: begin
                    dbg_rdata_q <= {23'd0, mmio_ps_console_valid_w, mmio_ps_console_data_w};
                    mmio_ps_console_pop_q <= mmio_ps_console_valid_w;
                end
                6'h29: dbg_rdata_q <= mmio_debug_clint_mtime_w;
                6'h2a: dbg_rdata_q <= mmio_debug_clint_mtimecmp_w;
                6'h2b: dbg_rdata_q <= mmio_debug_clint_status_w;
                6'h2c: dbg_rdata_q <= mouse_debug_state_w;
                6'h2d: dbg_rdata_q <= mouse_debug0_w;
                6'h2e: dbg_rdata_q <= mouse_debug1_w;
                6'h2f: dbg_rdata_q <= mouse_debug2_w;
                6'h30: dbg_rdata_q <= mouse_debug3_w;
                6'h31: dbg_rdata_q <= mouse_debug4_w;
                6'h32: dbg_rdata_q <= mouse_debug5_w;
                6'h33: dbg_rdata_q <= mouse_debug6_w;
                6'h34: dbg_rdata_q <= mouse_debug7_w;
                6'h35: dbg_rdata_q <= dbg_d_awlen_wlast_q;
                6'h36: dbg_rdata_q <= dbg_d_wlast_count_q;
                6'h37: dbg_rdata_q <= dbg_iop_aw_count_q;
                6'h38: dbg_rdata_q <= dbg_iop_w_count_q;
                6'h39: dbg_rdata_q <= dbg_iop_b_count_q;
                6'h3a: dbg_rdata_q <= dbg_iop_ar_count_q;
                6'h3b: dbg_rdata_q <= dbg_iop_r_count_q;
                6'h3c: dbg_rdata_q <= dbg_iop_awaddr_q;
                6'h3d: dbg_rdata_q <= dbg_iop_wdata_q;
                6'h3e: dbg_rdata_q <= {
                    12'd0,
                    M_AXI_IOP_wlast,
                    iop_wlast_q,
                    iop_w_sent_q,
                    iop_aw_sent_q,
                    iop_w_buf_valid_q,
                    iop_aw_buf_valid_q,
                    d_aw_iop_q,
                    d_aw_pending_q,
                    cpu_axi_d_bready_w,
                    cpu_axi_d_bvalid_w,
                    cpu_axi_d_wready_w,
                    cpu_axi_d_wvalid_w,
                    cpu_axi_d_awready_w,
                    cpu_axi_d_awvalid_w,
                    M_AXI_IOP_bready,
                    M_AXI_IOP_bvalid,
                    M_AXI_IOP_wready,
                    M_AXI_IOP_wvalid,
                    M_AXI_IOP_awready,
                    M_AXI_IOP_awvalid
                };
                6'h3f: dbg_rdata_q <= {iop_awlen_q, cpu_axi_d_awlen_w, iop_wstrb_q, M_AXI_IOP_bresp, M_AXI_IOP_bid, 6'd0};
                7'h40: dbg_rdata_q <= eth_txclk_edge_count_q;
                7'h41: dbg_rdata_q <= eth_rxclk_edge_count_q;
                7'h42: dbg_rdata_q <= eth_rxdv_edge_count_q;
                7'h43: dbg_rdata_q <= eth_txen_edge_count_q;
                7'h44: dbg_rdata_q <= eth_rx_activity_count_q;
                7'h45: dbg_rdata_q <= eth_tx_activity_count_q;
                7'h46: dbg_rdata_q <= eth_pin_sample_q;
                7'h47: dbg_rdata_q <= eth_txclk_domain_count_q;
                7'h48: dbg_rdata_q <= eth_rxclk_domain_count_q;
                7'h49: dbg_rdata_q <= mmio_debug_plic_status_w;
                7'h4a: dbg_rdata_q <= mmio_debug_plic_claim_w;
                default: dbg_rdata_q <= 32'd0;
            endcase
        end else if (S_AXI_DBG_rready) begin
            dbg_rvalid_q <= 1'b0;
        end
    end
end

assign led = {cpu_resetn, core_rst, alive_q[23:18]};
assign dbg_write_fire_w = S_AXI_DBG_awvalid && S_AXI_DBG_wvalid && !dbg_bvalid_q;
assign dbg_irq_clear_w = dbg_write_fire_w &&
                         (S_AXI_DBG_awaddr[8:2] == 7'h49) &&
                         S_AXI_DBG_wdata[2];

assign S_AXI_DBG_awready = dbg_awready_q;
assign S_AXI_DBG_wready = dbg_wready_q;
assign S_AXI_DBG_bresp = 2'b00;
assign S_AXI_DBG_bvalid = dbg_bvalid_q;
assign S_AXI_DBG_arready = dbg_arready_q;
assign S_AXI_DBG_rdata = dbg_rdata_q;
assign S_AXI_DBG_rresp = 2'b00;
assign S_AXI_DBG_rvalid = dbg_rvalid_q;

assign M_AXI_I_awaddr = remap_ps_ddr_addr(axi_i_awaddr_w);
assign M_AXI_I_araddr = remap_ps_ddr_addr(axi_i_araddr_w);
assign axi_d_awaddr_w = remap_ps_ddr_addr(cpu_axi_d_awaddr_w);
assign axi_d_araddr_w = remap_ps_ddr_addr(cpu_axi_d_araddr_w);

wire ddr_aw_hold_w = ddr_awvalid_q && d_aw_pending_q && d_aw_route_ddr_w &&
                     cpu_axi_d_wvalid_w && !M_AXI_D_awready;
assign M_AXI_D_awvalid = (cpu_axi_d_awvalid_w && ddr_aw_sel_w && cpu_axi_d_wvalid_w) || ddr_aw_hold_w;
assign M_AXI_D_awaddr = ddr_aw_hold_w ? ddr_awaddr_q : axi_d_awaddr_w;
assign M_AXI_D_awid = ddr_aw_hold_w ? ddr_awid_q : cpu_axi_d_awid_w;
assign M_AXI_D_awlen = ddr_aw_hold_w ? ddr_awlen_q : cpu_axi_d_awlen_w;
assign M_AXI_D_awburst = ddr_aw_hold_w ? ddr_awburst_q : cpu_axi_d_awburst_w;
assign M_AXI_D_wvalid = cpu_axi_d_wvalid_w && d_aw_route_ddr_w;
assign M_AXI_D_wdata = cpu_axi_d_wdata_w;
assign M_AXI_D_wstrb = cpu_axi_d_wstrb_w;
assign M_AXI_D_wlast = d_ddr_wlast_w;
assign M_AXI_D_bready = cpu_axi_d_bready_w && d_aw_route_ddr_w;
assign M_AXI_D_arvalid = cpu_axi_d_arvalid_w && ddr_ar_sel_w;
assign M_AXI_D_araddr = axi_d_araddr_w;
assign M_AXI_D_arid = cpu_axi_d_arid_w;
assign M_AXI_D_arlen = cpu_axi_d_arlen_w;
assign M_AXI_D_arburst = cpu_axi_d_arburst_w;
assign M_AXI_D_rready = cpu_axi_d_rready_w && d_ar_route_ddr_w;

assign M_AXI_IOP_awvalid = iop_write_ready_w && !iop_aw_sent_q;
assign M_AXI_IOP_awaddr = iop_awaddr_q;
assign M_AXI_IOP_awid = iop_awid_q;
assign M_AXI_IOP_awlen = 8'd0;
assign M_AXI_IOP_awburst = 2'b01;
assign M_AXI_IOP_wvalid = iop_write_ready_w && !iop_w_sent_q;
assign M_AXI_IOP_wid = iop_awid_q;
assign M_AXI_IOP_wdata = iop_wdata_q;
assign M_AXI_IOP_wstrb = iop_wstrb_q;
assign M_AXI_IOP_wlast = 1'b1;
assign M_AXI_IOP_bready = cpu_axi_d_bready_w && d_aw_route_iop_w;
assign M_AXI_IOP_arvalid = cpu_axi_d_arvalid_w && ps_iop_ar_sel_w;
assign M_AXI_IOP_araddr = cpu_axi_d_araddr_w;
assign M_AXI_IOP_arid = cpu_axi_d_arid_w;
assign M_AXI_IOP_arlen = cpu_axi_d_arlen_w;
assign M_AXI_IOP_arburst = cpu_axi_d_arburst_w;
assign M_AXI_IOP_rready = cpu_axi_d_rready_w && d_ar_route_iop_w;

assign mmio_awready_w = video_aw_sel_w ? video_awready_w :
                        mouse_aw_sel_w ? mouse_awready_w : console_awready_w;
assign mmio_wready_w = d_aw_route_video_w ? video_wready_w :
                       d_aw_route_mouse_w ? mouse_wready_w : console_wready_w;
assign mmio_bvalid_w = video_bvalid_w || mouse_bvalid_w || console_bvalid_w;
assign mmio_bresp_w = video_bvalid_w ? video_bresp_w :
                      mouse_bvalid_w ? mouse_bresp_w : console_bresp_w;
assign mmio_bid_w = video_bvalid_w ? video_bid_w :
                    mouse_bvalid_w ? mouse_bid_w : console_bid_w;
assign mmio_arready_w = video_ar_sel_w ? video_arready_w :
                        mouse_ar_sel_w ? mouse_arready_w : console_arready_w;
assign mmio_rvalid_w = d_ar_route_video_w ? video_rvalid_w :
                       d_ar_route_mouse_w ? mouse_rvalid_w : console_rvalid_w;
assign mmio_rdata_w = d_ar_route_video_w ? video_rdata_w :
                      d_ar_route_mouse_w ? mouse_rdata_w : console_rdata_w;
assign mmio_rresp_w = d_ar_route_video_w ? video_rresp_w :
                      d_ar_route_mouse_w ? mouse_rresp_w : console_rresp_w;
assign mmio_rid_w = d_ar_route_video_w ? video_rid_w :
                    d_ar_route_mouse_w ? mouse_rid_w : console_rid_w;
assign mmio_rlast_w = d_ar_route_video_w ? video_rlast_w :
                      d_ar_route_mouse_w ? mouse_rlast_w : console_rlast_w;

assign cpu_axi_d_awready_w = mmio_aw_sel_w ? mmio_awready_w :
                             ps_iop_aw_sel_w ? (!iop_aw_buf_valid_q && !iop_w_buf_valid_q) : M_AXI_D_awready;
assign cpu_axi_d_wready_w = d_aw_route_mmio_w ? mmio_wready_w :
                            d_aw_route_iop_w ? (!iop_w_buf_valid_q && (iop_aw_buf_valid_q || iop_aw_capture_w)) : M_AXI_D_wready;
assign cpu_axi_d_bvalid_w = d_aw_route_mmio_w ? mmio_bvalid_w :
                            d_aw_route_iop_w ? M_AXI_IOP_bvalid : M_AXI_D_bvalid;
assign cpu_axi_d_bresp_w = d_aw_route_mmio_w ? mmio_bresp_w :
                           d_aw_route_iop_w ? M_AXI_IOP_bresp : M_AXI_D_bresp;
assign cpu_axi_d_bid_w = d_aw_route_mmio_w ? mmio_bid_w :
                         d_aw_route_iop_w ? M_AXI_IOP_bid : M_AXI_D_bid;
assign cpu_axi_d_arready_w = mmio_ar_sel_w ? mmio_arready_w :
                             ps_iop_ar_sel_w ? M_AXI_IOP_arready : M_AXI_D_arready;
assign cpu_axi_d_rvalid_w = d_ar_route_mmio_w ? mmio_rvalid_w :
                            d_ar_route_iop_w ? M_AXI_IOP_rvalid : M_AXI_D_rvalid;
assign cpu_axi_d_rdata_w = d_ar_route_mmio_w ? mmio_rdata_w :
                           d_ar_route_iop_w ? M_AXI_IOP_rdata : M_AXI_D_rdata;
assign cpu_axi_d_rresp_w = d_ar_route_mmio_w ? mmio_rresp_w :
                           d_ar_route_iop_w ? M_AXI_IOP_rresp : M_AXI_D_rresp;
assign cpu_axi_d_rid_w = d_ar_route_mmio_w ? mmio_rid_w :
                         d_ar_route_iop_w ? M_AXI_IOP_rid : M_AXI_D_rid;
assign cpu_axi_d_rlast_w = d_ar_route_mmio_w ? mmio_rlast_w :
                           d_ar_route_iop_w ? M_AXI_IOP_rlast : M_AXI_D_rlast;
assign mmio_rready_route_w = cpu_axi_d_rready_w && d_ar_route_mmio_w;

assign M_AXI_I_awsize = 3'b010;
assign M_AXI_I_awlock = 1'b0;
assign M_AXI_I_awcache = 4'b0011;
assign M_AXI_I_awprot = 3'b000;
assign M_AXI_I_arsize = 3'b010;
assign M_AXI_I_arlock = 1'b0;
assign M_AXI_I_arcache = 4'b0011;
assign M_AXI_I_arprot = 3'b000;

assign M_AXI_D_awsize = 3'b010;
assign M_AXI_D_awlock = 1'b0;
assign M_AXI_D_awcache = 4'b0011;
assign M_AXI_D_awprot = 3'b000;
assign M_AXI_D_arsize = 3'b010;
assign M_AXI_D_arlock = 1'b0;
assign M_AXI_D_arcache = 4'b0011;
assign M_AXI_D_arprot = 3'b000;

assign M_AXI_IOP_awsize = 3'b010;
assign M_AXI_IOP_awlock = 1'b0;
assign M_AXI_IOP_awcache = 4'b0000;
assign M_AXI_IOP_awprot = 3'b000;
assign M_AXI_IOP_arsize = 3'b010;
assign M_AXI_IOP_arlock = 1'b0;
assign M_AXI_IOP_arcache = 4'b0000;
assign M_AXI_IOP_arprot = 3'b000;

pl_mmio_jtag_console u_pl_mmio (
    .clk(clk),
    .resetn(resetn && cpu_resetn),
    .s_axi_awvalid(cpu_axi_d_awvalid_w && console_aw_sel_w),
    .s_axi_awaddr(cpu_axi_d_awaddr_w),
    .s_axi_awid(cpu_axi_d_awid_w),
    .s_axi_awready(console_awready_w),
    .s_axi_wvalid(cpu_axi_d_wvalid_w && d_aw_route_console_w),
    .s_axi_wdata(cpu_axi_d_wdata_w),
    .s_axi_wstrb(cpu_axi_d_wstrb_w),
    .s_axi_wlast(cpu_axi_d_wlast_w),
    .s_axi_wready(console_wready_w),
    .s_axi_bvalid(console_bvalid_w),
    .s_axi_bresp(console_bresp_w),
    .s_axi_bid(console_bid_w),
    .s_axi_bready(cpu_axi_d_bready_w && d_aw_route_console_w),
    .s_axi_arvalid(cpu_axi_d_arvalid_w && console_ar_sel_w),
    .s_axi_araddr(cpu_axi_d_araddr_w),
    .s_axi_arid(cpu_axi_d_arid_w),
    .s_axi_arready(console_arready_w),
    .s_axi_rvalid(console_rvalid_w),
    .s_axi_rdata(console_rdata_w),
    .s_axi_rresp(console_rresp_w),
    .s_axi_rid(console_rid_w),
    .s_axi_rlast(console_rlast_w),
    .s_axi_rready(mmio_rready_route_w && d_ar_route_console_w),
    .timer_intr(mmio_timer_intr_w),
    .ext_intr(mmio_ext_intr_w),
    .debug_fifo_status(mmio_debug_fifo_status_w),
    .debug_enqueue_count(mmio_debug_enqueue_count_w),
    .debug_dequeue_count(mmio_debug_dequeue_count_w),
    .debug_last_bytes(mmio_debug_last_bytes_w),
    .debug_jtag_state(mmio_debug_jtag_state_w),
    .debug_axi_counts(mmio_debug_axi_counts_w),
    .debug_uart_counts(mmio_debug_uart_counts_w),
    .debug_last_uart_decode_addr(mmio_debug_last_uart_decode_addr_w),
    .debug_last_wstrb_wdata(mmio_debug_last_wstrb_wdata_w),
    .debug_clint_mtime(mmio_debug_clint_mtime_w),
    .debug_clint_mtimecmp(mmio_debug_clint_mtimecmp_w),
    .debug_clint_status(mmio_debug_clint_status_w),
    .debug_plic_status(mmio_debug_plic_status_w),
    .debug_plic_claim(mmio_debug_plic_claim_w),
    .net_irq_event(eth_rx_irq_event_w),
    .debug_irq_clear(dbg_irq_clear_w),
    .ps_console_pop(mmio_ps_console_pop_q),
    .ps_console_valid(mmio_ps_console_valid_w),
    .ps_console_data(mmio_ps_console_data_w),
    .pl_uart_tx(pl_uart_tx)
);

pl_mouse_mmio u_pl_mouse (
    .clk(clk),
    .resetn(resetn && cpu_resetn),
    .dbg_wr_valid(dbg_write_fire_w),
    .dbg_wr_addr(S_AXI_DBG_awaddr[7:0]),
    .dbg_wr_data(S_AXI_DBG_wdata),
    .s_axi_awvalid(cpu_axi_d_awvalid_w && mouse_aw_sel_w),
    .s_axi_awaddr(cpu_axi_d_awaddr_w),
    .s_axi_awid(cpu_axi_d_awid_w),
    .s_axi_awready(mouse_awready_w),
    .s_axi_wvalid(cpu_axi_d_wvalid_w && d_aw_route_mouse_w),
    .s_axi_wdata(cpu_axi_d_wdata_w),
    .s_axi_wstrb(cpu_axi_d_wstrb_w),
    .s_axi_wready(mouse_wready_w),
    .s_axi_bvalid(mouse_bvalid_w),
    .s_axi_bresp(mouse_bresp_w),
    .s_axi_bid(mouse_bid_w),
    .s_axi_bready(cpu_axi_d_bready_w && d_aw_route_mouse_w),
    .s_axi_arvalid(cpu_axi_d_arvalid_w && mouse_ar_sel_w),
    .s_axi_araddr(cpu_axi_d_araddr_w),
    .s_axi_arid(cpu_axi_d_arid_w),
    .s_axi_arready(mouse_arready_w),
    .s_axi_rvalid(mouse_rvalid_w),
    .s_axi_rdata(mouse_rdata_w),
    .s_axi_rresp(mouse_rresp_w),
    .s_axi_rid(mouse_rid_w),
    .s_axi_rlast(mouse_rlast_w),
    .s_axi_rready(mmio_rready_route_w && d_ar_route_mouse_w),
    .debug_state(mouse_debug_state_w),
    .debug0(mouse_debug0_w),
    .debug1(mouse_debug1_w),
    .debug2(mouse_debug2_w),
    .debug3(mouse_debug3_w),
    .debug4(mouse_debug4_w),
    .debug5(mouse_debug5_w),
    .debug6(mouse_debug6_w),
    .debug7(mouse_debug7_w)
  );

vibe_hdmi_mmio u_vibe_hdmi (
    .clk(clk),
    .resetn(resetn && cpu_resetn),
    .s_axi_awvalid(cpu_axi_d_awvalid_w && video_aw_sel_w),
    .s_axi_awaddr(cpu_axi_d_awaddr_w),
    .s_axi_awid(cpu_axi_d_awid_w),
    .s_axi_awready(video_awready_w),
    .s_axi_wvalid(cpu_axi_d_wvalid_w && d_aw_route_video_w),
    .s_axi_wdata(cpu_axi_d_wdata_w),
    .s_axi_wstrb(cpu_axi_d_wstrb_w),
    .s_axi_wlast(cpu_axi_d_wlast_w),
    .s_axi_wready(video_wready_w),
    .s_axi_bvalid(video_bvalid_w),
    .s_axi_bresp(video_bresp_w),
    .s_axi_bid(video_bid_w),
    .s_axi_bready(cpu_axi_d_bready_w && d_aw_route_video_w),
    .s_axi_arvalid(cpu_axi_d_arvalid_w && video_ar_sel_w),
    .s_axi_araddr(cpu_axi_d_araddr_w),
    .s_axi_arid(cpu_axi_d_arid_w),
    .s_axi_arready(video_arready_w),
    .s_axi_rvalid(video_rvalid_w),
    .s_axi_rdata(video_rdata_w),
    .s_axi_rresp(video_rresp_w),
    .s_axi_rid(video_rid_w),
    .s_axi_rlast(video_rlast_w),
    .s_axi_rready(mmio_rready_route_w && d_ar_route_video_w),
    .hdmi_tx_clk_n(hdmi_tx_clk_n),
    .hdmi_tx_clk_p(hdmi_tx_clk_p),
    .hdmi_tx_n(hdmi_tx_n),
    .hdmi_tx_p(hdmi_tx_p)
);

riscv_top #(
    .CORE_ID(0),
    // Keep instruction cache enabled, but force data accesses through the
    // uncached path. The Zynq GP0 write channel has been hanging on D-cache
    // writeback traffic before the OS reaches USB init.
    .MEM_CACHE_ADDR_MIN(32'hffff_ffff),
    .MEM_CACHE_ADDR_MAX(32'h0000_0000)
) u_cpu (
    .clk_i(clk),
    .rst_i(core_rst),
    .intr_i(mmio_ext_intr_w),
    .timer_intr_i(mmio_timer_intr_w),
    .reset_vector_i(RESET_VECTOR),

    .axi_i_awready_i(M_AXI_I_awready),
    .axi_i_wready_i(M_AXI_I_wready),
    .axi_i_bvalid_i(M_AXI_I_bvalid),
    .axi_i_bresp_i(M_AXI_I_bresp),
    .axi_i_bid_i(M_AXI_I_bid),
    .axi_i_arready_i(M_AXI_I_arready),
    .axi_i_rvalid_i(M_AXI_I_rvalid),
    .axi_i_rdata_i(M_AXI_I_rdata),
    .axi_i_rresp_i(M_AXI_I_rresp),
    .axi_i_rid_i(M_AXI_I_rid),
    .axi_i_rlast_i(M_AXI_I_rlast),
    .axi_i_awvalid_o(M_AXI_I_awvalid),
    .axi_i_awaddr_o(axi_i_awaddr_w),
    .axi_i_awid_o(M_AXI_I_awid),
    .axi_i_awlen_o(M_AXI_I_awlen),
    .axi_i_awburst_o(M_AXI_I_awburst),
    .axi_i_wvalid_o(M_AXI_I_wvalid),
    .axi_i_wdata_o(M_AXI_I_wdata),
    .axi_i_wstrb_o(M_AXI_I_wstrb),
    .axi_i_wlast_o(M_AXI_I_wlast),
    .axi_i_bready_o(M_AXI_I_bready),
    .axi_i_arvalid_o(M_AXI_I_arvalid),
    .axi_i_araddr_o(axi_i_araddr_w),
    .axi_i_arid_o(M_AXI_I_arid),
    .axi_i_arlen_o(M_AXI_I_arlen),
    .axi_i_arburst_o(M_AXI_I_arburst),
    .axi_i_rready_o(M_AXI_I_rready),

    .axi_d_awready_i(cpu_axi_d_awready_w),
    .axi_d_wready_i(cpu_axi_d_wready_w),
    .axi_d_bvalid_i(cpu_axi_d_bvalid_w),
    .axi_d_bresp_i(cpu_axi_d_bresp_w),
    .axi_d_bid_i(cpu_axi_d_bid_w),
    .axi_d_arready_i(cpu_axi_d_arready_w),
    .axi_d_rvalid_i(cpu_axi_d_rvalid_w),
    .axi_d_rdata_i(cpu_axi_d_rdata_w),
    .axi_d_rresp_i(cpu_axi_d_rresp_w),
    .axi_d_rid_i(cpu_axi_d_rid_w),
    .axi_d_rlast_i(cpu_axi_d_rlast_w),
    .axi_d_awvalid_o(cpu_axi_d_awvalid_w),
    .axi_d_awaddr_o(cpu_axi_d_awaddr_w),
    .axi_d_awid_o(cpu_axi_d_awid_w),
    .axi_d_awlen_o(cpu_axi_d_awlen_w),
    .axi_d_awburst_o(cpu_axi_d_awburst_w),
    .axi_d_wvalid_o(cpu_axi_d_wvalid_w),
    .axi_d_wdata_o(cpu_axi_d_wdata_w),
    .axi_d_wstrb_o(cpu_axi_d_wstrb_w),
    .axi_d_wlast_o(cpu_axi_d_wlast_w),
    .axi_d_bready_o(cpu_axi_d_bready_w),
    .axi_d_arvalid_o(cpu_axi_d_arvalid_w),
    .axi_d_araddr_o(cpu_axi_d_araddr_w),
    .axi_d_arid_o(cpu_axi_d_arid_w),
    .axi_d_arlen_o(cpu_axi_d_arlen_w),
    .axi_d_arburst_o(cpu_axi_d_arburst_w),
    .axi_d_rready_o(cpu_axi_d_rready_w)
);

endmodule
