set out [open "H:/testproject/project_2/read_mouse_debug.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
set target_selected 0
if {![catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}]} {
    set target_selected 1
}
if {!$target_selected && ![catch {targets -set -filter {name =~ "APU*"}}]} {
    set target_selected 1
}
puts $out "READ_MOUSE_DEBUG_BEGIN"
catch {puts $out [targets -target-properties]}
if {!$target_selected} {
    puts $out "TARGET_SELECT_ERROR"
    puts $out [targets]
    close $out
    catch {con}
    disconnect
    exit
}
if {[catch {mrd -value 0x412100b0} v]} {
    puts $out "MOUSE_DEBUG_READ_ERROR=$v"
    close $out
    catch {con}
    disconnect
    exit
}
puts $out [format "MOUSE_DEBUG=0x%08x buttons=0x%02x wheel=0x%02x seq=%d" $v [expr {($v >> 24) & 0xff}] [expr {($v >> 16) & 0xff}] [expr {$v & 0xffff}]]
for {set i 0} {$i < 16} {incr i} {
    set addr [expr {0x412100b4 + ($i * 4)}]
    if {[catch {mrd -value $addr} rv]} {
        puts $out [format "MOUSE_DBG%d_READ_ERROR=%s" $i $rv]
    } else {
        puts $out [format "MOUSE_DBG%d=0x%08x" $i $rv]
    }
}
close $out
catch {con}
disconnect
