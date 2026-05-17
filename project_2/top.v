`timescale 1ns/1ps

module top #(
    // Simulation RAM backing store. 32M words = 128MB, enough for the
    // current vibeos firmware.hex. This is not intended to infer Zynq-7020
    // block RAM for synthesis; real hardware should connect this bus to PS DDR.
    parameter integer MEM_WORDS = 33554432,
    parameter [31:0] RAM_BASE = 32'h80000000,
    parameter [31:0] RESET_VECTOR = 32'h80000000,
    parameter MEM_INIT_FILE = "firmware.hex",
    parameter integer CLK_FREQ_HZ = 100000000,
    parameter integer UART_BAUD = 115200,
    parameter DEBUG_UART_TRACE = 1,
    parameter DEBUG_JTAG_CONSOLE = 1
)(
    input clk,
    input resetn,
    output uart_tx,
    output [7:0] led
);

wire rst = !resetn;

wire [31:0] mem_i_pc;
wire mem_i_rd;
wire mem_i_accept;
wire mem_i_valid;
wire mem_i_error;
wire [31:0] mem_i_inst;

wire [31:0] mem_d_addr;
wire [31:0] mem_d_data_wr;
wire mem_d_rd;
wire [3:0] mem_d_wr;
wire mem_d_cacheable;
wire [10:0] mem_d_req_tag;
wire mem_d_invalidate;
wire mem_d_writeback;
wire mem_d_flush;
wire [31:0] mem_d_data_rd;
wire mem_d_accept;
wire mem_d_ack;
wire mem_d_error;
wire [10:0] mem_d_resp_tag;
wire timer_intr;

riscv_core #(
    .SUPPORT_MULDIV(1),
    .SUPPORT_SUPER(1),
    .SUPPORT_MMU(1),
    .SUPPORT_LOAD_BYPASS(1),
    .SUPPORT_MUL_BYPASS(1),
    .SUPPORT_REGFILE_XILINX(0),
    .EXTRA_DECODE_STAGE(1),
    .MEM_CACHE_ADDR_MIN(RAM_BASE),
    .MEM_CACHE_ADDR_MAX(RAM_BASE + (MEM_WORDS * 4) - 1)
) cpu (
    .clk_i(clk),
    .rst_i(rst),
    .mem_d_data_rd_i(mem_d_data_rd),
    .mem_d_accept_i(mem_d_accept),
    .mem_d_ack_i(mem_d_ack),
    .mem_d_error_i(mem_d_error),
    .mem_d_resp_tag_i(mem_d_resp_tag),
    .mem_i_accept_i(mem_i_accept),
    .mem_i_valid_i(mem_i_valid),
    .mem_i_error_i(mem_i_error),
    .mem_i_inst_i(mem_i_inst),
    .intr_i(1'b0),
    .timer_intr_i(timer_intr),
    .reset_vector_i(RESET_VECTOR),
    .cpu_id_i(32'h00000000),
    .mem_d_addr_o(mem_d_addr),
    .mem_d_data_wr_o(mem_d_data_wr),
    .mem_d_rd_o(mem_d_rd),
    .mem_d_wr_o(mem_d_wr),
    .mem_d_cacheable_o(mem_d_cacheable),
    .mem_d_req_tag_o(mem_d_req_tag),
    .mem_d_invalidate_o(mem_d_invalidate),
    .mem_d_writeback_o(mem_d_writeback),
    .mem_d_flush_o(mem_d_flush),
    .mem_i_rd_o(mem_i_rd),
    .mem_i_flush_o(),
    .mem_i_invalidate_o(),
    .mem_i_pc_o(mem_i_pc)
);

native_ram_mmio #(
    .MEM_WORDS(MEM_WORDS),
    .RAM_BASE(RAM_BASE),
    .MEM_INIT_FILE(MEM_INIT_FILE),
    .CLK_FREQ_HZ(CLK_FREQ_HZ),
    .UART_BAUD(UART_BAUD),
    .DEBUG_UART_TRACE(DEBUG_UART_TRACE),
    .DEBUG_JTAG_CONSOLE(DEBUG_JTAG_CONSOLE)
) memory (
    .clk(clk),
    .rst(rst),
    .uart_tx(uart_tx),
    .led(led),
    .mem_i_pc(mem_i_pc),
    .mem_i_rd(mem_i_rd),
    .mem_i_accept(mem_i_accept),
    .mem_i_valid(mem_i_valid),
    .mem_i_error(mem_i_error),
    .mem_i_inst(mem_i_inst),
    .mem_d_addr(mem_d_addr),
    .mem_d_data_wr(mem_d_data_wr),
    .mem_d_rd(mem_d_rd),
    .mem_d_wr(mem_d_wr),
    .mem_d_req_tag(mem_d_req_tag),
    .mem_d_data_rd(mem_d_data_rd),
    .mem_d_accept(mem_d_accept),
    .mem_d_ack(mem_d_ack),
    .mem_d_error(mem_d_error),
    .mem_d_resp_tag(mem_d_resp_tag),
    .timer_intr(timer_intr)
);

endmodule

module native_ram_mmio #(
    parameter integer MEM_WORDS = 33554432,
    parameter [31:0] RAM_BASE = 32'h80000000,
    parameter MEM_INIT_FILE = "firmware.hex",
    parameter integer CLK_FREQ_HZ = 100000000,
    parameter integer UART_BAUD = 115200,
    parameter DEBUG_UART_TRACE = 1,
    parameter DEBUG_JTAG_CONSOLE = 1
)(
    input clk,
    input rst,
    output uart_tx,
    output reg [7:0] led,
    input [31:0] mem_i_pc,
    input mem_i_rd,
    output mem_i_accept,
    output reg mem_i_valid,
    output mem_i_error,
    output reg [31:0] mem_i_inst,
    input [31:0] mem_d_addr,
    input [31:0] mem_d_data_wr,
    input mem_d_rd,
    input [3:0] mem_d_wr,
    input [10:0] mem_d_req_tag,
    output reg [31:0] mem_d_data_rd,
    output mem_d_accept,
    output reg mem_d_ack,
    output mem_d_error,
    output reg [10:0] mem_d_resp_tag,
    output timer_intr
);

localparam [31:0] UART_THR = 32'h10000000;
localparam [31:0] UART_LSR = 32'h10000005;
localparam [31:0] CLINT_BASE = 32'h02000000;
localparam [31:0] CLINT_MTIMECMP = CLINT_BASE + 32'h4000;
localparam [31:0] CLINT_MTIME = CLINT_BASE + 32'hbff8;
localparam [31:0] PLIC_BASE = 32'h0c000000;

reg [31:0] ram [0:MEM_WORDS-1];
reg [63:0] mtime;
reg [63:0] mtimecmp;
reg [7:0] uart_tx_data;
reg uart_tx_start;
wire uart_tx_ready;
reg [7:0] dbg_char_data;
reg dbg_char_valid;
reg trace_boot_done;
reg trace_fetch_done;
reg trace_data_read_done;
reg trace_data_write_done;
integer i;

assign mem_i_accept = 1'b1;
assign mem_d_accept = 1'b1;
assign mem_i_error = 1'b0;
assign mem_d_error = 1'b0;
assign timer_intr = (mtime >= mtimecmp);

jtag_console_bridge_mod u_jtag_console (
    .clk(clk),
    .rst(rst),
    .data_i(dbg_char_data),
    .valid_i(dbg_char_valid)
);

simple_uart_tx #(
    .CLK_FREQ_HZ(CLK_FREQ_HZ),
    .UART_BAUD(UART_BAUD)
) u_uart_tx (
    .clk(clk),
    .rst(rst),
    .tx_data(uart_tx_data),
    .tx_start(uart_tx_start),
    .tx_ready(uart_tx_ready),
    .tx(uart_tx)
);

initial begin
    led = 8'h00;
    mtime = 64'd0;
    mtimecmp = 64'hffffffffffffffff;
    for (i = 0; i < MEM_WORDS; i = i + 1)
        ram[i] = 32'h00000013;
    if (MEM_INIT_FILE != "")
        $readmemh(MEM_INIT_FILE, ram);
end

function ram_hit;
    input [31:0] addr;
    reg [31:0] off;
    begin
        off = addr - RAM_BASE;
        ram_hit = (addr >= RAM_BASE) && (off[31:2] < MEM_WORDS);
    end
endfunction

function [31:0] read_word;
    input [31:0] addr;
    reg [31:0] off;
    begin
        off = addr - RAM_BASE;
        read_word = 32'h00000000;
        if (ram_hit(addr))
            read_word = ram[off[31:2]];
        else if (addr == UART_LSR)
            read_word = uart_tx_ready ? 32'h00006000 : 32'h00000000;
        else if (addr == CLINT_MTIME)
            read_word = mtime[31:0];
        else if (addr == CLINT_MTIME + 4)
            read_word = mtime[63:32];
        else if (addr == CLINT_MTIMECMP)
            read_word = mtimecmp[31:0];
        else if (addr == CLINT_MTIMECMP + 4)
            read_word = mtimecmp[63:32];
        else if (addr >= PLIC_BASE && addr < PLIC_BASE + 32'h00400000)
            read_word = 32'h00000000;
    end
endfunction

task write_word;
    input [31:0] addr;
    input [31:0] data;
    input [3:0] strb;
    reg [31:0] off;
    begin
        off = addr - RAM_BASE;
        if (ram_hit(addr)) begin
            if (strb[0]) ram[off[31:2]][7:0] <= data[7:0];
            if (strb[1]) ram[off[31:2]][15:8] <= data[15:8];
            if (strb[2]) ram[off[31:2]][23:16] <= data[23:16];
            if (strb[3]) ram[off[31:2]][31:24] <= data[31:24];
        end else if (addr == UART_THR) begin
            if (strb[0] && uart_tx_ready) begin
                led <= data[7:0];
                uart_tx_data <= data[7:0];
                uart_tx_start <= 1'b1;
                emit_debug_char(data[7:0]);
`ifndef SYNTHESIS
                $write("%c", data[7:0]);
