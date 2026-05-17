set out [open "H:/testproject/project_2/probe_window_gui_ddr.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
foreach addr {0x01000000 0x01000004 0x01000008 0x0100000c 0x01001000 0x01100000} {
    puts $out [format "DDR_%08x=0x%08x" $addr [mrd -value $addr]]
}
puts $out [format "CPU_RESET_GPIO=0x%08x" [mrd -value 0x41200000]]
puts $out [format "PORTSC1=0x%08x" [mrd -value 0xE0002184]]
close $out
disconnect
