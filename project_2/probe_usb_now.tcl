set out [open "H:/testproject/project_2/probe_usb_now.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}
foreach {name addr} {
USBCMD 0xE0002140
USBSTS 0xE0002144
FRINDEX 0xE000214C
ULPI_VIEW 0xE0002170
CONFIGFLAG 0xE0002180
PORTSC1 0xE0002184
OTGSC 0xE00021A4
USBMODE 0xE00021A8
GPIO_DATA1 0xE000A044
GPIO_DIRM1 0xE000A244
GPIO_OEN1 0xE000A248
MIO46 0xF80007B8
} {
    if {[catch {mrd -value $addr} v]} {puts $out "$name=$v"} else {puts $out [format "%s=0x%08x" $name $v]}
}
close $out
disconnect
