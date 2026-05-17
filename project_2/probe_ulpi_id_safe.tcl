connect -url tcp:127.0.0.1:3121
set out [open "H:/testproject/project_2/probe_ulpi_id_safe.out" "w"]
puts $out "PROBE_ULPI_ID_SAFE_BEGIN"
configparams force-mem-access 1
if {[catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}]} {
    targets -set -filter {name =~ "APU*"}
}

set VIEW 0xE0002170
set USBCMD 0xE0002140
set USBSTS 0xE0002144
set PORTSC1 0xE0002184
set USBMODE 0xE00021A8

proc wait_clear {addr mask} {
    for {set i 0} {$i < 10000} {incr i} {
        if {([mrd -value $addr] & $mask) == 0} { return 0 }
        after 1
    }
    return -1
}

proc ulpi_read {reg} {
    global VIEW
    mwr $VIEW 0xa0000000
    wait_clear $VIEW 0x80000000
    mwr $VIEW [expr {0x40000000 | (($reg & 0xff) << 16)}]
    if {[wait_clear $VIEW 0x40000000] != 0} {
        return -1
    }
    return [expr {([mrd -value $VIEW] >> 8) & 0xff}]
}

puts $out [format "before CMD=0x%08x STS=0x%08x PORTSC1=0x%08x MODE=0x%08x VIEW=0x%08x" \
    [mrd -value $USBCMD] [mrd -value $USBSTS] [mrd -value $PORTSC1] [mrd -value $USBMODE] [mrd -value $VIEW]]

set vid0 [ulpi_read 0x00]
set vid1 [ulpi_read 0x01]
set pid0 [ulpi_read 0x02]
set pid1 [ulpi_read 0x03]
set func [ulpi_read 0x04]
set ifc  [ulpi_read 0x07]
set otg  [ulpi_read 0x0a]
set ist  [ulpi_read 0x13]

puts $out [format "ULPI_VID0=0x%02x VID1=0x%02x PID0=0x%02x PID1=0x%02x FUNC=0x%02x IFACE=0x%02x OTG=0x%02x INTST=0x%02x VIEW=0x%08x" \
    $vid0 $vid1 $pid0 $pid1 $func $ifc $otg $ist [mrd -value $VIEW]]
close $out
disconnect
