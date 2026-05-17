set out [open "H:/testproject/project_2/read_usb_mouse_state.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

# HID driver uses an uncached CPU/PS physical scratch page at 0x07000000.
set base 0x07000000
puts $out "USB_MOUSE_STATE_BEGIN"
for {set off 0} {$off < 0x230} {incr off 4} {
    set value [mrd -value [expr {$base + $off}]]
    puts $out [format "+0x%03x 0x%08x" $off $value]
}
close $out
disconnect
