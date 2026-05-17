set out [open "H:/testproject/project_2/usb_host_recover.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "APU*"}}
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}
puts $out "USB_HOST_RECOVER_BEGIN"

proc rd {addr} {
    return [mrd -value $addr]
}
proc wr {addr val} {
    mwr $addr $val
}
proc show {out name addr} {
    if {[catch {mrd -value $addr} v]} {
        puts $out [format "%s_READ_ERROR=%s" $name $v]
    } else {
        puts $out [format "%s=0x%08x" $name $v]
    }
}

set GPIO_DATA0 0xE000A040
set GPIO_DIRM0 0xE000A204
set GPIO_OEN0  0xE000A208
set USB_USBCMD 0xE0002140
set USB_USBSTS 0xE0002144
set USB_PORTSC1 0xE0002184
set USB_USBMODE 0xE00021A8
set USB_CONFIGFLAG 0xE0002180

set dirm [rd $GPIO_DIRM0]
set oen  [rd $GPIO_OEN0]
wr $GPIO_DIRM0 [expr {$dirm | 0x100}]
wr $GPIO_OEN0  [expr {$oen  | 0x100}]

set data [rd $GPIO_DATA0]
wr $GPIO_DATA0 [expr {$data & ~0x100}]
after 100
wr $GPIO_DATA0 [expr {[rd $GPIO_DATA0] | 0x100}]
after 500

wr $USB_USBCMD 0x00080002
after 100
for {set i 0} {$i < 1000} {incr i} {
    if {([rd $USB_USBCMD] & 0x2) == 0} {break}
    after 1
}
wr $USB_USBMODE 0x00000003
wr $USB_CONFIGFLAG 0x00000001
wr $USB_PORTSC1 [expr {[rd $USB_PORTSC1] | 0x00001000}]
wr $USB_USBSTS [rd $USB_USBSTS]
wr $USB_USBCMD 0x00080001
after 1000

show $out USB_USBCMD $USB_USBCMD
show $out USB_USBSTS $USB_USBSTS
show $out USB_CONFIGFLAG $USB_CONFIGFLAG
show $out USB_PORTSC1 $USB_PORTSC1
show $out USB_USBMODE $USB_USBMODE
show $out GPIO_DATA0 $GPIO_DATA0
show $out GPIO_DIRM0 $GPIO_DIRM0
show $out GPIO_OEN0 $GPIO_OEN0
close $out
disconnect
