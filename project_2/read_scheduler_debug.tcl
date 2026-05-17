set out [open "H:/testproject/project_2/read_scheduler_debug.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

proc r32 {out name addr} {
    if {[catch {mrd -value $addr} v]} {
        puts $out [format "%s_READ_ERROR=%s" $name $v]
    } else {
        puts $out [format "%s=0x%08x" $name $v]
    }
}

puts $out "READ_SCHEDULER_DEBUG_BEGIN"
puts $out "fpga_boot_trace"
for {set i -4} {$i < 20} {incr i} {
    r32 $out [format "boot_%02d" $i] [expr {0x012d4dc4 + $i * 4}]
}
r32 $out taskTop         0x011d4010
r32 $out current_task_id 0x011c0008
r32 $out ctx_now         0x011d4008
r32 $out current_level   0x011d400c
r32 $out quota           0x011c0004

puts $out "ctx_os"
for {set i 0} {$i < 14} {incr i} {
    r32 $out [format "ctx_os_%02d" $i] [expr {0x011d482c + $i * 4}]
}

puts $out "ctx_tasks"
for {set i 0} {$i < 8} {incr i} {
    set base [expr {0x011d4864 + $i * 56}]
    r32 $out [format "ctx%d_ra" $i] $base
    r32 $out [format "ctx%d_sp" $i] [expr {$base + 4}]
}

puts $out "task_meta"
for {set i 0} {$i < 8} {incr i} {
    set base [expr {0x011d4d04 + $i * 12}]
    r32 $out [format "task%d_flags" $i] $base
    r32 $out [format "task%d_level" $i] [expr {$base + 4}]
    r32 $out [format "task%d_prio" $i] [expr {$base + 8}]
}

puts $out "task_levels"
for {set level 0} {$level < 4} {incr level} {
    set base [expr {0x011d4be4 + $level * 72}]
    r32 $out [format "level%d_running" $level] [expr {$base + 64}]
    r32 $out [format "level%d_now" $level] [expr {$base + 68}]
    for {set slot 0} {$slot < 8} {incr slot} {
        r32 $out [format "level%d_task%d" $level $slot] [expr {$base + $slot * 4}]
    }
}

close $out
catch {con}
disconnect
