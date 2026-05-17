connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
set aper [mrd -value 0xF800012C]
set gem0 [mrd -value 0xF8000140]
puts [format "APER=0x%08x GEM0CLK=0x%08x" $aper $gem0]
disconnect
