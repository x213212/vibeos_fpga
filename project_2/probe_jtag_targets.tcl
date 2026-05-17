set out [open "H:/testproject/project_2/probe_jtag_targets.out" "w"]
connect -url tcp:127.0.0.1:3121
puts $out "XSDB_TARGETS"
puts $out [targets]
puts $out "JTAG_TARGETS"
catch {puts $out [jtag targets]} e
puts $out "JTAG_TARGETS_ERR=$e"
close $out
disconnect
