set out [open "H:/testproject/project_2/probe_ulpi_id.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

set USBCMD      0xE0002140
set USBSTS      0xE0002144
set ULPI_VIEW   0xE0002170
set CONFIGFLAG  0xE0002180
set PORTSC1     0xE0002184
set OTGSC       0xE00021A4
set USBMODE     0xE00021A8

proc wait_clear {addr mask limit} {
    for {set i 0} {$i < $limit} {incr i} {
        set v [mrd -value $addr]
        if {($v & $mask) == 0} {
            return [list 1 $v]
        }
        after 1
    }
    return [list 0 [mrd -value $addr]]
}

proc z7lite_usb_phy_reset_release {out} {
    set slcr_unlock 0xF8000008
    set slcr_lock   0xF8000004
    set mio46       0xF80007B8
    set gpio_data1  0xE000A044
    set gpio_dirm1  0xE000A244
    set gpio_oen1   0xE000A248
    set bit         0x00004000

    mwr -force $slcr_unlock 0x0000DF0D
    mwr -force $mio46 0x00001600
    mwr -force $slcr_lock 0x0000767B

    mwr $gpio_data1 [expr {[mrd -value $gpio_data1] & ~$bit}]
    mwr $gpio_dirm1 [expr {[mrd -value $gpio_dirm1] | $bit}]
    mwr $gpio_oen1  [expr {[mrd -value $gpio_oen1] | $bit}]
    after 10
    mwr $gpio_data1 [expr {[mrd -value $gpio_data1] | $bit}]
    after 100
    puts $out [format "USB_PHY_RESET_RELEASE MIO46=0x%08x GPIO_DATA1=0x%08x DIRM1=0x%08x OEN1=0x%08x" \
        [mrd -value $mio46] [mrd -value $gpio_data1] [mrd -value $gpio_dirm1] [mrd -value $gpio_oen1]]
}

proc ulpi_wake {view} {
    mwr $view 0xa0000000
    return [wait_clear $view 0x80000000 200]
}

proc ulpi_read {view reg} {
    set w [ulpi_wake $view]
    if {![lindex $w 0]} {
        return [list 0 [lindex $w 1]]
    }
    mwr $view [expr {0x40000000 | (($reg & 0xff) << 16)}]
    set r [wait_clear $view 0x40000000 200]
    if {![lindex $r 0]} {
        return [list 0 [lindex $r 1]]
    }
    set v [lindex $r 1]
    return [list 1 [expr {($v >> 8) & 0xff}] $v]
}

proc ulpi_write {view reg val} {
    set w [ulpi_wake $view]
    if {![lindex $w 0]} {
        return [list 0 [lindex $w 1]]
    }
    mwr $view [expr {0x60000000 | (($reg & 0xff) << 16) | ($val & 0xff)}]
    return [wait_clear $view 0x40000000 200]
}

puts $out "PROBE_ULPI_ID_BEGIN"
puts $out [format "BEFORE_CMD=0x%08x STS=0x%08x PORT=0x%08x MODE=0x%08x OTG=0x%08x VIEW=0x%08x" \
    [mrd -value $USBCMD] [mrd -value $USBSTS] [mrd -value $PORTSC1] [mrd -value $USBMODE] [mrd -value $OTGSC] [mrd -value $ULPI_VIEW]]

z7lite_usb_phy_reset_release $out
mwr $USBCMD 0x00080002
after 50
mwr $USBMODE 0x00000003
mwr $CONFIGFLAG 0x00000001
mwr $OTGSC [expr {([mrd -value $OTGSC] & ~0x007f0001) | 0x00000022}]
set wr [ulpi_write $ULPI_VIEW 0x0b 0x60]
mwr $PORTSC1 [expr {[mrd -value $PORTSC1] | 0x00001000}]
mwr $USBSTS [mrd -value $USBSTS]
mwr $USBCMD 0x00080001
after 500

puts $out [format "DRVVBUS_WRITE_OK=%d VIEW=0x%08x" [lindex $wr 0] [lindex $wr 1]]
foreach {name reg} {
    VID_LOW  0x00
    VID_HIGH 0x01
    PID_LOW  0x02
    PID_HIGH 0x03
    FUNC_CTL 0x04
    IFACE_CTL 0x07
    OTG_CTL 0x0a
} {
    set rr [ulpi_read $ULPI_VIEW $reg]
    if {[lindex $rr 0]} {
        puts $out [format "%s=0x%02x RAW=0x%08x" $name [lindex $rr 1] [lindex $rr 2]]
    } else {
        puts $out [format "%s_READ_TIMEOUT VIEW=0x%08x" $name [lindex $rr 1]]
    }
}
puts $out [format "AFTER_CMD=0x%08x STS=0x%08x PORT=0x%08x MODE=0x%08x OTG=0x%08x VIEW=0x%08x" \
    [mrd -value $USBCMD] [mrd -value $USBSTS] [mrd -value $PORTSC1] [mrd -value $USBMODE] [mrd -value $OTGSC] [mrd -value $ULPI_VIEW]]
close $out
disconnect
