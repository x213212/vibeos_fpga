connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}
set gpio_data [mrd -value 0x41200000]
set gpio_tri [mrd -value 0x41200004]
puts [format "GPIO_DATA=0x%08x" [expr {$gpio_data & 0xffffffff}]]
puts [format "GPIO_TRI=0x%08x" [expr {$gpio_tri & 0xffffffff}]]
disconnect
