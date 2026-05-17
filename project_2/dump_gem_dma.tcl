set out [open "H:/testproject/project_2/dump_gem_dma.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

proc rd {addr} {
    return [expr {[mrd -value $addr] & 0xffffffff}]
}

puts $out "GEM_DMA_DUMP_BEGIN"
foreach {name addr} {
    NWCTRL   0xE000B000
    NWCFG    0xE000B004
    NWSR     0xE000B008
    DMACR    0xE000B010
    TXSR     0xE000B014
    RXQBASE  0xE000B018
    TXQBASE  0xE000B01C
    RXSR     0xE000B020
    ISR      0xE000B024
    IMR      0xE000B030
    PHYMNTNC 0xE000B034
    LADDR1L  0xE000B088
    LADDR1H  0xE000B08C
} {
    if {[catch {rd $addr} v]} {
        puts $out [format "%s_READ_ERROR=%s" $name $v]
    } else {
        puts $out [format "%s=0x%08x" $name $v]
    }
}

puts $out "RX_BD_FIRST_16"
puts $out [mrd 0x01bbb000 32]
puts $out "TX_BD_FIRST_8"
puts $out [mrd 0x01bc7100 16]
puts $out "RX_BUF0_HEAD"
puts $out [mrd 0x01bbb100 32]
puts $out "TX_BUF0_HEAD"
puts $out [mrd 0x01bc7140 32]
puts $out "GEM_DMA_DUMP_END"

close $out
disconnect
