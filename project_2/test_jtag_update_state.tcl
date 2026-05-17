connect
jtag targets -set -filter {name =~ "xc7z020*" && irlen == 6}
set out [open "H:/testproject/project_2/test_jtag_update_state.txt" w]
set s [jtag sequence]
$s clear
$s state RESET
$s state IDLE
$s irshift -bits -state IDLE 6 010000
$s drshift -capture -bits -state DRUPDATE 10 0000000000
puts $out "DR1=[$s run -bits -single]"
$s clear
$s state IDLE
$s irshift -bits -state IDLE 6 010000
$s drshift -capture -bits -state DRUPDATE 10 0000000000
puts $out "DR2=[$s run -bits -single]"
close $out
$s delete
disconnect
