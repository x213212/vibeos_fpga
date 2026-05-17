set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"

if {[llength $argv] >= 1} {
    set bit_file [lindex $argv 0]
}

connect -url tcp:127.0.0.1:3121
jtag targets -set -filter {name =~ "xc7z020*"}
fpga -file $bit_file
disconnect
