set out [open "H:/testproject/project_2/probe_dap_slow_jtag.out" "w"]
connect -url tcp:127.0.0.1:3121
catch {jtag targets -set -filter {level == 0}}
catch {jtag frequency 100000} jf
puts $out "JTAG_FREQ_RESULT=$jf"
after 500
puts $out [targets]
close $out
disconnect
