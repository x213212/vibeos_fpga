connect -url tcp:127.0.0.1:3121
after 1000
puts "XSDB_FORCE_OPEN_BEGIN"
puts "JTAG_BEFORE"
puts [jtag targets]
if {[catch {jtag targets -open -filter {level == 0}} e]} {
    puts "JTAG_OPEN_ERROR=$e"
} else {
    puts "JTAG_OPEN_OK"
}
after 1000
puts "JTAG_AFTER"
puts [jtag targets]
puts "TARGETS_AFTER"
puts [targets]
disconnect
