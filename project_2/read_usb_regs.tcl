set out [open "H:/testproject/project_2/read_usb_regs.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}
foreach {name addr} {
    USBCMD     0xE0002140
    USBSTS     0xE0002144
    FRINDEX    0xE000214C
    CTRLDSSEG  0xE0002150
    PERIODIC   0xE0002154
    ASYNC      0xE0002158
    TTCTRL     0xE000215C
    BURSTSIZE  0xE0002160
    TXFILL     0xE0002164
    ULPI       0xE0002170
    CONFIGFLAG 0xE0002180
    PORTSC1    0xE0002184
    OTGSC      0xE00021A4
    USBMODE    0xE00021A8
    SBUSCFG    0xE0002090
} {
    if {[catch {mrd -value $addr} v]} {
        puts $out [format "%s_READ_ERROR=%s" $name $v]
    } else {
        puts $out [format "%s=0x%08x" $name $v]
    }
}
close $out
catch {con}
disconnect
