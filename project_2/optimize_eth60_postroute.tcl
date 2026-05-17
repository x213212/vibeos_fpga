set root_dir H:/testproject
set in_dir $root_dir/project_2/riscv_ps_ddr_hw_eth_clkdomain_probe_60
set out_dir $root_dir/project_2/riscv_ps_ddr_hw_eth_clkdomain_probe_60_postroute_opt

file delete -force $out_dir
file mkdir $out_dir

open_checkpoint $in_dir/riscv_ps_ddr_routed.dcp

phys_opt_design -directive AggressiveExplore
route_design -directive Explore
phys_opt_design -directive AggressiveExplore

write_checkpoint -force $out_dir/riscv_ps_ddr_routed_opt.dcp
report_timing_summary -file $out_dir/riscv_ps_ddr_timing_summary.rpt
report_utilization -file $out_dir/riscv_ps_ddr_utilization_route.rpt
write_bitstream -force $out_dir/riscv_ps_ddr_wrapper.bit
close_design
exit
