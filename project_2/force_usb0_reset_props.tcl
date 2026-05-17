open_project H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.xpr
open_bd_design H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/riscv_ps_ddr.bd
set ps [get_bd_cells processing_system7_0]
set_property -dict [list \
    CONFIG.PCW_USB0_RESET_ENABLE {1} \
    CONFIG.PCW_USB0_RESET_IO {MIO 8} \
    CONFIG.PCW_USB_RESET_ENABLE {1} \
    CONFIG.PCW_USB_RESET_SELECT {USB0} \
    CONFIG.PCW_USB_RESET_POLARITY {Active Low} \
] $ps
set out [open H:/testproject/project_2/force_usb0_reset_props.out w]
foreach p [list CONFIG.PCW_USB0_RESET_ENABLE CONFIG.PCW_USB0_RESET_IO CONFIG.PCW_USB_RESET_ENABLE CONFIG.PCW_USB_RESET_SELECT CONFIG.PCW_USB_RESET_POLARITY CONFIG.PCW_MIO_8_IOTYPE CONFIG.PCW_MIO_8_PULLUP CONFIG.PCW_MIO_8_SLEW] {
    catch {puts $out "$p = [get_property $p $ps]"}
}
close $out
save_bd_design
close_project
