connect
jtag targets -set -filter {name =~ "xc7z020*" && irlen == 6}
set seq [jtag sequence]
set user1_ir_bits "010000"
set pop_dr_bits "0000000000"
$seq clear
$seq state RESET
$seq state IDLE

for {set i 0} {$i < 20} {incr i} {
    $seq clear
    $seq state IDLE
    $seq irshift -bits -state IDLE 6 $user1_ir_bits
    $seq drshift -capture -bits -state IDLE 10 $pop_dr_bits
    set dr [$seq run -bits -single]
    set dr10 [string range $dr 0 9]
    puts "DR=$dr DR10=$dr10 DATA=[string range $dr10 0 7] HAVE=[string index $dr10 8] OVF=[string index $dr10 9]"
    after 100
}

$seq delete
disconnect
