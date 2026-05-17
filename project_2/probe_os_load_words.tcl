set out [open "H:/testproject/project_2/probe_os_load_words.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
for {set off 0} {$off < 128} {incr off 4} {
    set addr [expr {0x01000000 + $off}]
    puts $out [format "0x%08x=0x%08x" $addr [mrd -value $addr]]
}
close $out
disconnect
