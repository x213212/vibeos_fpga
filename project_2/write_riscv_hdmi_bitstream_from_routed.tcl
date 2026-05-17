open_checkpoint H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper_routed.dcp
set_property SEVERITY Warning [get_drc_checks RTSTAT-10]
set_property PACKAGE_PIN U19 [get_ports hdmi_tx_clk_n]
set_property PACKAGE_PIN U18 [get_ports hdmi_tx_clk_p]
set_property PACKAGE_PIN W20 [get_ports {hdmi_tx_n[0]}]
set_property PACKAGE_PIN U20 [get_ports {hdmi_tx_n[1]}]
set_property PACKAGE_PIN P20 [get_ports {hdmi_tx_n[2]}]
set_property PACKAGE_PIN V20 [get_ports {hdmi_tx_p[0]}]
set_property PACKAGE_PIN T20 [get_ports {hdmi_tx_p[1]}]
set_property PACKAGE_PIN N20 [get_ports {hdmi_tx_p[2]}]
set_property IOSTANDARD TMDS_33 [get_ports {hdmi_tx_clk_n hdmi_tx_clk_p hdmi_tx_n[0] hdmi_tx_n[1] hdmi_tx_n[2] hdmi_tx_p[0] hdmi_tx_p[1] hdmi_tx_p[2]}]
write_bitstream -force H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit
