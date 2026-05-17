connect -url tcp:127.0.0.1:3121
targets -set -filter {name =~ "APU*"}

set out [open "H:/testproject/project_2/usb0_host_kick.out" "w"]
puts $out "USB0_HOST_KICK_BEGIN"

# USB0 EHCI registers on Zynq-7000.
set USB_CMD   0xE0002140
set USB_STS   0xE0002144
set PORTSC1   0xE0002184
set USB_MODE  0xE00021A8

puts $out [format "before CMD=0x%08x STS=0x%08x PORTSC1=0x%08x MODE=0x%08x" \
    [mrd -value $USB_CMD] [mrd -value $USB_STS] [mrd -value $PORTSC1] [mrd -value $USB_MODE]]

# Controller reset, host mode, port power, run.
mwr $USB_CMD 0x00080002
after 50
mwr $USB_MODE 0x00000003
set p [mrd -value $PORTSC1]
mwr $PORTSC1 [expr {$p | 0x00001000}]
mwr $USB_CMD 0x00080001
after 500

puts $out [format "after  CMD=0x%08x STS=0x%08x PORTSC1=0x%08x MODE=0x%08x" \
    [mrd -value $USB_CMD] [mrd -value $USB_STS] [mrd -value $PORTSC1] [mrd -value $USB_MODE]]

close $out
disconnect
