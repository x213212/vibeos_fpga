set out [open "H:/testproject/project_2/read_vga_debug.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

proc show {out name addr} {
    if {[catch {mrd -value $addr} v]} {
        puts $out [format "%s_READ_ERROR=%s" $name $v]
    } else {
        puts $out [format "%s=0x%08x" $name $v]
    }
}

puts $out "READ_VGA_DEBUG_BEGIN"
show $out VGA_MAGIC      0x40000000
show $out VGA_WIDTH      0x40000004
show $out VGA_HEIGHT     0x40000008
show $out VGA_WRITES     0x4000000c
show $out VGA_LAST_ADDR  0x40000010
show $out VGA_LAST_DATA  0x40000014
close $out
disconnect
