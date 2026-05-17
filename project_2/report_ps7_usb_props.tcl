open_project H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.xpr
open_bd_design H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/riscv_ps_ddr.bd
set out [open H:/testproject/project_2/report_ps7_usb_props.out w]
foreach p [lsort [list_property [get_bd_cells processing_system7_0]]] {
    if {[string match -nocase *USB* $p] || [string match -nocase *MIO_8* $p] || [string match -nocase *RESET* $p]} {
        catch {puts $out "$p = [get_property $p [get_bd_cells processing_system7_0]]"}
    }
}
close $out
close_project
