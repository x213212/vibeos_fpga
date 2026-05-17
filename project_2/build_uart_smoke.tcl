proc require_arg {name} {
    global argv
    set idx [lsearch -exact $argv $name]
    if {$idx < 0 || $idx + 1 >= [llength $argv]} {
        error "Missing $name value. Example: vivado -mode batch -source build_uart_smoke.tcl -tclargs -clk_pin <PIN> -resetn_pin <PIN> -uart_tx_pin <PIN>"
    }
    return [lindex $argv [expr {$idx + 1}]]
}

set clk_pin [require_arg "-clk_pin"]
set resetn_pin [require_arg "-resetn_pin"]
set uart_tx_pin [require_arg "-uart_tx_pin"]

create_project uart_smoke H:/testproject/project_2/uart_smoke -part xc7z020clg400-1 -force
add_files H:/testproject/project_2/top.v
add_files H:/testproject/project_2/uart_smoke.v
set_property top uart_smoke [current_fileset]

create_clock -name sys_clk -period 10.000 [get_ports clk]
set_property PACKAGE_PIN $clk_pin [get_ports clk]
set_property IOSTANDARD LVCMOS33 [get_ports clk]

set_property PACKAGE_PIN $resetn_pin [get_ports resetn]
set_property IOSTANDARD LVCMOS33 [get_ports resetn]
set_property PULLUP true [get_ports resetn]

set_property PACKAGE_PIN $uart_tx_pin [get_ports uart_tx]
set_property IOSTANDARD LVCMOS33 [get_ports uart_tx]

set_property SEVERITY {Warning} [get_drc_checks NSTD-1]
set_property SEVERITY {Warning} [get_drc_checks UCIO-1]

synth_design -top uart_smoke -part xc7z020clg400-1
opt_design
place_design
route_design
write_bitstream -force H:/testproject/project_2/uart_smoke.bit
