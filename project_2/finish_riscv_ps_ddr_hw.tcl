set root_dir H:/testproject
set out_dir $root_dir/project_2/riscv_ps_ddr_hw_candidate
set build_jobs 2
set enable_eth 0
set fclk_mhz 60
set pl_uart_tx_pin N17

for {set i 0} {$i < [llength $argv]} {incr i} {
    set arg [lindex $argv $i]
    if {$arg eq "-pl_uart_tx_pin"} {
        incr i
        if {$i >= [llength $argv]} {
            error "Missing value for -pl_uart_tx_pin"
        }
        set pl_uart_tx_pin [lindex $argv $i]
    } elseif {$arg eq "-jobs"} {
        incr i
        if {$i >= [llength $argv]} {
            error "Missing value for -jobs"
        }
        set build_jobs [lindex $argv $i]
    } elseif {$arg eq "-out_dir"} {
        incr i
        if {$i >= [llength $argv]} {
            error "Missing value for -out_dir"
        }
        set out_dir [lindex $argv $i]
    } elseif {$arg eq "-fclk_mhz"} {
        incr i
        if {$i >= [llength $argv]} {
            error "Missing value for -fclk_mhz"
        }
        set fclk_mhz [lindex $argv $i]
    } elseif {$arg eq "-enable_eth"} {
        set enable_eth 1
    }
}

set project_file $out_dir/riscv_ps_ddr_hw.xpr
if {![file exists $project_file]} {
    error "Missing project file: $project_file"
}

open_project $project_file

set bd_file [get_files -quiet $out_dir/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/riscv_ps_ddr.bd]
if {[llength $bd_file] == 0} {
    error "Missing block design in project: riscv_ps_ddr.bd"
}
set_property synth_checkpoint_mode None $bd_file
reset_target all $bd_file
generate_target all $bd_file

set wrapper_files [get_files -quiet *riscv_ps_ddr_wrapper.v]
if {[llength $wrapper_files] == 0} {
    set wrapper [make_wrapper -files $bd_file -top]
    add_files -norecurse $wrapper
}
set_property top riscv_ps_ddr_wrapper [current_fileset]
if {[expr {abs(double($fclk_mhz) - 50.0) < 0.01}]} {
    set_property verilog_define {HDMI_FCLK_50} [current_fileset]
}
update_compile_order -fileset sources_1

set pl_uart_xdc $out_dir/pl_uart_tx.xdc
set old_xdc [get_files -quiet $pl_uart_xdc]
if {[llength $old_xdc] != 0} {
    remove_files $old_xdc
}

