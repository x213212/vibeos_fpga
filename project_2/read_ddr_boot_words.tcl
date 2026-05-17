set out [open "H:/testproject/project_2/read_ddr_boot_words.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
foreach a {0x01000000 0x01000010 0x01000020 0x01000030 0x01000040 0x01000050} {
    if {[catch {mrd -value $a} v]} { puts $out [format "%s ERR %s" $a $v] } else { puts $out [format "%s 0x%08x" $a $v] }
}
close $out
disconnect
