connect
puts "TARGETS_RETURN_BEGIN"
puts [targets]
puts "TARGETS_RETURN_END"
puts "TARGETS_VERBOSE_BEGIN"
catch {targets -target-properties} target_props
puts $target_props
puts "TARGETS_VERBOSE_END"
puts "JTAG_TARGETS_RETURN_BEGIN"
puts [jtag targets]
puts "JTAG_TARGETS_RETURN_END"
disconnect
