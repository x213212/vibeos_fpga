connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
for {set off 0x00} {$off <= 0x70} {incr off 4} {
    set addr [expr {0x01000000 + $off}]
    puts [format "DDR_%08x=0x%08x" $addr [mrd -value $addr]]
}
foreach addr {0x010018f0 0x010018f4 0x01cd2fa0 0x01cd2fac 0x01cd3000 0x01cd3010 0x01cd303c} {
    puts [format "DDR_%08x=0x%08x" $addr [mrd -value $addr]]
}
disconnect
