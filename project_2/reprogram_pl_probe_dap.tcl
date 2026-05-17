set out [open "H:/testproject/project_2/reprogram_pl_probe_dap.out" "w"]
set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
connect -url tcp:127.0.0.1:3121
puts $out "BEFORE"
puts $out [targets]
targets -set -filter {name =~ "xc7z020*"}
fpga -file $bit_file
after 1000
puts $out "AFTER"
puts $out [targets]
close $out
disconnect
