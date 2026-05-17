proc read32 {addr} { return [expr {[mrd -value $addr] & 0xffffffff}] }

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

puts [format "CTX0_RA=0x%08x SP=0x%08x VALID=0x%08x" \
    [read32 0x0100e8bc] [read32 0x0100e8c0] [read32 0x0100e974]]
puts [format "CTX1_RA=0x%08x SP=0x%08x VALID=0x%08x" \
    [read32 0x0100e978] [read32 0x0100e97c] [read32 0x0100ea30]]
puts [format "TASKTOP=0x%08x CTXNOW=0x%08x" \
    [read32 0x01004004] [read32 0x01004000]]
puts [format "CLINT_MTIME=0x%08x MTIMECMP=0x%08x STATUS=0x%08x" \
    [read32 0x412100a4] [read32 0x412100a8] [read32 0x412100ac]]

disconnect
