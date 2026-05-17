set out [open "H:/testproject/project_2/test_gem_local_loopback.out" "w"]

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

proc rd {addr} {
    return [expr {[mrd -value $addr] & 0xffffffff}]
}

proc wr {addr val} {
    mwr $addr [expr {$val & 0xffffffff}]
}

set gem 0xE000B000
set rxbd 0x01bbb000
set rxbuf 0x01bbb100
set txbd 0x01bc7100
set txbuf 0x01bc7140

puts $out "GEM_LOCAL_LOOPBACK_BEGIN"
puts $out [format "START_NWCTRL=0x%08x" [rd [expr {$gem + 0x0000}]]]
puts $out [format "START_NWCFG=0x%08x" [rd [expr {$gem + 0x0004}]]]
puts $out [format "START_DMACR=0x%08x" [rd [expr {$gem + 0x0010}]]]
puts $out [format "START_TXSR=0x%08x" [rd [expr {$gem + 0x0014}]]]
puts $out [format "START_RXSR=0x%08x" [rd [expr {$gem + 0x0020}]]]

# Stop datapath, rebuild a tiny one-BD RX/TX ring, and enable MAC local loopback.
wr [expr {$gem + 0x0000}] 0x00000010
after 10
wr [expr {$gem + 0x0014}] 0x000001ff
wr [expr {$gem + 0x0020}] 0x0000000f
wr [expr {$gem + 0x0024}] 0xffffffff

# RX descriptor: one descriptor, wrap set, NEW clear.
wr [expr {$rxbd + 0x00}] [expr {$rxbuf | 0x2}]
wr [expr {$rxbd + 0x04}] 0x00000000
wr [expr {$gem + 0x0018}] $rxbd

# Ethernet broadcast + ARP request frame.
foreach {off val} {
    0x00 0xffffffff
    0x04 0x5602ffff
    0x08 0x01454249
    0x0c 0x01000608
    0x10 0x04060008
    0x14 0x56020100
    0x18 0x01454249
    0x1c 0x9a00a8c0
    0x20 0x00000000
    0x24 0xa8c00000
    0x28 0x00009a00
    0x2c 0x00000000
    0x30 0x00000000
    0x34 0x00000000
    0x38 0x00000000
} {
    wr [expr {$txbuf + $off}] $val
}

# TX descriptor: one descriptor, wrap + LAST + len=60, USED clear.
wr [expr {$txbd + 0x00}] $txbuf
wr [expr {$txbd + 0x04}] 0x4000803c
wr [expr {$gem + 0x001c}] $txbd

# Keep existing speed/MDC config, enable copy-all/FCS ignore for loopback receive.
wr [expr {$gem + 0x0004}] [expr {[rd [expr {$gem + 0x0004}]] | 0x04020010}]
wr [expr {$gem + 0x0010}] 0x00180710
wr [expr {$gem + 0x0000}] 0x0000021e
after 500

puts $out [format "END_NWCTRL=0x%08x" [rd [expr {$gem + 0x0000}]]]
puts $out [format "END_NWCFG=0x%08x" [rd [expr {$gem + 0x0004}]]]
puts $out [format "END_TXSR=0x%08x" [rd [expr {$gem + 0x0014}]]]
puts $out [format "END_RXSR=0x%08x" [rd [expr {$gem + 0x0020}]]]
puts $out [format "END_TXCNT=0x%08x" [rd [expr {$gem + 0x0108}]]]
puts $out [format "END_TXBCCNT=0x%08x" [rd [expr {$gem + 0x010c}]]]
puts $out [format "END_RXCNT=0x%08x" [rd [expr {$gem + 0x0158}]]]
puts $out [format "END_OCTRXL=0x%08x" [rd [expr {$gem + 0x0150}]]]
puts $out "RX_BD0"
puts $out [mrd $rxbd 2]
puts $out "TX_BD0"
puts $out [mrd $txbd 2]
puts $out "RX_BUF0"
puts $out [mrd $rxbuf 16]
puts $out "GEM_LOCAL_LOOPBACK_END"

close $out
disconnect
