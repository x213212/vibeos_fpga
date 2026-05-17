connect -url tcp:127.0.0.1:3121
set out [open "H:/testproject/project_2/ulpi_vbus_kick.out" "w"]
puts $out "ULPI_VBUS_KICK_BEGIN"
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

set VIEW 0xE0002170
set PORTSC1 0xE0002184
set USBCMD 0xE0002140
set USBSTS 0xE0002144
set USBMODE 0xE00021A8

proc wait_clear {addr mask} {
    for {set i 0} {$i < 10000} {incr i} {
        if {([mrd -value $addr] & $mask) == 0} { return 0 }
    }
    return -1
}

proc ulpi_read {reg} {
    global VIEW
    mwr $VIEW 0xa0000000
    wait_clear $VIEW 0x80000000
    mwr $VIEW [expr {0x40000000 | (($reg & 0xff) << 16)}]
    wait_clear $VIEW 0x40000000
    return [expr {([mrd -value $VIEW] >> 8) & 0xff}]
}

proc ulpi_write {reg val} {
    global VIEW
    mwr $VIEW 0xa0000000
    wait_clear $VIEW 0x80000000
    mwr $VIEW [expr {0x60000000 | (($reg & 0xff) << 16) | ($val & 0xff)}]
    wait_clear $VIEW 0x40000000
}

puts $out [format "before CMD=0x%08x STS=0x%08x PORTSC=0x%08x MODE=0x%08x VIEW=0x%08x" \
    [mrd -value $USBCMD] [mrd -value $USBSTS] [mrd -value $PORTSC1] [mrd -value $USBMODE] [mrd -value $VIEW]]
puts $out [format "ulpi_vid=%02x%02x pid=%02x%02x otg=%02x intst=%02x" \
    [ulpi_read 0x01] [ulpi_read 0x00] [ulpi_read 0x03] [ulpi_read 0x02] [ulpi_read 0x0a] [ulpi_read 0x13]]

# OTG Control set register: DrvVbusExternal(6) | DrvVbus(5), keep pull-downs.
ulpi_write 0x0b 0x60
after 100
mwr $USBMODE 0x00000003
mwr $PORTSC1 [expr {[mrd -value $PORTSC1] | 0x00001000}]
mwr $USBCMD 0x00080001
after 500

puts $out [format "after  CMD=0x%08x STS=0x%08x PORTSC=0x%08x MODE=0x%08x VIEW=0x%08x" \
    [mrd -value $USBCMD] [mrd -value $USBSTS] [mrd -value $PORTSC1] [mrd -value $USBMODE] [mrd -value $VIEW]]
puts $out [format "ulpi_otg=%02x intst=%02x" [ulpi_read 0x0a] [ulpi_read 0x13]]

close $out
disconnect
