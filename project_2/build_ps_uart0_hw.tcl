set out_dir H:/testproject/project_2/ps_uart0_hw
file delete -force $out_dir

create_project ps_uart0_hw $out_dir -part xc7z020clg400-1 -force
create_bd_design ps_uart0
create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 processing_system7_0

set_property -dict [list \
    CONFIG.PCW_UART0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_UART0_UART0_IO {MIO 14 .. 15} \
    CONFIG.PCW_UART0_GRP_FULL_ENABLE {0} \
    CONFIG.PCW_UART_PERIPHERAL_FREQMHZ {50} \
    CONFIG.PCW_PRESET_BANK0_VOLTAGE {LVCMOS 3.3V} \
    CONFIG.PCW_PRESET_BANK1_VOLTAGE {LVCMOS 1.8V} \
    CONFIG.PCW_EN_CLK0_PORT {0} \
    CONFIG.PCW_USE_M_AXI_GP0 {0} \
    CONFIG.PCW_USE_M_AXI_GP1 {0} \
    CONFIG.PCW_USE_S_AXI_GP0 {0} \
    CONFIG.PCW_USE_S_AXI_GP1 {0} \
    CONFIG.PCW_USE_S_AXI_HP0 {0} \
    CONFIG.PCW_USE_S_AXI_HP1 {0} \
    CONFIG.PCW_USE_S_AXI_HP2 {0} \
    CONFIG.PCW_USE_S_AXI_HP3 {0} \
] [get_bd_cells processing_system7_0]

validate_bd_design
save_bd_design
generate_target all [get_files $out_dir/ps_uart0_hw.srcs/sources_1/bd/ps_uart0/ps_uart0.bd]

set wrapper [make_wrapper -files [get_files $out_dir/ps_uart0_hw.srcs/sources_1/bd/ps_uart0/ps_uart0.bd] -top]
add_files -norecurse $wrapper
set_property top ps_uart0_wrapper [current_fileset]
update_compile_order -fileset sources_1

write_hwdef -force -file H:/testproject/project_2/ps_uart0_hw/ps_uart0.hwdef
close_project
