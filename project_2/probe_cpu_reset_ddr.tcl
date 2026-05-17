set out [open "H:/testproject/project_2/probe_cpu_reset_ddr.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
puts $out [format "CPU_RESET_GPIO=0x%08x" [mrd -value 0x41200000]]
puts $out [format "DDR_FIRST=0x%08x" [mrd -value 0x00000000]]
puts $out [format "PORTSC1=0x%08x" [mrd -value 0xE0002184]]
close $out
disconnect
