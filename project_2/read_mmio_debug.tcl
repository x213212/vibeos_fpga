connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
puts [format "DBG_MMIO_AW_COUNT=0x%08x" [mrd -value 0x4121001c]]
puts [format "DBG_MMIO_AR_COUNT=0x%08x" [mrd -value 0x41210094]]
puts [format "DBG_FIFO_STATUS=0x%08x" [mrd -value 0x41210054]]
puts [format "DBG_ENQUEUE_COUNT=0x%08x" [mrd -value 0x41210058]]
puts [format "DBG_DEQUEUE_COUNT=0x%08x" [mrd -value 0x4121005c]]
puts [format "DBG_LAST_MMIO_AWADDR=0x%08x" [mrd -value 0x41210034]]
puts [format "DBG_LAST_MMIO_WDATA=0x%08x" [mrd -value 0x41210038]]
puts [format "DBG_LAST_MMIO_ARADDR=0x%08x" [mrd -value 0x41210098]]
puts [format "DBG_LAST_MMIO_RDATA=0x%08x" [mrd -value 0x4121009c]]
disconnect
