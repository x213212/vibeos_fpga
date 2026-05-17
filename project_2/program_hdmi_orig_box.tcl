open_hw_manager
connect_hw_server
set targets [get_hw_targets *]
if {[llength $targets] == 0} {
    error "No hardware targets"
}
current_hw_target [lindex $targets 0]
open_hw_target
set dev [lindex [get_hw_devices xc7z020*] 0]
if {$dev eq ""} {
    error "No xc7z020 device"
}
current_hw_device $dev
set_property PROGRAM.FILE H:/testproject/project_2/hdmi_orig_box_hw/hdmi_orig_box_hw.runs/impl_1/hdmi_orig_box.bit $dev
program_hw_devices $dev
puts "HDMI_ORIG_BOX_PROGRAMMED"
close_hw_manager
