set out [open "H:/testproject/project_2/monitor_usb_port.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
if {[catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}]} {
    targets -set -filter {name =~ "APU*"}
}
puts $out "MONITOR_USB_PORT_BEGIN"
set last_port -1
set last_otg -1
set last_sts -1
for {set i 0} {$i < 100} {incr i} {
    set port [mrd -value 0xE0002184]
    set sts  [mrd -value 0xE0002144]
    set otg  [mrd -value 0xE00021A4]
    if {$port != $last_port || $otg != $last_otg || $sts != $last_sts || $i == 0 || $i == 99} {
        puts $out [format "t=%04dms PORTSC1=0x%08x USBSTS=0x%08x OTGSC=0x%08x" [expr {$i * 100}] $port $sts $otg]
        flush $out
        set last_port $port
        set last_otg $otg
        set last_sts $sts
    }
    after 100
}
close $out
disconnect
