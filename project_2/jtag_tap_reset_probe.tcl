set out [open "H:/testproject/project_2/jtag_tap_reset_probe.out" "w"]
connect -url tcp:127.0.0.1:3121
puts $out "BEFORE"
puts $out [jtag targets]
catch {jtag targets -set -filter {name =~ "arm_dap"}} e1
puts $out "SET_ARM_DAP=$e1"
catch {
    set seq [jtag sequence]
    $seq state RESET
    $seq run
    $seq delete
} e2
puts $out "SEQ_RESET=$e2"
after 1000
puts $out "AFTER"
puts $out [targets]
close $out
disconnect
