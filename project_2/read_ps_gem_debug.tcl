set out [open "H:/testproject/project_2/read_ps_gem_debug.out" "w"]

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

puts $out "READ_PS_GEM_DEBUG_BEGIN"
set base 0x01bca140
for {set i 0} {$i < 8} {incr i} {
    set addr [expr {$base + ($i * 4)}]
    if {[catch {mrd -value $addr} v]} {
        puts $out [format "ETH_DBG%d_READ_ERROR=%s" $i $v]
    } else {
        puts $out [format "ETH_DBG%d=0x%08x" $i [expr {$v & 0xffffffff}]]
    }
}
close $out
disconnect
