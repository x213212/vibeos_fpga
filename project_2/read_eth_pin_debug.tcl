set out [open "H:/testproject/project_2/read_eth_pin_debug.out" "w"]

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

proc rd {addr} {
    return [expr {[mrd -value $addr] & 0xffffffff}]
}

proc dump_once {out tag} {
    set base 0x41210000
    puts $out $tag
    foreach {name off} {
        DBG_MAGIC       0x000
        ETH_TXCLK_EDGES 0x100
        ETH_RXCLK_EDGES 0x104
        ETH_RXDV_EDGES  0x108
        ETH_TXEN_EDGES  0x10c
        ETH_RX_ACTIVE   0x110
        ETH_TX_ACTIVE   0x114
        ETH_PIN_SAMPLE  0x118
        ETH_TXCLK_DOMAIN 0x11c
        ETH_RXCLK_DOMAIN 0x120
    } {
        puts $out [format "%s=0x%08x" $name [rd [expr {$base + $off}]]]
    }
}

puts $out "READ_ETH_PIN_DEBUG_BEGIN"
dump_once $out "SAMPLE0"
after 200
dump_once $out "SAMPLE1"
puts $out "READ_ETH_PIN_DEBUG_END"

close $out
disconnect
