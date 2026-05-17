set out [open "H:/testproject/project_2/usb_phy_hard_recover.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

set SLCR_UNLOCK 0xF8000008
set MIO8        0xF8000720
set GPIO_DATA0  0xE000A040
set GPIO_DIRM0  0xE000A204
set GPIO_OEN0   0xE000A208
set USBCMD      0xE0002140
set USBSTS      0xE0002144
set CONFIGFLAG  0xE0002180
set PORTSC1     0xE0002184
set OTGSC       0xE00021A4
set USBMODE     0xE00021A8

proc rd {addr} { return [mrd -value $addr] }
proc show {out name addr} { puts $out [format "%s=0x%08x" $name [rd $addr]] }

puts $out "USB_PHY_HARD_RECOVER_BEGIN"
show $out BEFORE_USBCMD $USBCMD
show $out BEFORE_USBSTS $USBSTS
show $out BEFORE_PORTSC1 $PORTSC1
show $out BEFORE_OTGSC $OTGSC
show $out BEFORE_USBMODE $USBMODE
show $out BEFORE_GPIO_DATA0 $GPIO_DATA0

mwr $USBCMD 0x00080000
after 50
mwr $USBCMD 0x00080002
after 100

mwr -force $SLCR_UNLOCK 0x0000DF0D
mwr -force $MIO8 0x00000601
mwr $GPIO_DIRM0 [expr {[rd $GPIO_DIRM0] | 0x00000100}]
mwr $GPIO_OEN0  [expr {[rd $GPIO_OEN0]  | 0x00000100}]
mwr $GPIO_DATA0 [expr {[rd $GPIO_DATA0] & ~0x00000100}]
after 1000
mwr $GPIO_DATA0 [expr {[rd $GPIO_DATA0] | 0x00000100}]
after 2000

mwr $USBCMD 0x00080002
after 100
mwr $USBMODE 0x00000003
mwr $CONFIGFLAG 0x00000001
mwr $PORTSC1 [expr {[rd $PORTSC1] | 0x00001000}]
mwr $USBSTS [rd $USBSTS]
mwr $USBCMD 0x00080001
after 1000

show $out AFTER_USBCMD $USBCMD
show $out AFTER_USBSTS $USBSTS
show $out AFTER_PORTSC1 $PORTSC1
show $out AFTER_OTGSC $OTGSC
show $out AFTER_USBMODE $USBMODE
show $out AFTER_GPIO_DATA0 $GPIO_DATA0
close $out
disconnect
