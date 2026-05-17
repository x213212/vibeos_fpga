set out [open "H:/testproject/project_2/dump_usb_ehci_live.out" "w"]

proc dump_words {out name base count} {
    puts $out [format "%s_BASE=0x%08x" $name $base]
    for {set i 0} {$i < $count} {incr i} {
        set addr [expr {$base + ($i * 4)}]
        if {[catch {mrd -value $addr} v]} {
            puts $out [format "%s_%02d_READ_ERROR=%s" $name $i $v]
        } else {
            puts $out [format "%s_%02d[0x%08x]=0x%08x" $name $i $addr $v]
        }
    }
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

puts $out "DUMP_USB_EHCI_LIVE_BEGIN"

set usb_base 0xe0002000
foreach {name off} {
    SBUSCFG 0x090
    USBCMD 0x140
    USBSTS 0x144
    FRINDEX 0x14c
    CTRLDSSEG 0x150
    PERIODICLIST 0x154
    ASYNCLIST 0x158
    TTCTRL 0x15c
    BURSTSIZE 0x160
    CONFIGFLAG 0x180
    PORTSC1 0x184
    OTGSC 0x1a4
    USBMODE 0x1a8
} {
    set addr [expr {$usb_base + $off}]
    if {[catch {mrd -value $addr} v]} {
        puts $out [format "%s_READ_ERROR=%s" $name $v]
    } else {
        puts $out [format "%s=0x%08x" $name $v]
    }
}

# VibeOS pins the USB EHCI DMA state here so XSDB and the driver inspect
# the exact same DDR-backed descriptors.
set mouse_phys 0x061da000
set qh_size 128
set qtd_size 64
set qtd_base [expr {$mouse_phys + ($qh_size * 3)}]

dump_words $out ASYNC_HEAD $mouse_phys 24
dump_words $out CTRL_QH [expr {$mouse_phys + $qh_size}] 24
dump_words $out INTR_QH [expr {$mouse_phys + ($qh_size * 2)}] 24
dump_words $out QTD0_SETUP $qtd_base 13
dump_words $out QTD1_DATA [expr {$qtd_base + $qtd_size}] 13
dump_words $out QTD2_STATUS [expr {$qtd_base + ($qtd_size * 2)}] 13
dump_words $out QTD3_INTR [expr {$qtd_base + ($qtd_size * 3)}] 13
dump_words $out SETUP_PKT [expr {$mouse_phys + 0x2000}] 4
dump_words $out DATA_BUF [expr {$mouse_phys + 0x2020}] 10
dump_words $out REPORT_BUF [expr {$mouse_phys + 0x2120}] 4

close $out
disconnect
