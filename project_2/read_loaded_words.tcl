set out [open "H:/testproject/project_2/read_loaded_words.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
foreach addr {0x01012240 0x01012244 0x01012248 0x0101224c 0x01012250 0x01012254 0x01012258 0x0101225c 0x01016640 0x01016680} {
    if {[catch {mrd -value $addr} v]} {
        puts $out [format "0x%08x READ_ERROR %s" $addr $v]
    } else {
        puts $out [format "0x%08x 0x%08x" $addr [expr {$v & 0xffffffff}]]
    }
}
close $out
disconnect
