set out [open "H:/testproject/project_2/dump_mouse_data_window.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
for {set off 0x1ff0} {$off < 0x2060} {incr off 4} {
    set addr [expr {0x061da000 + $off}]
    puts $out [format "+0x%04x [0x%08x]=0x%08x" $off $addr [mrd -value $addr]]
}
close $out
disconnect
