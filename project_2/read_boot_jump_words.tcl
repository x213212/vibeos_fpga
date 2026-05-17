set out [open "H:/testproject/project_2/read_boot_jump_words.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
foreach a {0x01000054 0x01012458 0x0101278c 0x01012790 0x01012794 0x01012798 0x011d4018} {
    if {[catch {mrd -value $a} v]} {
        puts $out [format "0x%08x ERROR %s" $a $v]
    } else {
        puts $out [format "0x%08x 0x%08x" $a [expr {$v & 0xffffffff}]]
    }
}
close $out
disconnect
