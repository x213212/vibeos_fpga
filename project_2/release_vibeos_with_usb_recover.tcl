set out [open "H:/testproject/project_2/release_vibeos_with_usb_recover.out" "w"]
set bin_file "H:/testproject/vibeos/os.window_gui.bin"
set ddr_load_addr 0x01000000

if {[llength $argv] >= 1} {
    set bin_file [lindex $argv 0]
}

proc rd {addr} {
    return [expr {[mrd -value $addr] & 0xffffffff}]
}

proc show {out name addr} {
    puts $out [format "%s=0x%08x" $name [rd $addr]]
}

proc usb_ulpi_wait_idle {ulpi_view limit_ms} {
    for {set i 0} {$i < $limit_ms} {incr i} {
        set v [mrd -value $ulpi_view]
        if {($v & 0xc0000000) == 0} {
            return [list 1 $v]
        }
        after 1
    }
    return [list 0 [mrd -value $ulpi_view]]
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

set cpu_reset_gpio 0x41200000
set slcr_unlock   0xF8000008
set slcr_lock     0xF8000004
set mio46         0xF80007B8
set gpio_data1    0xE000A044
set gpio_dirm1    0xE000A244
set gpio_oen1     0xE000A248
set bit46         0x00004000

set usb_cmd       0xE0002140
set usb_sts       0xE0002144
set ctrldsseg     0xE0002150
set periodiclist  0xE0002154
set asynclist     0xE0002158
set ttctrl        0xE000215C
set burstsize     0xE0002160
set ulpi_view     0xE0002170
set configflag    0xE0002180
set portsc1       0xE0002184
set otgsc         0xE00021A4
set usb_mode      0xE00021A8
set sbuscfg       0xE0002090

puts $out "RELEASE_VIBEOS_WITH_USB_RECOVER_BEGIN"
puts $out [format "BIN_FILE=%s" $bin_file]
show $out BEFORE_USBCMD $usb_cmd
show $out BEFORE_USBSTS $usb_sts
show $out BEFORE_ULPI $ulpi_view
show $out BEFORE_PORTSC1 $portsc1
show $out BEFORE_OTGSC $otgsc
show $out BEFORE_GPIO_DATA1 $gpio_data1
show $out BEFORE_MIO46 $mio46

# Hold the PL RISC-V while PS-side USB PHY/controller state is recovered.
catch {mwr $cpu_reset_gpio 0x00000000}

mwr $usb_cmd 0x00080000
after 10
mwr $usb_cmd 0x00080002
after 50

# USB3320 reset is wired to PS MIO46 on this board.  This is intentionally
# done from XSDB/PS-side access, not from the PL RISC-V OS.
mwr -force $slcr_unlock 0x0000DF0D
mwr -force $mio46 0x00001600
mwr -force $slcr_lock 0x0000767B
mwr $gpio_dirm1 [expr {[rd $gpio_dirm1] | $bit46}]
mwr $gpio_oen1  [expr {[rd $gpio_oen1]  | $bit46}]
mwr $gpio_data1 [expr {[rd $gpio_data1] & ~$bit46}]
after 50
mwr $gpio_data1 [expr {[rd $gpio_data1] | $bit46}]
after 500

mwr $usb_cmd 0x00080002
after 100

mwr $usb_mode 0x00000003
mwr $configflag 0x00000001
mwr $sbuscfg 0x00000007
mwr $ctrldsseg 0x00000000
mwr $ttctrl 0x00000000
mwr $burstsize 0x00001010
mwr $asynclist 0x00000000
mwr $periodiclist 0x00000000
mwr $otgsc [expr {([rd $otgsc] & ~0x007f0001) | 0x00000022}]

mwr $ulpi_view 0xa0000000
set wake [usb_ulpi_wait_idle $ulpi_view 500]
puts $out [format "ULPI_WAKE_OK=%d VALUE=0x%08x" [lindex $wake 0] [lindex $wake 1]]
if {[lindex $wake 0]} {
    mwr $ulpi_view 0x600a0086
    set otg [usb_ulpi_wait_idle $ulpi_view 500]
    puts $out [format "ULPI_OTGCTRL_OK=%d VALUE=0x%08x" [lindex $otg 0] [lindex $otg 1]]
    mwr $ulpi_view 0x60040041
    set func [usb_ulpi_wait_idle $ulpi_view 500]
    puts $out [format "ULPI_FUNCCTRL_OK=%d VALUE=0x%08x" [lindex $func 0] [lindex $func 1]]
    mwr $ulpi_view 0x60070000
    set iface [usb_ulpi_wait_idle $ulpi_view 500]
    puts $out [format "ULPI_IFACECTRL_OK=%d VALUE=0x%08x" [lindex $iface 0] [lindex $iface 1]]
    mwr $ulpi_view 0x600b0060
    set drv [usb_ulpi_wait_idle $ulpi_view 500]
    puts $out [format "ULPI_DRVVBUS_OK=%d VALUE=0x%08x" [lindex $drv 0] [lindex $drv 1]]
} else {
    puts $out "ULPI_DRVVBUS_SKIPPED=1"
}

mwr $portsc1 [expr {[rd $portsc1] | 0x00001000}]
mwr $usb_sts [rd $usb_sts]
mwr $usb_cmd 0x00080001
after 100

show $out AFTER_RECOVER_USBCMD $usb_cmd
show $out AFTER_RECOVER_USBSTS $usb_sts
show $out AFTER_RECOVER_ULPI $ulpi_view
show $out AFTER_RECOVER_PORTSC1 $portsc1
show $out AFTER_RECOVER_OTGSC $otgsc
show $out AFTER_RECOVER_GPIO_DATA1 $gpio_data1
show $out AFTER_RECOVER_MIO46 $mio46

dow -data $bin_file $ddr_load_addr
set first_word [mrd -value $ddr_load_addr]
puts $out [format "DOW_BINARY_FILE=%s DDR_LOAD_ADDR=0x%08x FIRST_WORD=0x%08x" $bin_file $ddr_load_addr [expr {$first_word & 0xffffffff}]]
after 100
mwr $cpu_reset_gpio 0x00000001
after 500

show $out AFTER_RELEASE_USBCMD $usb_cmd
show $out AFTER_RELEASE_USBSTS $usb_sts
show $out AFTER_RELEASE_ULPI $ulpi_view
show $out AFTER_RELEASE_PORTSC1 $portsc1
puts $out "RELEASE_DONE"

close $out
disconnect
