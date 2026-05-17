set out [open "H:/testproject/project_2/read_fpga_boot_trace.out" "w"]

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

puts $out "READ_FPGA_BOOT_TRACE_BEGIN"
set base 0x012d4dc4
for {set i 0} {$i < 16} {incr i} {
    set addr [expr {$base + ($i * 4)}]
    if {[catch {mrd -value $addr} v]} {
        puts $out [format "TRACE%d_READ_ERROR=%s" $i $v]
    } else {
        puts $out [format "TRACE%d=0x%08x" $i [expr {$v & 0xffffffff}]]
    }
}
close $out
disconnect