set xdc_fh [open $pl_uart_xdc w]
puts $xdc_fh "set_property PACKAGE_PIN $pl_uart_tx_pin \[get_ports pl_uart_tx\]"
puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports pl_uart_tx\]"
puts $xdc_fh "set_property PACKAGE_PIN U19 \[get_ports hdmi_tx_clk_n\]"
puts $xdc_fh "set_property PACKAGE_PIN U18 \[get_ports hdmi_tx_clk_p\]"
puts $xdc_fh "set_property PACKAGE_PIN W20 \[get_ports {hdmi_tx_n[0]}\]"
puts $xdc_fh "set_property PACKAGE_PIN U20 \[get_ports {hdmi_tx_n[1]}\]"
puts $xdc_fh "set_property PACKAGE_PIN P20 \[get_ports {hdmi_tx_n[2]}\]"
puts $xdc_fh "set_property PACKAGE_PIN V20 \[get_ports {hdmi_tx_p[0]}\]"
puts $xdc_fh "set_property PACKAGE_PIN T20 \[get_ports {hdmi_tx_p[1]}\]"
puts $xdc_fh "set_property PACKAGE_PIN N20 \[get_ports {hdmi_tx_p[2]}\]"
puts $xdc_fh "set_property IOSTANDARD TMDS_33 \[get_ports {hdmi_tx_clk_n hdmi_tx_clk_p hdmi_tx_n[0] hdmi_tx_n[1] hdmi_tx_n[2] hdmi_tx_p[0] hdmi_tx_p[1] hdmi_tx_p[2]}\]"
if {$enable_eth} {
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports {enet0_gmii_txd[3]}\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports {enet0_gmii_txd[2]}\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports {enet0_gmii_txd[1]}\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports {enet0_gmii_txd[0]}\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports {enet0_gmii_rxd[3]}\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports {enet0_gmii_rxd[2]}\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports {enet0_gmii_rxd[1]}\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports {enet0_gmii_rxd[0]}\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports ENET0_GMII_RX_DV_0\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports {ENET0_GMII_TX_EN_0[0]}\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports MDIO_ETHERNET_0_0_mdio_io\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports MDIO_ETHERNET_0_0_mdc\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports ENET0_GMII_RX_CLK_0\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports ENET0_GMII_TX_CLK_0\]"
    # Z7-LITE tutorial Ethernet pinmap. Keep this in sync with
    # build_riscv_ps_ddr_hw.tcl so both build flows generate the same bitmap.
    puts $xdc_fh "set_property PACKAGE_PIN K17 \[get_ports ENET0_GMII_RX_CLK_0\]"
    puts $xdc_fh "set_property PACKAGE_PIN L14 \[get_ports ENET0_GMII_TX_CLK_0\]"
    puts $xdc_fh "set_property PACKAGE_PIN K18 \[get_ports ENET0_GMII_RX_DV_0\]"
    puts $xdc_fh "set_property PACKAGE_PIN J14 \[get_ports {enet0_gmii_rxd[0]}\]"
    puts $xdc_fh "set_property PACKAGE_PIN K14 \[get_ports {enet0_gmii_rxd[1]}\]"
    puts $xdc_fh "set_property PACKAGE_PIN M18 \[get_ports {enet0_gmii_rxd[2]}\]"
    puts $xdc_fh "set_property PACKAGE_PIN M17 \[get_ports {enet0_gmii_rxd[3]}\]"
    puts $xdc_fh "set_property PACKAGE_PIN N16 \[get_ports {ENET0_GMII_TX_EN_0[0]}\]"
    puts $xdc_fh "set_property PACKAGE_PIN M14 \[get_ports {enet0_gmii_txd[0]}\]"
    puts $xdc_fh "set_property PACKAGE_PIN L15 \[get_ports {enet0_gmii_txd[1]}\]"
    puts $xdc_fh "set_property PACKAGE_PIN M15 \[get_ports {enet0_gmii_txd[2]}\]"
    puts $xdc_fh "set_property PACKAGE_PIN N15 \[get_ports {enet0_gmii_txd[3]}\]"
    puts $xdc_fh "set_property PACKAGE_PIN J15 \[get_ports MDIO_ETHERNET_0_0_mdio_io\]"
    puts $xdc_fh "set_property PACKAGE_PIN G14 \[get_ports MDIO_ETHERNET_0_0_mdc\]"
    puts $xdc_fh "set_property IOSTANDARD LVCMOS33 \[get_ports ETH_RESET\]"
    puts $xdc_fh "set_property PACKAGE_PIN H20 \[get_ports ETH_RESET\]"
    puts $xdc_fh "set_property SLEW FAST \[get_ports {enet0_gmii_txd[3]}\]"
    puts $xdc_fh "set_property SLEW FAST \[get_ports {enet0_gmii_txd[2]}\]"
    puts $xdc_fh "set_property SLEW FAST \[get_ports {enet0_gmii_txd[1]}\]"
    puts $xdc_fh "set_property SLEW FAST \[get_ports {enet0_gmii_txd[0]}\]"
    puts $xdc_fh "set_property SLEW FAST \[get_ports {ENET0_GMII_TX_EN_0[0]}\]"
    puts $xdc_fh "set_property SLEW FAST \[get_ports MDIO_ETHERNET_0_0_mdio_io\]"
    puts $xdc_fh "set_property SLEW FAST \[get_ports MDIO_ETHERNET_0_0_mdc\]"
    puts $xdc_fh "set eth_tx_clk_net \[get_nets -quiet ENET0_GMII_TX_CLK_0_IBUF\]"
    puts $xdc_fh "if {\[llength \$eth_tx_clk_net\] != 0} { set_property CLOCK_DEDICATED_ROUTE FALSE \$eth_tx_clk_net }"
    puts $xdc_fh "set eth_rx_clk_net \[get_nets -quiet ENET0_GMII_RX_CLK_0_IBUF\]"
    puts $xdc_fh "if {\[llength \$eth_rx_clk_net\] != 0} { set_property CLOCK_DEDICATED_ROUTE FALSE \$eth_rx_clk_net }"
}
close $xdc_fh
add_files -fileset constrs_1 -norecurse $pl_uart_xdc

if {[llength [get_drc_checks RTSTAT-10]]} {
    set_property SEVERITY Warning [get_drc_checks RTSTAT-10]
}

set_param general.maxThreads $build_jobs
synth_design -top riscv_ps_ddr_wrapper -part xc7z020clg400-1
set hdmi_written_src [get_cells -hierarchical -filter {NAME =~ */u_vibe_hdmi/has_written_q_reg}]
set hdmi_written_dst [get_cells -hierarchical -filter {NAME =~ */u_vibe_hdmi/has_written_meta_q_reg}]
if {[llength $hdmi_written_src] != 0 && [llength $hdmi_written_dst] != 0} {
    set_false_path -from $hdmi_written_src -to $hdmi_written_dst
}
if {$enable_eth} {
    set eth_tx_clk_net [get_nets -quiet ENET0_GMII_TX_CLK_0_IBUF]
    if {[llength $eth_tx_clk_net] != 0} {
        set_property CLOCK_DEDICATED_ROUTE FALSE $eth_tx_clk_net
    }
    set eth_rx_clk_net [get_nets -quiet ENET0_GMII_RX_CLK_0_IBUF]
    if {[llength $eth_rx_clk_net] != 0} {
        set_property CLOCK_DEDICATED_ROUTE FALSE $eth_rx_clk_net
    }
}
write_checkpoint -force $out_dir/riscv_ps_ddr_synth.dcp
report_utilization -file $out_dir/riscv_ps_ddr_utilization_synth.rpt

opt_design
place_design
phys_opt_design
route_design
write_checkpoint -force $out_dir/riscv_ps_ddr_routed.dcp
report_timing_summary -file $out_dir/riscv_ps_ddr_timing_summary.rpt
report_utilization -file $out_dir/riscv_ps_ddr_utilization_route.rpt
write_bitstream -force $out_dir/riscv_ps_ddr_wrapper.bit
file mkdir $out_dir/riscv_ps_ddr_hw.runs/impl_1
file copy -force $out_dir/riscv_ps_ddr_wrapper.bit $out_dir/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit
write_hwdef -force -file $out_dir/riscv_ps_ddr.hwdef
close_project
