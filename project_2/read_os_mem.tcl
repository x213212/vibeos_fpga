set out [open "H:/testproject/project_2/read_os_mem.out" "w"]

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

puts $out "READ_OS_MEM_BEGIN"
foreach addr {0x00000000 0x00002408 0x001c4000 0x002d4d78 0x00c331f8 0x01000000 0x01000020 0x01002408 0x011c4000 0x012d4d78 0x01c331f8} {
    if {[catch {mrd -value $addr} value]} {
        puts $out [format "MEM_0x%08x_ERROR=%s" $addr $value]
    } else {
        puts $out [format "MEM_0x%08x=0x%08x" $addr [expr {$value & 0xffffffff}]]
    }
}

close $out
disconnect
