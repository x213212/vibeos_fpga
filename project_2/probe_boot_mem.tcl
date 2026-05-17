connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

foreach {name addr} {
    TRACE_PHY_012d4d78 0x012d4d78
    TRACE_ALT_002d4d78 0x002d4d78
    TASK_META_011d4cb8 0x011d4cb8
    TASKTOP_011c4010 0x011c4010
    BSSSTART_011c4000 0x011c4000
} {
    if {[catch {mrd -value $addr} value]} {
        puts [format "%s_READ_ERROR=%s" $name $value]
    } else {
        puts [format "%s=0x%08x" $name [expr {$value & 0xffffffff}]]
    }
}

disconnect
