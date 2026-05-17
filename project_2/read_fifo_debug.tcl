set dbg_base 0x41210000

proc read_dbg {name offset} {
    global dbg_base
    set value [mrd -value [expr {$dbg_base + $offset}]]
    puts [format "%s=0x%08x" $name [expr {$value & 0xffffffff}]]
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
jtag targets -set -filter {level == 0}
jtag targets -open -filter {level == 0}
after 1000
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

read_dbg DBG_FIFO_STATUS 0x54
read_dbg DBG_FIFO_ENQUEUE_COUNT 0x58
read_dbg DBG_FIFO_DEQUEUE_COUNT 0x5c
read_dbg DBG_FIFO_LAST_BYTES 0x60
read_dbg DBG_JTAG_STATE 0x64

disconnect
