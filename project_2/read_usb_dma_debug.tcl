set out [open "H:/testproject/project_2/read_usb_dma_debug.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "APU*"}}
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}
puts $out "READ_USB_DMA_DEBUG_BEGIN"
set base [mrd -value 0xE0002158]
puts $out [format "ASYNC_BASE_FROM_REG=0x%08x" $base]
if {$base == 0} {
    set base 0x061da000
    puts $out [format "ASYNC_BASE_FALLBACK=0x%08x" $base]
}
foreach {name addr words} {
    ASYNC_HEAD 0x0000 32
    CTRL_QH    0x0080 32
    INTR_QH    0x0100 32
    QTD0       0x0180 16
    QTD1       0x01c0 16
    QTD2       0x0200 16
    QTD3       0x0240 16
    KBD_QH     0x0280 32
    KBD_QTD    0x0300 16
    SETUP_BUF  0x2000 8
    DATA_BUF   0x2020 16
} {
    set addr [expr {$base + $addr}]
    puts $out [format "%s @ 0x%08x" $name $addr]
    for {set i 0} {$i < $words} {incr i} {
        set a [expr {$addr + ($i * 4)}]
        if {[catch {mrd -value $a} v]} {
            puts $out [format "  +%02x READ_ERROR=%s" [expr {$i * 4}] $v]
        } else {
            puts $out [format "  +%02x 0x%08x" [expr {$i * 4}] $v]
        }
    }
}
close $out
disconnect