`endif
            end
        end else if (addr == CLINT_MTIMECMP) begin
            if (strb[0]) mtimecmp[7:0] <= data[7:0];
            if (strb[1]) mtimecmp[15:8] <= data[15:8];
            if (strb[2]) mtimecmp[23:16] <= data[23:16];
            if (strb[3]) mtimecmp[31:24] <= data[31:24];
        end else if (addr == CLINT_MTIMECMP + 4) begin
            if (strb[0]) mtimecmp[39:32] <= data[7:0];
            if (strb[1]) mtimecmp[47:40] <= data[15:8];
            if (strb[2]) mtimecmp[55:48] <= data[23:16];
            if (strb[3]) mtimecmp[63:56] <= data[31:24];
        end
    end
endtask

task uart_send_byte;
    input [7:0] data;
    begin
        uart_tx_data <= data;
        uart_tx_start <= 1'b1;
    end
endtask

task emit_debug_char;
    input [7:0] data;
    begin
        dbg_char_data <= data;
        dbg_char_valid <= DEBUG_JTAG_CONSOLE;
    end
endtask

always @(posedge clk) begin
    if (rst) begin
        mem_i_valid <= 1'b0;
        mem_i_inst <= 32'h00000013;
        mem_d_ack <= 1'b0;
        mem_d_data_rd <= 32'h00000000;
        mem_d_resp_tag <= 11'h000;
        led <= 8'h00;
        mtime <= 64'd0;
        uart_tx_data <= 8'h00;
        uart_tx_start <= 1'b0;
        dbg_char_data <= 8'h00;
        dbg_char_valid <= 1'b0;
        trace_boot_done <= 1'b0;
        trace_fetch_done <= 1'b0;
        trace_data_read_done <= 1'b0;
        trace_data_write_done <= 1'b0;
    end else begin
        mtime <= mtime + 64'd1;
        uart_tx_start <= 1'b0;
        dbg_char_valid <= 1'b0;

        if (DEBUG_UART_TRACE && uart_tx_ready && !uart_tx_start) begin
            if (!trace_boot_done) begin
                uart_send_byte("B");
                emit_debug_char("B");
                trace_boot_done <= 1'b1;
            end else if (!trace_fetch_done && mem_i_rd) begin
                uart_send_byte("F");
                emit_debug_char("F");
                trace_fetch_done <= 1'b1;
            end else if (!trace_data_read_done && mem_d_rd) begin
                uart_send_byte("R");
                emit_debug_char("R");
                trace_data_read_done <= 1'b1;
            end else if (!trace_data_write_done && (mem_d_wr != 4'b0000)) begin
                uart_send_byte("W");
                emit_debug_char("W");
                trace_data_write_done <= 1'b1;
            end
        end

        mem_i_valid <= mem_i_rd;
        if (mem_i_rd) begin
            mem_i_inst <= read_word(mem_i_pc);
            if (led == 8'h00)
                led <= 8'h01;
        end

        mem_d_ack <= mem_d_rd | (mem_d_wr != 4'b0000);
        mem_d_resp_tag <= mem_d_req_tag;
        if (mem_d_rd) begin
            mem_d_data_rd <= read_word(mem_d_addr);
            if (led == 8'h01)
                led <= 8'h02;
        end
        if (mem_d_wr != 4'b0000)
            write_word(mem_d_addr, mem_d_data_wr, mem_d_wr);
    end
end

endmodule

module simple_uart_tx #(
    parameter integer CLK_FREQ_HZ = 100000000,
    parameter integer UART_BAUD = 115200
)(
    input clk,
    input rst,
    input [7:0] tx_data,
    input tx_start,
    output tx_ready,
    output reg tx
);

localparam integer BAUD_DIV = (CLK_FREQ_HZ + (UART_BAUD / 2)) / UART_BAUD;

reg [31:0] baud_cnt;
reg [3:0] bit_cnt;
reg [9:0] shifter;
reg busy;

assign tx_ready = !busy;

always @(posedge clk) begin
    if (rst) begin
        tx <= 1'b1;
        baud_cnt <= 32'd0;
        bit_cnt <= 4'd0;
        shifter <= 10'h3ff;
        busy <= 1'b0;
    end else if (!busy) begin
        tx <= 1'b1;
        baud_cnt <= 32'd0;
        bit_cnt <= 4'd0;
        if (tx_start) begin
            shifter <= {1'b1, tx_data, 1'b0};
            busy <= 1'b1;
        end
    end else begin
        tx <= shifter[0];
        if (baud_cnt == BAUD_DIV - 1) begin
            baud_cnt <= 32'd0;
            shifter <= {1'b1, shifter[9:1]};
            if (bit_cnt == 4'd9) begin
                bit_cnt <= 4'd0;
                busy <= 1'b0;
            end else begin
                bit_cnt <= bit_cnt + 4'd1;
            end
        end else begin
            baud_cnt <= baud_cnt + 32'd1;
        end
    end
end

endmodule

`ifndef SYNTHESIS
module BSCANE2 #(
    parameter JTAG_CHAIN = 1
)(
    output CAPTURE,
    output DRCK,
    output RESET,
    output RUNTEST,
    output SEL,
    output SHIFT,
    output TCK,
    output TDI,
    output TMS,
    output UPDATE,
    input TDO
);

