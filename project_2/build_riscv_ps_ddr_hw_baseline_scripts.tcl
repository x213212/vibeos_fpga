set root_dir H:/testproject
set out_dir $root_dir/project_2/riscv_ps_ddr_hw_candidate
set pl_uart_tx_pin N17
set hdmi_src_dir $root_dir/project_2/zynq_z7lite_training/Tutorial/part1/10.hdmi_simple/10.hdmi_simple.srcs/sources_1/new

for {set i 0} {$i < [llength $argv]} {incr i} {
    set arg [lindex $argv $i]
    if {$arg eq "-pl_uart_tx_pin"} {
        incr i
        if {$i >= [llength $argv]} {
            error "Missing value for -pl_uart_tx_pin"
        }
        set pl_uart_tx_pin [lindex $argv $i]
    }
}

file delete -force $out_dir

create_project riscv_ps_ddr_hw $out_dir -part xc7z020clg400-1 -force

set core_files [concat \
    [glob $root_dir/ultraembedded_riscv/core/riscv/*.v] \
    [glob $root_dir/ultraembedded_riscv/top_cache_axi/src_v/*.v] \
    [list $root_dir/project_2/pl_mmio_jtag_console.v] \
    [list $root_dir/project_2/pl_mouse_mmio.v] \
    [list $root_dir/project_2/vibe_hdmi_mmio.v] \
    [list $hdmi_src_dir/async_reset.v] \
    [list $hdmi_src_dir/display_clock.v] \
    [list $hdmi_src_dir/display_timings.v] \
    [list $hdmi_src_dir/dvi_generator.v] \
    [list $hdmi_src_dir/serializer_10to1.v] \
    [list $hdmi_src_dir/tmds_encoder_dvi.v] \
    [list $root_dir/project_2/riscv_ps_ddr_top.v] \
]
add_files -norecurse $core_files
set_property include_dirs [list $root_dir/ultraembedded_riscv/core/riscv] [current_fileset]
update_compile_order -fileset sources_1

create_bd_design riscv_ps_ddr
create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 processing_system7_0
create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_ps7_0_100M
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 axi_instr_ic
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 axi_mem_ic
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 axi_iop_ic
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 axi_ctrl_ic
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio:2.0 axi_cpu_reset
create_bd_cell -type module -reference riscv_ps_ddr_top riscv_0

set_property -dict [list \
    CONFIG.PCW_UART0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_UART0_UART0_IO {MIO 14 .. 15} \
    CONFIG.PCW_UART0_GRP_FULL_ENABLE {0} \
    CONFIG.PCW_UART_PERIPHERAL_FREQMHZ {50} \
    CONFIG.PCW_USB0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_EN_USB0 {1} \
    CONFIG.PCW_USB0_USB0_IO {MIO 28 .. 39} \
    CONFIG.PCW_USB0_PERIPHERAL_FREQMHZ {60} \
    CONFIG.PCW_USB0_RESET_ENABLE {0} \
    CONFIG.PCW_USB_RESET_ENABLE {0} \
    CONFIG.PCW_GPIO_MIO_GPIO_ENABLE {1} \
    CONFIG.PCW_GPIO_MIO_GPIO_IO {MIO} \
    CONFIG.PCW_MIO_46_DIRECTION {inout} \
    CONFIG.PCW_MIO_46_IOTYPE {LVCMOS 3.3V} \
    CONFIG.PCW_MIO_46_PULLUP {enabled} \
    CONFIG.PCW_MIO_46_SLEW {slow} \
    CONFIG.PCW_SD0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_SD0_SD0_IO {MIO 40 .. 45} \
    CONFIG.PCW_SD0_GRP_CD_ENABLE {0} \
    CONFIG.PCW_SD0_GRP_WP_ENABLE {0} \
    CONFIG.PCW_SDIO_PERIPHERAL_FREQMHZ {50} \
    CONFIG.PCW_PRESET_BANK0_VOLTAGE {LVCMOS 3.3V} \
    CONFIG.PCW_PRESET_BANK1_VOLTAGE {LVCMOS 1.8V} \
    CONFIG.PCW_EN_CLK0_PORT {1} \
    CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {60} \
    CONFIG.PCW_USE_M_AXI_GP0 {1} \
    CONFIG.PCW_USE_M_AXI_GP1 {0} \
    CONFIG.PCW_USE_S_AXI_GP0 {1} \
    CONFIG.PCW_USE_S_AXI_GP1 {0} \
    CONFIG.PCW_USE_S_AXI_HP0 {1} \
    CONFIG.PCW_S_AXI_HP0_DATA_WIDTH {32} \
    CONFIG.PCW_USE_S_AXI_HP1 {1} \
    CONFIG.PCW_S_AXI_HP1_DATA_WIDTH {32} \
    CONFIG.PCW_USE_S_AXI_HP2 {0} \
    CONFIG.PCW_USE_S_AXI_HP3 {0} \
    CONFIG.PCW_UIPARAM_DDR_MEMORY_TYPE {DDR 3} \
    CONFIG.PCW_UIPARAM_DDR_BL {8} \
    CONFIG.PCW_UIPARAM_DDR_BUS_WIDTH {16 Bit} \
    CONFIG.PCW_UIPARAM_DDR_DRAM_WIDTH {16 Bits} \
    CONFIG.PCW_UIPARAM_DDR_DEVICE_CAPACITY {4096 MBits} \
    CONFIG.PCW_UIPARAM_DDR_PARTNO {MT41J256M16 RE-125} \
    CONFIG.PCW_UIPARAM_DDR_SPEED_BIN {DDR3_1066F} \
    CONFIG.PCW_UIPARAM_DDR_FREQ_MHZ {533.333333} \
    CONFIG.PCW_UIPARAM_DDR_ROW_ADDR_COUNT {15} \
    CONFIG.PCW_UIPARAM_DDR_COL_ADDR_COUNT {10} \
    CONFIG.PCW_UIPARAM_DDR_CL {7} \
    CONFIG.PCW_UIPARAM_DDR_CWL {6} \
    CONFIG.PCW_UIPARAM_DDR_T_RCD {7} \
    CONFIG.PCW_UIPARAM_DDR_T_RP {7} \
    CONFIG.PCW_UIPARAM_DDR_T_RC {48.91} \
    CONFIG.PCW_UIPARAM_DDR_T_RAS_MIN {35.0} \
    CONFIG.PCW_UIPARAM_DDR_T_FAW {40.0} \
    CONFIG.PCW_UIPARAM_DDR_TRAIN_WRITE_LEVEL {1} \
    CONFIG.PCW_UIPARAM_DDR_TRAIN_READ_GATE {1} \
    CONFIG.PCW_UIPARAM_DDR_TRAIN_DATA_EYE {1} \
    CONFIG.PCW_UIPARAM_DDR_USE_INTERNAL_VREF {0} \
    CONFIG.PCW_UIPARAM_DDR_DQS_TO_CLK_DELAY_0 {-0.051} \
    CONFIG.PCW_UIPARAM_DDR_DQS_TO_CLK_DELAY_1 {-0.006} \
    CONFIG.PCW_UIPARAM_DDR_DQS_TO_CLK_DELAY_2 {-0.009} \
    CONFIG.PCW_UIPARAM_DDR_DQS_TO_CLK_DELAY_3 {-0.033} \
    CONFIG.PCW_UIPARAM_DDR_BOARD_DELAY0 {0.279} \
    CONFIG.PCW_UIPARAM_DDR_BOARD_DELAY1 {0.260} \
    CONFIG.PCW_UIPARAM_DDR_BOARD_DELAY2 {0.085} \
    CONFIG.PCW_UIPARAM_DDR_BOARD_DELAY3 {0.092} \
] [get_bd_cells processing_system7_0]

set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {1}] [get_bd_cells axi_instr_ic]
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {1}] [get_bd_cells axi_mem_ic]
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {1}] [get_bd_cells axi_iop_ic]
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {2}] [get_bd_cells axi_ctrl_ic]
set_property -dict [list CONFIG.C_GPIO_WIDTH {1} CONFIG.C_ALL_OUTPUTS {1} CONFIG.C_DOUT_DEFAULT {0x00000000}] [get_bd_cells axi_cpu_reset]

connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] [get_bd_pins rst_ps7_0_100M/slowest_sync_clk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_RESET0_N] [get_bd_pins rst_ps7_0_100M/ext_reset_in]

foreach pin [list \
    axi_instr_ic/ACLK axi_instr_ic/S00_ACLK axi_instr_ic/M00_ACLK \
    axi_mem_ic/ACLK axi_mem_ic/S00_ACLK axi_mem_ic/M00_ACLK \
    axi_iop_ic/ACLK axi_iop_ic/S00_ACLK axi_iop_ic/M00_ACLK \
    axi_ctrl_ic/ACLK axi_ctrl_ic/S00_ACLK axi_ctrl_ic/M00_ACLK axi_ctrl_ic/M01_ACLK \
    axi_cpu_reset/s_axi_aclk riscv_0/clk processing_system7_0/M_AXI_GP0_ACLK processing_system7_0/S_AXI_GP0_ACLK processing_system7_0/S_AXI_HP0_ACLK processing_system7_0/S_AXI_HP1_ACLK \
] {
    connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] [get_bd_pins $pin]
}

foreach pin [list \
    axi_instr_ic/ARESETN axi_instr_ic/S00_ARESETN axi_instr_ic/M00_ARESETN \
    axi_mem_ic/ARESETN axi_mem_ic/S00_ARESETN axi_mem_ic/M00_ARESETN \
    axi_iop_ic/ARESETN axi_iop_ic/S00_ARESETN axi_iop_ic/M00_ARESETN \
    axi_ctrl_ic/ARESETN axi_ctrl_ic/S00_ARESETN axi_ctrl_ic/M00_ARESETN axi_ctrl_ic/M01_ARESETN \
    axi_cpu_reset/s_axi_aresetn riscv_0/resetn \
] {
    connect_bd_net [get_bd_pins rst_ps7_0_100M/peripheral_aresetn] [get_bd_pins $pin]
}

connect_bd_net [get_bd_pins axi_cpu_reset/gpio_io_o] [get_bd_pins riscv_0/cpu_resetn]

connect_bd_intf_net [get_bd_intf_pins riscv_0/M_AXI_I] [get_bd_intf_pins axi_instr_ic/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_instr_ic/M00_AXI] [get_bd_intf_pins processing_system7_0/S_AXI_HP0]

connect_bd_intf_net [get_bd_intf_pins riscv_0/M_AXI_D] [get_bd_intf_pins axi_mem_ic/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_mem_ic/M00_AXI] [get_bd_intf_pins processing_system7_0/S_AXI_HP1]

connect_bd_intf_net [get_bd_intf_pins riscv_0/M_AXI_IOP] [get_bd_intf_pins axi_iop_ic/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_iop_ic/M00_AXI] [get_bd_intf_pins processing_system7_0/S_AXI_GP0]

connect_bd_intf_net [get_bd_intf_pins processing_system7_0/M_AXI_GP0] [get_bd_intf_pins axi_ctrl_ic/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_ctrl_ic/M00_AXI] [get_bd_intf_pins axi_cpu_reset/S_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_ctrl_ic/M01_AXI] [get_bd_intf_pins riscv_0/S_AXI_DBG]

make_bd_pins_external [get_bd_pins riscv_0/pl_uart_tx]
set pl_uart_bd_port [lindex [get_bd_ports *pl_uart_tx*] 0]
if {$pl_uart_bd_port eq ""} {
    error "Could not create external PL UART TX BD port"
}
set_property name pl_uart_tx $pl_uart_bd_port

foreach hdmi_pin [list hdmi_tx_clk_n hdmi_tx_clk_p hdmi_tx_n hdmi_tx_p] {
    make_bd_pins_external [get_bd_pins riscv_0/$hdmi_pin]
    set hdmi_bd_port [lindex [get_bd_ports *$hdmi_pin*] 0]
    if {$hdmi_bd_port eq ""} {
        error "Could not create external HDMI BD port for $hdmi_pin"
    }
    set_property name $hdmi_pin $hdmi_bd_port
}

assign_bd_address
set_property range 64K [get_bd_addr_segs {processing_system7_0/Data/SEG_axi_cpu_reset_Reg}]
set_property offset 0x41200000 [get_bd_addr_segs {processing_system7_0/Data/SEG_axi_cpu_reset_Reg}]
set dbg_seg ""
foreach seg [get_bd_addr_segs -of_objects [get_bd_addr_spaces processing_system7_0/Data]] {
    if {[string match "*riscv_0*" $seg]} {
        set dbg_seg $seg
    }
}
if {$dbg_seg eq ""} {
    error "Could not find RISC-V debug AXI address segment"
}
set_property range 64K $dbg_seg
set_property offset 0x41210000 $dbg_seg

validate_bd_design
save_bd_design
generate_target all [get_files $out_dir/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/riscv_ps_ddr.bd]

set wrapper [make_wrapper -files [get_files $out_dir/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/riscv_ps_ddr.bd] -top]
add_files -norecurse $wrapper
set_property top riscv_ps_ddr_wrapper [current_fileset]
update_compile_order -fileset sources_1

set pl_uart_xdc $out_dir/pl_uart_tx.xdc
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
close $xdc_fh
add_files -fileset constrs_1 -norecurse $pl_uart_xdc

if {[llength [get_drc_checks RTSTAT-10]]} {
    set_property SEVERITY Warning [get_drc_checks RTSTAT-10]
}

launch_runs synth_1 -scripts_only
launch_runs impl_1 -to_step write_bitstream -scripts_only

write_hwdef -force -file $out_dir/riscv_ps_ddr.hwdef
close_project
