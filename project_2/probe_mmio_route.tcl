connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

set base 0x41210000
foreach {name off} {
    DBG_MMIO_AW_COUNT 0x1c
    DBG_MMIO_AR_COUNT 0x94
    DBG_MMIO_ARADDR   0x98
    DBG_MMIO_RDATA    0x9c
    DBG_FIFO_STATUS   0x54
    DBG_UART_COUNTS   0x74
    DBG_LAST_UART_ADDR 0x78
    DBG_LAST_WSTRB_DATA 0x7c
    DBG_MOUSE_STATE   0xb0
    DBG_MOUSE0        0xb4
    DBG_MOUSE7        0xd0
} {
    set v [mrd -value [expr {$base + $off}]]
    puts [format "%s=0x%08x" $name [expr {$v & 0xffffffff}]]
}
disconnect
