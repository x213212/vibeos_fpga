create_project jtag_smoke H:/testproject/project_2/jtag_smoke -part xc7z020clg400-1 -force
add_files H:/testproject/project_2/jtag_console_smoke.v
set_property top jtag_console_smoke [current_fileset]
synth_design -top jtag_console_smoke -part xc7z020clg400-1
opt_design
place_design
route_design
write_bitstream -force H:/testproject/project_2/jtag_console_smoke.bit
