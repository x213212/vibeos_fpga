set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/project_2/rv32_store_sig.bin"
if {[llength $argv] >= 1} {
    set bin_file [lindex $argv 0]
}
set cpu_reset_gpio 0x41200000
set dbg_base 0x41210000
set ddr_load_addr 0x00000000
set sig_addr 0x00001000

proc xsdb_connect {} {
    connect -url tcp:127.0.0.1:3121
    configparams force-mem-access 1
    puts "JTAG_TARGETS_BEFORE_OPEN_BEGIN"
    puts [jtag targets]
    puts "JTAG_TARGETS_BEFORE_OPEN_END"
    jtag targets -set -filter {level == 0}
    jtag targets -open -filter {level == 0}
    after 1000
    puts "JTAG_TARGETS_AFTER_OPEN_BEGIN"
    puts [jtag targets]
    puts "JTAG_TARGETS_AFTER_OPEN_END"
    catch {jtag frequency 1000000}
}

proc select_arm_target {} {
    targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
}

proc select_dap_target {} {
    if {[catch {targets -set -filter {name =~ "DAP*"}}]} {
        targets -set -filter {name =~ "APU"}
    }
}

proc read_dbg {name offset} {
    global dbg_base
    set value [mrd -value [expr {$dbg_base + $offset}]]
    puts [format "%s=0x%08x" $name [expr {$value & 0xffffffff}]]
}

proc mwr_binary {path base_addr} {
    set fd [open $path rb]
    fconfigure $fd -translation binary
    set data [read $fd]
    close $fd
    binary scan $data c* bytes
    set count [llength $bytes]
    for {set i 0} {$i < $count} {incr i 4} {
        set b0 [expr {([lindex $bytes $i] & 0xff)}]
        set b1 [expr {(($i + 1) < $count) ? ([lindex $bytes [expr {$i + 1}]] & 0xff) : 0}]
        set b2 [expr {(($i + 2) < $count) ? ([lindex $bytes [expr {$i + 2}]] & 0xff) : 0}]
        set b3 [expr {(($i + 3) < $count) ? ([lindex $bytes [expr {$i + 3}]] & 0xff) : 0}]
        set word [expr {$b0 | ($b1 << 8) | ($b2 << 16) | ($b3 << 24)}]
        mwr [expr {$base_addr + $i}] $word
    }
    puts [format "MWR_BINARY_BYTES=%d" $count]
}

xsdb_connect
targets -set -filter {name =~ "xc7z020*"}
fpga -file $bit_file

select_arm_target
catch {stop}
source $init_file
ps7_init
if {[llength [info procs ps7_post_config]]} {
    ps7_post_config
}
after 1000
catch {rst -processor}
after 1000
select_arm_target
catch {stop}

mwr $cpu_reset_gpio 0x00000000
mwr $sig_addr 0x00000000
dow -data $bin_file $ddr_load_addr
puts [format "DOW_BINARY_FILE=%s" $bin_file]
set load0 [mrd -value $ddr_load_addr]
set load4 [mrd -value [expr {$ddr_load_addr + 4}]]
set load8 [mrd -value [expr {$ddr_load_addr + 8}]]
puts [format "DDR_LOAD_WORD0=0x%08x" [expr {$load0 & 0xffffffff}]]
puts [format "DDR_LOAD_WORD4=0x%08x" [expr {$load4 & 0xffffffff}]]
puts [format "DDR_LOAD_WORD8=0x%08x" [expr {$load8 & 0xffffffff}]]
mwr $cpu_reset_gpio 0x00000001
after 1000
set sig [mrd -value $sig_addr]
puts [format "CPU_SIG_WORD=0x%08x" [expr {$sig & 0xffffffff}]]
read_dbg DBG_MAGIC 0x00
read_dbg DBG_FLAGS 0x04
read_dbg DBG_I_AR_COUNT 0x08
read_dbg DBG_I_R_COUNT 0x0c
read_dbg DBG_D_AW_COUNT 0x10
read_dbg DBG_D_W_COUNT 0x14
read_dbg DBG_D_B_COUNT 0x18
read_dbg DBG_MMIO_AW_COUNT 0x1c
read_dbg DBG_LAST_I_ARADDR 0x20
read_dbg DBG_LAST_I_PS_ARADDR 0x24
read_dbg DBG_LAST_I_RDATA 0x28
read_dbg DBG_LAST_D_AWADDR 0x2c
read_dbg DBG_LAST_D_WDATA 0x30
read_dbg DBG_LAST_MMIO_AWADDR 0x34
read_dbg DBG_LAST_MMIO_WDATA 0x38
read_dbg DBG_ALIVE 0x3c
read_dbg DBG_I_RDATA0 0x40
read_dbg DBG_I_RDATA1 0x44
read_dbg DBG_I_RDATA2 0x48
read_dbg DBG_I_RDATA3 0x4c
read_dbg DBG_RESP_OR 0x50
read_dbg DBG_FIFO_STATUS 0x54
read_dbg DBG_FIFO_ENQUEUE_COUNT 0x58
read_dbg DBG_FIFO_DEQUEUE_COUNT 0x5c
read_dbg DBG_FIFO_LAST_BYTES 0x60
read_dbg DBG_JTAG_STATE 0x64
read_dbg DBG_FIRST_MMIO_WDATA 0x68
read_dbg DBG_SECOND_MMIO_WDATA 0x6c
read_dbg DBG_MMIO_AXI_COUNTS 0x70
read_dbg DBG_UART_COUNTS 0x74
read_dbg DBG_LAST_UART_DECODE_ADDR 0x78
read_dbg DBG_LAST_WSTRB_WDATA 0x7c
disconnect
