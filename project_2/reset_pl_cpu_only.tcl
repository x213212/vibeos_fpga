connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
mwr 0x41200000 0x00000000
after 200
mwr 0x41200000 0x00000001
after 3000
disconnect