assign CAPTURE = 1'b0;
assign DRCK = 1'b0;
assign RESET = 1'b0;
assign RUNTEST = 1'b0;
assign SEL = 1'b0;
assign SHIFT = 1'b0;
assign TCK = 1'b0;
assign TDI = 1'b0;
assign TMS = 1'b0;
assign UPDATE = 1'b0;

endmodule
`endif

module jtag_console_bridge_mod (
    input clk,
    input rst,
    input [7:0] data_i,
    input valid_i
);

localparam FIFO_DEPTH = 16;

reg [7:0] fifo_mem [0:FIFO_DEPTH-1];
reg [3:0] wr_ptr;
reg [3:0] rd_ptr;
reg [4:0] fifo_count;
reg [7:0] fifo_peek;
reg overflow_flag;

reg pop_toggle_drck;
reg [1:0] pop_toggle_sync;
wire pop_pulse = pop_toggle_sync[1] ^ pop_toggle_sync[0];

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

reg [9:0] shift_reg;
reg [3:0] shift_count;
reg captured_have;
reg [7:0] fifo_peek_meta;
reg [7:0] fifo_peek_sync;
reg fifo_have_meta;
reg fifo_have_sync;
reg overflow_meta;
reg overflow_sync;

`ifndef SYNTHESIS
assign capture_w = 1'b0;
assign drck_w = 1'b0;
assign reset_w = rst;
assign runtest_w = 1'b0;
assign sel_w = 1'b0;
assign shift_w = 1'b0;
assign tck_w = 1'b0;
assign tdi_w = 1'b0;
assign tms_w = 1'b0;
assign update_w = 1'b0;
`else
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
`endif

