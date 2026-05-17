connect -url tcp:127.0.0.1:3121
set out [open "H:/testproject/project_2/program_fpga_only.out" "w"]
puts $out "PROGRAM_FPGA_ONLY_BEGIN"
targets -set -filter {name =~ "xc7z020*"}
fpga -file H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit
after 1000
puts $out [targets]
close $out
disconnect
