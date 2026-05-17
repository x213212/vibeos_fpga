connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
mwr 0x41200000 0x00000000
after 100
mwr 0x41200000 0x00000001
after 1000
set base 0x41210000
puts [format "GPIO=0x%08x" [mrd -value 0x41200000]]
puts [format "DBG_FLAGS=0x%08x" [mrd -value [expr {$base+0x04}]]]
puts [format "I_AR=%08x I_PS=%08x I0=%08x MMIO=%08x" [mrd -value [expr {$base+0x20}]] [mrd -value [expr {$base+0x24}]] [mrd -value [expr {$base+0x40}]] [mrd -value [expr {$base+0x1c}]]]
disconnect
