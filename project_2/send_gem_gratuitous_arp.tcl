set out [open "H:/testproject/project_2/send_gem_gratuitous_arp.out" "w"]

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
set txbd 0x01bc7100
set txbuf 0x01bc7140

puts $out "SEND_GEM_GRATUITOUS_ARP_BEGIN"
puts $out [format "BEFORE_NWCTRL=0x%08x" [rd [expr {$gem + 0x0000}]]]
puts $out [format "BEFORE_NWCFG=0x%08x" [rd [expr {$gem + 0x0004}]]]
puts $out [format "BEFORE_DMACR=0x%08x" [rd [expr {$gem + 0x0010}]]]
puts $out [format "BEFORE_TXSR=0x%08x" [rd [expr {$gem + 0x0014}]]]
puts $out [format "BEFORE_TXCNT=0x%08x" [rd [expr {$gem + 0x0108}]]]
puts $out [format "BEFORE_TXBCCNT=0x%08x" [rd [expr {$gem + 0x010c}]]]

# Ethernet broadcast + ARP request announcing 192.168.0.154 from
# 02:56:49:42:45:01. Words are little-endian byte order in DDR.
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

# Reuse TX descriptor 0: addr points at frame, status length=60 + LAST.
wr [expr {$txbd + 0x00}] $txbuf
wr [expr {$txbd + 0x04}] 0x0000803c
wr [expr {$gem + 0x0014}] 0x000001ff
wr [expr {$gem + 0x0000}] [expr {[rd [expr {$gem + 0x0000}]] | 0x00000200}]

after 200

puts $out [format "AFTER_NWCTRL=0x%08x" [rd [expr {$gem + 0x0000}]]]
puts $out [format "AFTER_TXSR=0x%08x" [rd [expr {$gem + 0x0014}]]]
puts $out [format "AFTER_TXCNT=0x%08x" [rd [expr {$gem + 0x0108}]]]
puts $out [format "AFTER_TXBCCNT=0x%08x" [rd [expr {$gem + 0x010c}]]]
puts $out "TX_BD0"
puts $out [mrd $txbd 2]
puts $out "TX_BUF0"
puts $out [mrd $txbuf 16]
puts $out "SEND_GEM_GRATUITOUS_ARP_END"

close $out
disconnect
