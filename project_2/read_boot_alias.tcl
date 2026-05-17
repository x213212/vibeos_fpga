set out [open "H:/testproject/project_2/read_boot_alias.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
foreach a {0x00000000 0x00100000 0x01000000 0x011d4000 0x80000000} {
    if {[catch {mrd -value $a} v]} {
        puts $out [format "0x%08x ERROR %s" $a $v]
    } else {
        puts $out [format "0x%08x 0x%08x" $a [expr {$v & 0xffffffff}]]
    }
}
close $out
disconnect
