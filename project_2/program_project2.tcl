open_hw
connect_hw_server
open_hw_target [lindex [get_hw_targets *] 0]
current_hw_device [lindex [get_hw_devices xc7z020*] 0]
refresh_hw_device [current_hw_device]
set_property PROGRAM.FILE H:/testproject/project_2/project_2.runs/impl_1/top.bit [current_hw_device]
program_hw_devices [current_hw_device]
close_hw
