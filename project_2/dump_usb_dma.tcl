connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
puts "USB_DMA_DUMP_BEGIN"
puts [mrd 0x061da000 80]
puts [mrd 0x061da180 48]
puts "USB_DMA_DUMP_END"
disconnect
