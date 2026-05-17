connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
puts [format "CPU_RESET_GPIO=0x%08x" [mrd -value 0x41200000]]
puts [format "DBG_FLAGS=0x%08x" [mrd -value 0x41210004]]
puts [format "DBG_I_AR_COUNT=0x%08x" [mrd -value 0x41210008]]
puts [format "DBG_D_AW_COUNT=0x%08x" [mrd -value 0x41210010]]
disconnect
