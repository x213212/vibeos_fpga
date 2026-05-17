connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}
foreach {name addr} {
    MEM_00000000 0x00000000
    MEM_00100000 0x00100000
    MEM_01000000 0x01000000
    MEM_011d4000 0x011d4000
    MEM_001d4000 0x001d4000
} {
    set v [mrd -value $addr]
    puts [format "%s=0x%08x" $name [expr {$v & 0xffffffff}]]
}
disconnect
