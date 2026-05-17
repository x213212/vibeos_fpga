set out [open "H:/testproject/project_2/read_boot_trace.out" "w"]
set trace_addr 0x012d4d78

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

puts $out "READ_BOOT_TRACE_BEGIN"
for {set i 0} {$i < 16} {incr i} {
    set addr [expr {$trace_addr + ($i * 4)}]
    if {[catch {mrd -value $addr} value]} {
        puts $out [format "TRACE%d_READ_ERROR=%s" $i $value]
    } else {
        puts $out [format "TRACE%d=0x%08x" $i [expr {$value & 0xffffffff}]]
    }
}

close $out
disconnect
