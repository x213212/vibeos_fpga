open_hw_manager
connect_hw_server
set targets [get_hw_targets *]
if {[llength $targets] > 0} {
    current_hw_target [lindex $targets 0]
    open_hw_target
}
set targets [get_hw_targets *]
set devices [get_hw_devices *]
puts "HW_TARGETS:"
puts "COUNT=[llength $targets]"
foreach t $targets {
    puts "  $t"
}
puts "HW_DEVICES:"
puts "COUNT=[llength $devices]"
foreach d $devices {
    puts "  $d"
}
close_hw_manager
