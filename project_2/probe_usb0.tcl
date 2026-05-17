connect -url tcp:127.0.0.1:3121
set out [open "H:/testproject/project_2/probe_usb0.out" "w"]
puts $out "PROBE_USB0_BEGIN"
targets
targets -set -filter {name =~ "APU*"}

set cap     [mrd -value 0xE0002100]
set usbcmd  [mrd -value 0xE0002140]
set usbsts  [mrd -value 0xE0002144]
set portsc1 [mrd -value 0xE0002184]

puts $out [format "USB0_CAP=0x%08x USB0_USBCMD=0x%08x USB0_USBSTS=0x%08x USB0_PORTSC1=0x%08x" $cap $usbcmd $usbsts $portsc1]
close $out
disconnect
