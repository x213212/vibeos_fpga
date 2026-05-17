set script_dir [file dirname [file normalize [info script]]]
set bit_file "$script_dir/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "$script_dir/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set dbg_base 0x41210000

proc read_reg {name offset} {
    global dbg_base
    set value [mrd -value [expr {$dbg_base + $offset}]]
    puts [format "%s=0x%08x" $name $value]
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "xc7z020*"}}
fpga -file $bit_file
after 500
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}
catch {stop}
source $init_file
ps7_init
if {[llength [info procs ps7_post_config]]} {
    ps7_post_config
}

read_reg DBG_MAGIC 0x00
read_reg DBG_FLAGS 0x04
read_reg DBG_I_AR_COUNT 0x08
read_reg DBG_I_R_COUNT 0x0c
read_reg DBG_D_AW_COUNT 0x10
read_reg DBG_D_W_COUNT 0x14
read_reg DBG_D_B_COUNT 0x18
read_reg DBG_MMIO_AW_COUNT 0x1c
read_reg DBG_LAST_I_ARADDR 0x20
read_reg DBG_LAST_I_PS_ARADDR 0x24
read_reg DBG_LAST_I_RDATA 0x28
read_reg DBG_LAST_D_AWADDR 0x2c
read_reg DBG_LAST_D_WDATA 0x30
read_reg DBG_LAST_MMIO_AWADDR 0x34
read_reg DBG_LAST_MMIO_WDATA 0x38
read_reg DBG_ALIVE 0x3c
read_reg DBG_I_RDATA0 0x40
read_reg DBG_I_RDATA1 0x44
read_reg DBG_I_RDATA2 0x48
read_reg DBG_I_RDATA3 0x4c
read_reg DBG_RESP_OR 0x50
