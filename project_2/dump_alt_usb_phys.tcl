set out [open "H:/testproject/project_2/dump_alt_usb_phys.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
foreach base {0x051da000 0x061da000 0x851da000} {
    puts $out [format "BASE=0x%08x" $base]
    for {set off 0} {$off < 0x30} {incr off 4} {
        puts $out [format "+0x%02x 0x%08x" $off [mrd -value [expr {$base+$off}]]]
    }
}
close $out
disconnect
