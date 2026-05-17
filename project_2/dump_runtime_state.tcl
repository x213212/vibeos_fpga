set out [open "H:/testproject/project_2/dump_runtime_state.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

proc rd {addr} {
    return [expr {[mrd -value $addr] & 0xffffffff}]
}

puts $out "RUNTIME_STATE_BEGIN"
foreach {name addr} {
    lwip_rand_state 0x011c0000
    taskTop         0x011d4010
    os_debug        0x011d4018
    fpga_bump_next  0x011d4048
    bitmap          0x011d404c
    alloc_pages     0x011d4050
    heap_total      0x011d4054
    alloc_end       0x011d4058
    alloc_start     0x011d405c
    malloc_calls    0x011d4064
    gui_mx          0x011c0044
    gui_my          0x011c0048
    usb_state_clear 0x011d42f8
    usb_host_init   0x011d42ec
    ps_gem_ready    0x011d4314
    ps_gem_debug    0x01bca140
} {
    if {[catch {rd $addr} v]} {
        puts $out [format "%s_READ_ERROR=%s" $name $v]
    } else {
        puts $out [format "%s=0x%08x" $name $v]
    }
}
puts $out "RUNTIME_STATE_END"
close $out
disconnect
