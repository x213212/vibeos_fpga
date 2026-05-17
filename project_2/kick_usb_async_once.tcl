set out [open "H:/testproject/project_2/kick_usb_async_once.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

set USBCMD 0xE0002140
set USBSTS 0xE0002144
set ASYNCLIST 0xE0002158

puts $out "KICK_USB_ASYNC_ONCE_BEGIN"
puts $out [format "BEFORE_CMD=0x%08x STS=0x%08x ASYNC=0x%08x" \
    [mrd -value $USBCMD] [mrd -value $USBSTS] [mrd -value $ASYNCLIST]]

mwr $ASYNCLIST 0x061da080
mwr $USBSTS 0x0000003f
mwr $USBCMD [expr {[mrd -value $USBCMD] | 0x00000021}]
after 250

puts $out [format "AFTER_CMD=0x%08x STS=0x%08x ASYNC=0x%08x" \
    [mrd -value $USBCMD] [mrd -value $USBSTS] [mrd -value $ASYNCLIST]]
foreach {name addr} {
    CTRL_QH_EPCHAR 0x061da084
    CTRL_QH_EPCAP  0x061da088
    CTRL_QH_NEXT   0x061da090
    QTD0_TOKEN     0x061da208
    QTD1_TOKEN     0x061da248
    QTD2_TOKEN     0x061da288
    DATA0          0x061dc020
    DATA1          0x061dc024
} {
    puts $out [format "%s=0x%08x" $name [mrd -value $addr]]
}

close $out
disconnect
