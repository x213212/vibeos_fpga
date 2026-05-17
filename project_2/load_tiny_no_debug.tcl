set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/project_2/rv32_tiny_putchar.bin"
if {[llength $argv] >= 1} {
    set bin_file [lindex $argv 0]
}

set cpu_reset_gpio 0x41200000
set ddr_load_addr 0x00100000

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
jtag targets -set -filter {level == 0}
jtag targets -open -filter {level == 0}
after 1000
jtag frequency 1000000

targets -set -filter {name =~ "xc7z020*"}
fpga -file $bit_file

targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}
source $init_file
ps7_init
if {[llength [info procs ps7_post_config]]} {
    ps7_post_config
}
after 1000
catch {rst -processor}
after 1000
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}

mwr $cpu_reset_gpio 0x00000000
dow -data $bin_file $ddr_load_addr
puts [format "DOW_BINARY_FILE=%s" $bin_file]
mwr $cpu_reset_gpio 0x00000001
after 100

disconnect