assign tdo_w = shift_reg[0];

always @(posedge clk) begin
    if (rst) begin
        wr_ptr <= 4'd0;
        rd_ptr <= 4'd0;
        fifo_count <= 5'd0;
        fifo_peek <= 8'h00;
        overflow_flag <= 1'b0;
        pop_toggle_sync <= 2'b00;
    end else begin
        pop_toggle_sync <= {pop_toggle_sync[0], pop_toggle_drck};

        if (valid_i) begin
            if (fifo_count != FIFO_DEPTH) begin
                fifo_mem[wr_ptr] <= data_i;
                wr_ptr <= wr_ptr + 4'd1;
                fifo_count <= fifo_count + 5'd1;
                if (fifo_count == 5'd0)
                    fifo_peek <= data_i;
            end else begin
                overflow_flag <= 1'b1;
            end
        end

        if (pop_pulse && fifo_count != 5'd0) begin
            rd_ptr <= rd_ptr + 4'd1;
            fifo_count <= fifo_count - 5'd1;
            if (fifo_count > 5'd1)
                fifo_peek <= fifo_mem[rd_ptr + 4'd1];
            else
                fifo_peek <= 8'h00;
        end
    end
end

always @(posedge tck_w or posedge reset_w) begin
    if (reset_w) begin
        fifo_peek_meta <= 8'h00;
        fifo_peek_sync <= 8'h00;
        fifo_have_meta <= 1'b0;
        fifo_have_sync <= 1'b0;
        overflow_meta <= 1'b0;
        overflow_sync <= 1'b0;
        shift_reg <= 10'h000;
        shift_count <= 4'd0;
        captured_have <= 1'b0;
        pop_toggle_drck <= 1'b0;
    end else begin
        fifo_peek_meta <= fifo_peek;
        fifo_peek_sync <= fifo_peek_meta;
        fifo_have_meta <= (fifo_count != 5'd0);
        fifo_have_sync <= fifo_have_meta;
        overflow_meta <= overflow_flag;
        overflow_sync <= overflow_meta;

        if (capture_w && sel_w) begin
            shift_reg <= {overflow_sync, fifo_have_sync, fifo_peek_sync};
            shift_count <= 4'd0;
            captured_have <= fifo_have_sync;
        end else if (shift_w) begin
            if (shift_count == 4'd9 && captured_have)
                pop_toggle_drck <= ~pop_toggle_drck;
            shift_count <= shift_count + 4'd1;
            shift_reg <= {tdi_w, shift_reg[9:1]};
        end
    end
end

endmodule
