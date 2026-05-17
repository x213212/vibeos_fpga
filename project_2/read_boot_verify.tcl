connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
foreach addr {0x01000000 0x0100002c 0x01012940 0x01012408 0x011d4000 0x011d4cac 0x01c331f8} {
    puts [format "MEM_0x%08x=0x%08x" $addr [mrd -value $addr]]
}
disconnect
