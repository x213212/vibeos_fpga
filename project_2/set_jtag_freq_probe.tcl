open_hw_manager
connect_hw_server
set target [lindex [get_hw_targets *] 0]
open_hw_target $target
puts "TARGET=$target"
puts "FREQ_BEFORE=[get_property PARAM.FREQUENCY $target]"
catch {set_property PARAM.FREQUENCY 1000000 $target} set_freq
puts "SET_FREQ=$set_freq"
puts "FREQ_AFTER=[get_property PARAM.FREQUENCY $target]"
close_hw_manager
