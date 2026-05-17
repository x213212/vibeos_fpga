set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/project_2/rv32_tiny_putchar.bin"
set cpu_reset_gpio 0x41200000
set ddr_load_addr 0x00100000
set dbg_base 0x41210000

proc read_dbg {name offset} {
    global dbg_base
    set value [mrd -value [expr {$dbg_base + $offset}]]
    puts [format "%s=0x%08x" $name [expr {$value & 0xffffffff}]]
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
jtag targets -set -filter {level == 0}
jtag targets -open -filter {level == 0}
after 1000
catch {jtag frequency 1000000}

targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}
source $init_file
ps7_init

targets -set -filter {name =~ "xc7z020*"}
fpga -file $bit_file

targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
if {[llength [info procs ps7_post_config]]} {
    ps7_post_config
}
after 1000
catch {stop}

mwr $cpu_reset_gpio 0x00000000
dow -data $bin_file $ddr_load_addr
puts [format "DOW_BINARY_FILE=%s" $bin_file]
puts [format "DDR_LOAD_WORD0=0x%08x" [expr {[mrd -value $ddr_load_addr] & 0xffffffff}]]
mwr $cpu_reset_gpio 0x00000001
after 1000

read_dbg DBG_MAGIC 0x00
read_dbg DBG_FLAGS 0x04
read_dbg DBG_I_AR_COUNT 0x08
read_dbg DBG_I_R_COUNT 0x0c
read_dbg DBG_LAST_I_ARADDR 0x20
read_dbg DBG_LAST_I_PS_ARADDR 0x24
read_dbg DBG_I_RDATA0 0x40
read_dbg DBG_I_RDATA1 0x44
read_dbg DBG_MMIO_AW_COUNT 0x1c
read_dbg DBG_FIFO_ENQUEUE_COUNT 0x58
read_dbg DBG_FIFO_DEQUEUE_COUNT 0x5c
read_dbg DBG_FIFO_LAST_BYTES 0x60

disconnect
