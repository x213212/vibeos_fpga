set out [open "H:/testproject/project_2/dump_periodic.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
puts $out [format "PERIODICLIST=0x%08x" [mrd -value 0xE0002154]]
puts $out [format "USBCMD=0x%08x USBSTS=0x%08x FRINDEX=0x%08x" [mrd -value 0xE0002140] [mrd -value 0xE0002144] [mrd -value 0xE000214C]]
for {set i 0} {$i < 16} {incr i} {
    set a [expr {0x061db000 + $i*4}]
    puts $out [format "PERIODIC_%02d[0x%08x]=0x%08x" $i $a [mrd -value $a]]
}
close $out
disconnect
