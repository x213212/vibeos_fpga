set_property PACKAGE_PIN N18 [get_ports pl_clk_50m]
set_property IOSTANDARD LVCMOS33 [get_ports pl_clk_50m]
create_clock -period 20.000 -name pl_clk_50m [get_ports pl_clk_50m]

set_property PACKAGE_PIN U18 [get_ports hdmi_clk_p]
set_property PACKAGE_PIN U19 [get_ports hdmi_clk_n]
set_property PACKAGE_PIN N20 [get_ports hdmi_d2_p]
set_property PACKAGE_PIN P20 [get_ports hdmi_d2_n]
set_property PACKAGE_PIN T20 [get_ports hdmi_d1_p]
set_property PACKAGE_PIN U20 [get_ports hdmi_d1_n]
set_property PACKAGE_PIN V20 [get_ports hdmi_d0_p]
set_property PACKAGE_PIN W20 [get_ports hdmi_d0_n]

set_property IOSTANDARD TMDS_33 [get_ports {hdmi_clk_p hdmi_clk_n hdmi_d2_p hdmi_d2_n hdmi_d1_p hdmi_d1_n hdmi_d0_p hdmi_d0_n}]
