`timescale 1ns/1ps

module pl_mmio_jtag_console (
    input clk,
    input resetn,

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

    output        timer_intr,
    output        ext_intr,

    output [31:0] debug_fifo_status,
    output [31:0] debug_enqueue_count,
    output [31:0] debug_dequeue_count,
    output [31:0] debug_last_bytes,
    output [31:0] debug_jtag_state,
    output [31:0] debug_axi_counts,
    output [31:0] debug_uart_counts,
    output [31:0] debug_last_uart_decode_addr,
    output [31:0] debug_last_wstrb_wdata,
    output [31:0] debug_clint_mtime,
    output [31:0] debug_clint_mtimecmp,
    output [31:0] debug_clint_status,
    output [31:0] debug_plic_status,
    output [31:0] debug_plic_claim,
    input         net_irq_event,
    input         debug_irq_clear,
    input         ps_console_pop,
    output        ps_console_valid,
    output [7:0]  ps_console_data,
    output        pl_uart_tx
);

localparam [31:0] UART_BASE  = 32'h10000000;
localparam [31:0] CLINT_BASE = 32'h02000000;
localparam [31:0] PLIC_BASE  = 32'h0c000000;

wire rst = !resetn;

reg        bvalid_q;
reg [3:0]  bid_q;
reg        rvalid_q;
reg [3:0]  rid_q;
reg [31:0] rdata_q;

assign s_axi_awready = !bvalid_q && s_axi_awvalid && s_axi_wvalid;
assign s_axi_wready  = !bvalid_q && s_axi_awvalid && s_axi_wvalid;
assign s_axi_bvalid  = bvalid_q;
assign s_axi_bresp   = 2'b00;
assign s_axi_bid     = bid_q;

assign s_axi_arready = !rvalid_q;
assign s_axi_rvalid  = rvalid_q;
assign s_axi_rdata   = rdata_q;
assign s_axi_rresp   = 2'b00;
assign s_axi_rid     = rid_q;
assign s_axi_rlast   = 1'b1;

reg [7:0] uart_lcr_q;
reg [7:0] uart_ier_q;
reg [7:0] uart_dll_q;
reg [7:0] uart_dlm_q;
reg       uart_overflow_q;

reg [7:0] tx_fifo_q [0:255];
reg [7:0] tx_wr_ptr_q;
reg [7:0] tx_rd_ptr_q;
reg [8:0] tx_count_q;
reg [7:0] pl_uart_fifo_q [0:255];
reg [7:0] pl_uart_wr_ptr_q;
reg [7:0] pl_uart_rd_ptr_q;
reg [8:0] pl_uart_count_q;
reg [7:0] pl_uart_tx_data_q;
reg       pl_uart_tx_start_q;
wire      pl_uart_tx_busy_w;
reg       tx_pop_meta_q;
reg       tx_pop_sync_q;
reg       tx_pop_last_q;
wire      tx_pop_toggle_w;
wire      tx_pop_event_w = (tx_pop_sync_q != tx_pop_last_q);
wire [31:0] jtag_debug_state_w;

wire uart_dlab_w = uart_lcr_q[7];
wire tx_full_w = (tx_count_q == 9'd256);
wire pl_uart_full_w = (pl_uart_count_q == 9'd256);
wire tx_valid_w = (tx_count_q != 9'd0);
wire [7:0] tx_data_w = tx_fifo_q[tx_rd_ptr_q];
assign ps_console_valid = tx_valid_w;
assign ps_console_data = tx_data_w;

reg [31:0] tx_enqueue_count_q;
reg [31:0] tx_dequeue_count_q;
reg [7:0]  tx_last_enqueued_byte_q;
reg [7:0]  tx_last_dequeued_byte_q;
reg [31:0] dbg_aw_seen_count_q;
reg [31:0] dbg_w_seen_count_q;
reg [31:0] dbg_write_fire_count_q;
reg [31:0] dbg_uart_addr_hit_count_q;
reg [31:0] dbg_uart_push_count_q;
reg [31:0] dbg_last_uart_decode_addr_q;
reg [31:0] dbg_last_wstrb_wdata_q;
reg [31:0] jtag_debug_meta_q;
reg [31:0] jtag_debug_sync_q;
reg [63:0] mtime_q;
reg [63:0] mtimecmp_q;
reg [5:0]  mtime_accum_q;

assign debug_fifo_status = {
    16'd0,
    uart_overflow_q,
    tx_full_w,
    tx_valid_w,
    4'd0,
    tx_count_q
};
assign debug_enqueue_count = tx_enqueue_count_q;
assign debug_dequeue_count = tx_dequeue_count_q;
assign debug_last_bytes = {8'd0, tx_last_dequeued_byte_q, 8'd0, tx_last_enqueued_byte_q};
assign debug_jtag_state = jtag_debug_sync_q;
assign debug_axi_counts = {
    dbg_aw_seen_count_q[7:0],
    dbg_w_seen_count_q[7:0],
    dbg_write_fire_count_q[15:0]
};
assign debug_uart_counts = {
    dbg_uart_addr_hit_count_q[15:0],
    dbg_uart_push_count_q[15:0]
};
assign debug_last_uart_decode_addr = dbg_last_uart_decode_addr_q;
assign debug_last_wstrb_wdata = dbg_last_wstrb_wdata_q;
assign debug_clint_mtime = mtime_q[31:0];
assign debug_clint_mtimecmp = mtimecmp_q[31:0];
assign debug_clint_status = {29'd0, timer_intr, (mtimecmp_q[63:32] == 32'd0), (mtimecmp_q != 64'hffffffffffffffff)};

wire       mtime_hit_w = (mtime_q >= mtimecmp_q) && (mtimecmp_q != 64'hffffffffffffffff);
assign timer_intr = mtime_hit_w;

reg [31:0] plic_enable_q;
reg [31:0] plic_priority_net_q;
reg [31:0] plic_priority_uart_q;
reg [31:0] plic_threshold_q;
reg [31:0] plic_pending_q;
reg [31:0] plic_claim_count_q;
wire       plic_net_ready_w = plic_pending_q[2] &&
                              plic_enable_q[2] &&
                              (plic_priority_net_q > plic_threshold_q);
assign ext_intr = plic_net_ready_w;
assign debug_plic_claim = plic_net_ready_w ? 32'd2 : 32'd0;
assign debug_plic_status = {
    8'h50,
    plic_claim_count_q[7:0],
    8'd0,
    ext_intr,
    plic_enable_q[2],
    plic_pending_q[2],
    net_irq_event,
    plic_priority_net_q[3:0],
    plic_threshold_q[3:0]
};

function [7:0] byte_at;
    input [31:0] data;
    input [1:0] lane;
    begin
        case (lane)
        2'd0: byte_at = data[7:0];
        2'd1: byte_at = data[15:8];
        2'd2: byte_at = data[23:16];
        default: byte_at = data[31:24];
        endcase
    end
endfunction

function [31:0] byte_read_data;
    input [1:0] lane;
    input [7:0] value;
    begin
        case (lane)
        2'd0: byte_read_data = {24'd0, value};
        2'd1: byte_read_data = {16'd0, value, 8'd0};
        2'd2: byte_read_data = {8'd0, value, 16'd0};
        default: byte_read_data = {value, 24'd0};
        endcase
    end
endfunction

function [31:0] mmio_read;
    input [31:0] addr;
    reg [7:0] uart_lsr;
    reg [23:0] plic_off;
    begin
        mmio_read = 32'd0;
        if ((addr & 32'hfffffff0) == UART_BASE) begin
            uart_lsr = {1'b0, (!tx_full_w && !pl_uart_full_w), 4'b0000, 1'b0, 1'b0};
            case (addr[3:2])
            2'd0: begin
                mmio_read = {
                    uart_lcr_q,
                    8'd0,
                    (uart_dlab_w ? uart_dlm_q : uart_ier_q),
                    (uart_dlab_w ? uart_dll_q : 8'd0)
                };
            end
            2'd1: begin
                mmio_read = {
                    8'd0,
                    8'd0,
                    uart_lsr,
                    8'd0
                };
            end
            default: begin
                mmio_read = 32'd0;
            end
            endcase
        end else if ((addr & 32'hffff0000) == CLINT_BASE) begin
            case (addr - CLINT_BASE)
            32'h00004000: mmio_read = mtimecmp_q[31:0];
            32'h00004004: mmio_read = mtimecmp_q[63:32];
            32'h0000bff8: mmio_read = mtime_q[31:0];
            32'h0000bffc: mmio_read = mtime_q[63:32];
            default: mmio_read = 32'd0;
            endcase
        end else if ((addr & 32'hff000000) == PLIC_BASE) begin
            plic_off = addr - PLIC_BASE;
            if (plic_off == 24'h000008)
                mmio_read = plic_priority_net_q;
            else if (plic_off == 24'h000028)
                mmio_read = plic_priority_uart_q;
            else if (plic_off == 24'h001000)
                mmio_read = plic_pending_q;
            else if (plic_off == 24'h002000)
                mmio_read = plic_enable_q;
            else if (plic_off == 24'h200000)
                mmio_read = plic_threshold_q;
            else if (plic_off == 24'h200004)
                mmio_read = debug_plic_claim;
            else
                mmio_read = 32'd0;
        end
    end
endfunction

integer i;
reg [31:0] wr_addr_v;
reg [7:0] wr_byte_v;
reg [23:0] plic_off_v;
reg [8:0] tx_count_v;
reg [7:0] tx_wr_ptr_v;
reg [7:0] tx_rd_ptr_v;
reg [8:0] pl_uart_count_v;
reg [7:0] pl_uart_wr_ptr_v;
reg [7:0] pl_uart_rd_ptr_v;

always @(posedge clk or posedge rst) begin
    if (rst) begin
        bvalid_q <= 1'b0;
        bid_q <= 4'd0;
        rvalid_q <= 1'b0;
        rid_q <= 4'd0;
        rdata_q <= 32'd0;
        uart_lcr_q <= 8'd0;
        uart_ier_q <= 8'd0;
        uart_dll_q <= 8'd0;
        uart_dlm_q <= 8'd0;
        uart_overflow_q <= 1'b0;
        tx_wr_ptr_q <= 8'd0;
        tx_rd_ptr_q <= 8'd0;
        tx_count_q <= 9'd0;
        pl_uart_wr_ptr_q <= 8'd0;
        pl_uart_rd_ptr_q <= 8'd0;
        pl_uart_count_q <= 9'd0;
        pl_uart_tx_data_q <= 8'd0;
        pl_uart_tx_start_q <= 1'b0;
        tx_pop_meta_q <= 1'b0;
        tx_pop_sync_q <= 1'b0;
        tx_pop_last_q <= 1'b0;
        tx_enqueue_count_q <= 32'd0;
        tx_dequeue_count_q <= 32'd0;
        tx_last_enqueued_byte_q <= 8'd0;
        tx_last_dequeued_byte_q <= 8'd0;
        dbg_aw_seen_count_q <= 32'd0;
        dbg_w_seen_count_q <= 32'd0;
        dbg_write_fire_count_q <= 32'd0;
        dbg_uart_addr_hit_count_q <= 32'd0;
        dbg_uart_push_count_q <= 32'd0;
        dbg_last_uart_decode_addr_q <= 32'd0;
        dbg_last_wstrb_wdata_q <= 32'd0;
        jtag_debug_meta_q <= 32'd0;
        jtag_debug_sync_q <= 32'd0;
        mtime_q <= 64'd0;
        mtimecmp_q <= 64'hffffffffffffffff;
        mtime_accum_q <= 6'd0;
        plic_enable_q <= 32'h00000004;
        plic_priority_net_q <= 32'd1;
        plic_priority_uart_q <= 32'd0;
        plic_threshold_q <= 32'd0;
        plic_pending_q <= 32'd0;
        plic_claim_count_q <= 32'd0;
    end else begin
        tx_pop_meta_q <= tx_pop_toggle_w;
        tx_pop_sync_q <= tx_pop_meta_q;
        tx_pop_last_q <= tx_pop_sync_q;
        jtag_debug_meta_q <= jtag_debug_state_w;
        jtag_debug_sync_q <= jtag_debug_meta_q;

        if (s_axi_awvalid)
            dbg_aw_seen_count_q <= dbg_aw_seen_count_q + 32'd1;
        if (s_axi_wvalid)
            dbg_w_seen_count_q <= dbg_w_seen_count_q + 32'd1;

        tx_count_v = tx_count_q;
        tx_wr_ptr_v = tx_wr_ptr_q;
        tx_rd_ptr_v = tx_rd_ptr_q;
        pl_uart_count_v = pl_uart_count_q;
        pl_uart_wr_ptr_v = pl_uart_wr_ptr_q;
        pl_uart_rd_ptr_v = pl_uart_rd_ptr_q;
        pl_uart_tx_start_q <= 1'b0;

        if (!pl_uart_tx_busy_w && pl_uart_count_v != 9'd0) begin
            pl_uart_tx_data_q <= pl_uart_fifo_q[pl_uart_rd_ptr_v];
            pl_uart_tx_start_q <= 1'b1;
            pl_uart_rd_ptr_v = pl_uart_rd_ptr_v + 8'd1;
            pl_uart_count_v = pl_uart_count_v - 9'd1;
        end

        if ((tx_pop_event_w || ps_console_pop) && tx_count_v != 9'd0) begin
            tx_last_dequeued_byte_q <= tx_fifo_q[tx_rd_ptr_v];
            tx_dequeue_count_q <= tx_dequeue_count_q + 32'd1;
            tx_rd_ptr_v = tx_rd_ptr_v + 8'd1;
            tx_count_v = tx_count_v - 9'd1;
        end

        if (mtime_accum_q >= 6'd15) begin
            mtime_accum_q <= mtime_accum_q - 6'd15;
            mtime_q <= mtime_q + 64'd1;
        end else begin
            mtime_accum_q <= mtime_accum_q + 6'd10;
        end

        if (debug_irq_clear)
            plic_pending_q[2] <= 1'b0;
        if (net_irq_event)
            plic_pending_q[2] <= 1'b1;

        if (bvalid_q && s_axi_bready)
            bvalid_q <= 1'b0;
        if (rvalid_q && s_axi_rready)
            rvalid_q <= 1'b0;

        if (s_axi_awready && s_axi_wready) begin
            dbg_write_fire_count_q <= dbg_write_fire_count_q + 32'd1;
            bid_q <= s_axi_awid;
            bvalid_q <= 1'b1;

            for (i = 0; i < 4; i = i + 1) begin
                if (s_axi_wstrb[i]) begin
                    wr_addr_v = s_axi_awaddr + i[31:0];
                    wr_byte_v = byte_at(s_axi_wdata, i[1:0]);
                    dbg_last_uart_decode_addr_q <= wr_addr_v;
                    dbg_last_wstrb_wdata_q <= {20'd0, s_axi_wstrb, wr_byte_v};

                    if ((wr_addr_v & 32'hfffffff0) == UART_BASE) begin
                        dbg_uart_addr_hit_count_q <= dbg_uart_addr_hit_count_q + 32'd1;
                        case (wr_addr_v[3:0])
                        4'h0: begin
                            if (uart_dlab_w)
                                uart_dll_q <= wr_byte_v;
                            else if (tx_count_v != 9'd256 && pl_uart_count_v != 9'd256) begin
                                tx_fifo_q[tx_wr_ptr_v] <= wr_byte_v;
                                pl_uart_fifo_q[pl_uart_wr_ptr_v] <= wr_byte_v;
                                tx_last_enqueued_byte_q <= wr_byte_v;
                                tx_enqueue_count_q <= tx_enqueue_count_q + 32'd1;
                                dbg_uart_push_count_q <= dbg_uart_push_count_q + 32'd1;
                                tx_wr_ptr_v = tx_wr_ptr_v + 8'd1;
                                pl_uart_wr_ptr_v = pl_uart_wr_ptr_v + 8'd1;
                                tx_count_v = tx_count_v + 9'd1;
                                pl_uart_count_v = pl_uart_count_v + 9'd1;
                            end else begin
                                uart_overflow_q <= 1'b1;
                            end
                        end
                        4'h1: begin
                            if (uart_dlab_w)
                                uart_dlm_q <= wr_byte_v;
                            else
                                uart_ier_q <= wr_byte_v;
                        end
                        4'h3: uart_lcr_q <= wr_byte_v;
                        default: ;
                        endcase
                    end else if ((wr_addr_v & 32'hffff0000) == CLINT_BASE) begin
                        case (wr_addr_v - CLINT_BASE)
                        32'h00004000: begin
                            mtimecmp_q[31:0] <= s_axi_wdata;
                            mtimecmp_q[63:32] <= 32'd0;
                        end
                        32'h00004004: mtimecmp_q[63:32] <= s_axi_wdata;
                        32'h0000bff8: mtime_q[31:0] <= s_axi_wdata;
                        32'h0000bffc: mtime_q[63:32] <= s_axi_wdata;
                        default: ;
                        endcase
                    end else if ((wr_addr_v & 32'hff000000) == PLIC_BASE) begin
                        plic_off_v = wr_addr_v - PLIC_BASE;
                        if (plic_off_v == 24'h000008)
                            plic_priority_net_q <= s_axi_wdata;
                        else if (plic_off_v == 24'h000028)
                            plic_priority_uart_q <= s_axi_wdata;
                        else if (plic_off_v == 24'h002000)
                            plic_enable_q <= s_axi_wdata;
                        else if (plic_off_v == 24'h200000)
                            plic_threshold_q <= s_axi_wdata;
                        else if (plic_off_v == 24'h200004 && s_axi_wdata == 32'd2) begin
                            plic_pending_q[2] <= 1'b0;
                            plic_claim_count_q <= plic_claim_count_q + 32'd1;
                        end
                    end
                end
            end
        end

        tx_wr_ptr_q <= tx_wr_ptr_v;
        tx_rd_ptr_q <= tx_rd_ptr_v;
        tx_count_q <= tx_count_v;
        pl_uart_wr_ptr_q <= pl_uart_wr_ptr_v;
        pl_uart_rd_ptr_q <= pl_uart_rd_ptr_v;
        pl_uart_count_q <= pl_uart_count_v;

        if (s_axi_arvalid && s_axi_arready) begin
            rid_q <= s_axi_arid;
            rdata_q <= mmio_read(s_axi_araddr);
            rvalid_q <= 1'b1;
        end
    end
end

jtag_console_tx u_jtag_console_tx (
    .clk(clk),
    .resetn(resetn),
    .tx_data(tx_data_w),
    .tx_valid(tx_valid_w),
    .tx_seq(tx_rd_ptr_q),
    .overflow(uart_overflow_q),
    .pop_toggle(tx_pop_toggle_w),
    .debug_state(jtag_debug_state_w)
);

simple_pl_uart_tx #(
    .CLKS_PER_BIT(1042)
) u_simple_pl_uart_tx (
    .clk(clk),
    .resetn(resetn),
    .tx_start(pl_uart_tx_start_q),
    .tx_data(pl_uart_tx_data_q),
    .tx_busy(pl_uart_tx_busy_w),
    .tx(pl_uart_tx)
);

endmodule

module simple_pl_uart_tx #(
    parameter integer CLKS_PER_BIT = 434
)(
    input clk,
    input resetn,
    input tx_start,
    input [7:0] tx_data,
    output tx_busy,
    output reg tx
);

localparam [1:0] ST_IDLE  = 2'd0;
localparam [1:0] ST_START = 2'd1;
localparam [1:0] ST_DATA  = 2'd2;
localparam [1:0] ST_STOP  = 2'd3;

reg [1:0] state_q;
reg [15:0] clk_count_q;
reg [2:0] bit_index_q;
reg [7:0] data_q;

assign tx_busy = (state_q != ST_IDLE);

always @(posedge clk or negedge resetn) begin
    if (!resetn) begin
        state_q <= ST_IDLE;
        clk_count_q <= 16'd0;
        bit_index_q <= 3'd0;
        data_q <= 8'd0;
        tx <= 1'b1;
    end else begin
        case (state_q)
        ST_IDLE: begin
            tx <= 1'b1;
            clk_count_q <= 16'd0;
            bit_index_q <= 3'd0;
            if (tx_start) begin
                data_q <= tx_data;
                state_q <= ST_START;
                tx <= 1'b0;
            end
        end
        ST_START: begin
            tx <= 1'b0;
            if (clk_count_q == CLKS_PER_BIT - 1) begin
                clk_count_q <= 16'd0;
                state_q <= ST_DATA;
                tx <= data_q[0];
            end else begin
                clk_count_q <= clk_count_q + 16'd1;
            end
        end
        ST_DATA: begin
            tx <= data_q[bit_index_q];
            if (clk_count_q == CLKS_PER_BIT - 1) begin
                clk_count_q <= 16'd0;
                if (bit_index_q == 3'd7) begin
                    bit_index_q <= 3'd0;
                    state_q <= ST_STOP;
                    tx <= 1'b1;
                end else begin
                    bit_index_q <= bit_index_q + 3'd1;
                    tx <= data_q[bit_index_q + 3'd1];
                end
            end else begin
                clk_count_q <= clk_count_q + 16'd1;
            end
        end
        default: begin
            tx <= 1'b1;
            if (clk_count_q == CLKS_PER_BIT - 1) begin
                clk_count_q <= 16'd0;
                state_q <= ST_IDLE;
            end else begin
                clk_count_q <= clk_count_q + 16'd1;
            end
        end
        endcase
    end
end

endmodule

module jtag_console_tx (
    input clk,
    input resetn,
    input [7:0] tx_data,
    input tx_valid,
    input [7:0] tx_seq,
    input overflow,
    output reg pop_toggle,
    output [31:0] debug_state
);

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
wire tdo_w;

reg [17:0] shift_reg_q;
reg [9:0] cmd_shift_q;
reg [4:0] cmd_bit_count_q;
reg [7:0] tx_data_meta_q;
reg [7:0] tx_data_sync_q;
reg [7:0] tx_seq_meta_q;
reg [7:0] tx_seq_sync_q;
reg       tx_valid_meta_q;
reg       tx_valid_sync_q;
reg       overflow_meta_q;
reg       overflow_sync_q;
reg       captured_valid_q;
reg       pop_consumed_q;

assign tdo_w = shift_reg_q[0];
assign debug_state = {
    capture_w,
    shift_w,
    update_w,
    reset_w,
    sel_w,
    pop_toggle,
    pop_consumed_q,
    captured_valid_q,
    overflow_sync_q,
    tx_valid_sync_q,
    cmd_bit_count_q,
    cmd_shift_q,
    shift_reg_q[6:0]
};

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

always @(posedge drck_w or posedge reset_w or negedge resetn) begin
    if (reset_w || !resetn) begin
        tx_data_meta_q <= 8'd0;
        tx_data_sync_q <= 8'd0;
        tx_seq_meta_q <= 8'd0;
        tx_seq_sync_q <= 8'd0;
        tx_valid_meta_q <= 1'b0;
        tx_valid_sync_q <= 1'b0;
        overflow_meta_q <= 1'b0;
        overflow_sync_q <= 1'b0;
    end else begin
        tx_data_meta_q <= tx_data;
        tx_data_sync_q <= tx_data_meta_q;
        tx_seq_meta_q <= tx_seq;
        tx_seq_sync_q <= tx_seq_meta_q;
        tx_valid_meta_q <= tx_valid;
        tx_valid_sync_q <= tx_valid_meta_q;
        overflow_meta_q <= overflow;
        overflow_sync_q <= overflow_meta_q;
    end
end

always @(posedge drck_w or posedge reset_w or negedge resetn) begin
    if (reset_w || !resetn) begin
        shift_reg_q <= 18'd0;
        cmd_shift_q <= 10'd0;
        cmd_bit_count_q <= 5'd0;
        captured_valid_q <= 1'b0;
    end else if (sel_w && capture_w) begin
        shift_reg_q <= {overflow_sync_q, tx_valid_sync_q, tx_seq_sync_q, tx_valid_sync_q ? tx_data_sync_q : 8'ha5};
        cmd_shift_q <= 10'd0;
        cmd_bit_count_q <= 5'd0;
        captured_valid_q <= tx_valid_sync_q;
    end else if (sel_w && shift_w) begin
        cmd_shift_q <= {tdi_w, cmd_shift_q[9:1]};
        if (cmd_bit_count_q != 5'd31)
            cmd_bit_count_q <= cmd_bit_count_q + 5'd1;
        shift_reg_q <= {tdi_w, shift_reg_q[17:1]};
    end
end

always @(posedge update_w or posedge reset_w or negedge resetn) begin
    if (reset_w || !resetn) begin
        pop_toggle <= 1'b0;
        pop_consumed_q <= 1'b0;
    end else if (captured_valid_q) begin
        pop_toggle <= !pop_toggle;
        pop_consumed_q <= 1'b1;
    end else begin
        pop_consumed_q <= 1'b0;
    end
end

endmodule
