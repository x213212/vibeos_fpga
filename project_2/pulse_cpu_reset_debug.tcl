connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
puts [format "BEFORE_GPIO=0x%08x FLAGS=0x%08x I_AR=0x%08x D_AW=0x%08x" \
    [mrd -value 0x41200000] [mrd -value 0x41210004] [mrd -value 0x41210008] [mrd -value 0x41210010]]
mwr 0x41200000 0x00000000
after 20
puts [format "HELD_GPIO=0x%08x FLAGS=0x%08x I_AR=0x%08x D_AW=0x%08x" \
    [mrd -value 0x41200000] [mrd -value 0x41210004] [mrd -value 0x41210008] [mrd -value 0x41210010]]
mwr 0x41200000 0x00000001
after 1000
puts [format "AFTER_GPIO=0x%08x FLAGS=0x%08x I_AR=0x%08x D_AW=0x%08x" \
    [mrd -value 0x41200000] [mrd -value 0x41210004] [mrd -value 0x41210008] [mrd -value 0x41210010]]
disconnect
