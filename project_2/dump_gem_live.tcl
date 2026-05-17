set out [open "H:/testproject/project_2/dump_gem_live.out" "w"]

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

proc rd {addr} {
    return [expr {[mrd -value $addr] & 0xffffffff}]
}

proc wr {addr val} {
    mwr $addr [expr {$val & 0xffffffff}]
}

proc wait_mdio_idle {base} {
    set nwsr [expr {$base + 0x0008}]
    for {set i 0} {$i < 200000} {incr i} {
        set v [rd $nwsr]
        if {($v & 0x00000004) != 0} { return 0 }
    }
    return -1
}

proc phy_read {base phy reg} {
    if {[wait_mdio_idle $base] != 0} { return "idle_timeout_before" }
    wr [expr {$base + 0x0034}] [expr {0x60020000 | (($phy & 31) << 23) | (($reg & 31) << 18)}]
    if {[wait_mdio_idle $base] != 0} { return "idle_timeout_after" }
    return [format "0x%04x" [expr {[rd [expr {$base + 0x0034}]] & 0xffff}]]
}

set base 0xE000B000

puts $out "GEM_LIVE_DUMP_BEGIN"
foreach {name off} {
    NWCTRL 0x0000
    NWCFG 0x0004
    NWSR 0x0008
    DMACR 0x0010
    TXSR 0x0014
    RXQBASE 0x0018
    TXQBASE 0x001c
    RXSR 0x0020
    ISR 0x0024
    IMR 0x0030
    PHYMNTNC 0x0034
    LADDR1L 0x0088
    LADDR1H 0x008c
    TXCNT 0x0108
    TXBCCNT 0x010c
    TXMCCNT 0x0110
    OCTRXL 0x0150
    RXCNT 0x0158
    RXBROADCNT 0x015c
    RXMULTICNT 0x0160
    RXFCSCNT 0x0190
    RXLENGTHCNT 0x0194
    RXSYMBCNT 0x0198
    RXALIGNCNT 0x019c
    RXRESERRCNT 0x01a0
    RXORCNT 0x01a4
} {
    puts $out [format "%s=0x%08x" $name [rd [expr {$base + $off}]]]
}

puts $out "PHY_REGS"
for {set phy 0} {$phy < 32} {incr phy} {
    set bmsr [phy_read $base $phy 1]
    set id1 [phy_read $base $phy 2]
    set id2 [phy_read $base $phy 3]
    if {$id1 ne "0xffff" || $id2 ne "0xffff"} {
        set bmcr [phy_read $base $phy 0]
        set anar [phy_read $base $phy 4]
        set anlpar [phy_read $base $phy 5]
        set phy17 [phy_read $base $phy 17]
        set phy31 [phy_read $base $phy 31]
        puts $out [format "PHY%02d BMCR=%s BMSR=%s ID1=%s ID2=%s ANAR=%s ANLPAR=%s R17=%s R31=%s" \
            $phy $bmcr $bmsr $id1 $id2 $anar $anlpar $phy17 $phy31]
    }
}

puts $out "RX_BD_FIRST_8"
puts $out [mrd 0x01bbb000 16]
puts $out "TX_BD_FIRST_4"
puts $out [mrd 0x01bc7100 8]
puts $out "RX_BUF0_HEAD"
puts $out [mrd 0x01bbb100 16]
puts $out "TX_BUF0_HEAD"
puts $out [mrd 0x01bc7140 16]
puts $out "GEM_LIVE_DUMP_END"

close $out
disconnect
