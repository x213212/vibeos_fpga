set out [open "H:/testproject/project_2/force_rtl8201f_mii.out" "w"]

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
    if {[wait_mdio_idle $base] != 0} { return -1 }
    wr [expr {$base + 0x0034}] [expr {0x60020000 | (($phy & 31) << 23) | (($reg & 31) << 18)}]
    if {[wait_mdio_idle $base] != 0} { return -1 }
    return [expr {[rd [expr {$base + 0x0034}]] & 0xffff}]
}

proc phy_write {base phy reg val} {
    if {[wait_mdio_idle $base] != 0} { return -1 }
    wr [expr {$base + 0x0034}] [expr {0x50020000 | (($phy & 31) << 23) | (($reg & 31) << 18) | ($val & 0xffff)}]
    if {[wait_mdio_idle $base] != 0} { return -1 }
    return 0
}

set base 0xE000B000
set phy 1

puts $out "FORCE_RTL8201F_MII_BEGIN"
phy_write $base $phy 31 7
set before [phy_read $base $phy 16]
set mii_val [expr {$before & ~0x0008}]
phy_write $base $phy 16 $mii_val
set after [phy_read $base $phy 16]
phy_write $base $phy 31 0

puts $out [format "PAGE7_R16_BEFORE=0x%04x" $before]
puts $out [format "PAGE7_R16_WRITTEN=0x%04x" $mii_val]
puts $out [format "PAGE7_R16_AFTER=0x%04x" $after]
puts $out [format "RMII_MODE_BIT3_AFTER=%d" [expr {($after >> 3) & 1}]]
puts $out [format "BMCR=0x%04x" [phy_read $base $phy 0]]
puts $out [format "BMSR=0x%04x" [phy_read $base $phy 1]]
puts $out [format "R17_PAGE0=0x%04x" [phy_read $base $phy 17]]
puts $out "FORCE_RTL8201F_MII_END"

close $out
disconnect
