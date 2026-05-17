set out [open "H:/testproject/project_2/read_usb_extra.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}
foreach {name addr} {
 ID 0xE0002000
 HWGENERAL 0xE0002004
 HWHOST 0xE0002008
 HWTXBUF 0xE0002010
 HWRXBUF 0xE0002014
 CAPLEN_HCIVER 0xE0002100
 HCSPARAMS 0xE0002104
 HCCPARAMS 0xE0002108
 TTCTRL 0xE000215C
 TXFILL 0xE0002164
 TXTTFILL 0xE0002168
 IC_USB 0xE000216C
 USBMODE 0xE00021A8
 PORTSC1 0xE0002184
} {
 if {[catch {mrd -value $addr} v]} {puts $out "$name ERR $v"} else {puts $out [format "%s=0x%08x" $name $v]}
}
close $out
disconnect
