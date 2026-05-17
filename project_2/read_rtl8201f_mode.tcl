set out [open "H:/testproject/project_2/read_rtl8201f_mode.out" "w"]

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

puts $out "RTL8201F_MODE_BEGIN"
puts $out [format "PHY%02d_BMCR=0x%04x" $phy [phy_read $base $phy 0]]
puts $out [format "PHY%02d_BMSR=0x%04x" $phy [phy_read $base $phy 1]]
puts $out [format "PHY%02d_ID1=0x%04x" $phy [phy_read $base $phy 2]]
puts $out [format "PHY%02d_ID2=0x%04x" $phy [phy_read $base $phy 3]]
puts $out [format "PHY%02d_R17_PAGE0=0x%04x" $phy [phy_read $base $phy 17]]
puts $out [format "PHY%02d_R31_BEFORE=0x%04x" $phy [phy_read $base $phy 31]]

phy_write $base $phy 31 7
set rmsr [phy_read $base $phy 16]
puts $out [format "PHY%02d_PAGE7_R16_RMSR=0x%04x" $phy $rmsr]
puts $out [format "PHY%02d_PAGE7_R16_RMII_MODE_BIT3=%d" $phy [expr {($rmsr >> 3) & 1}]]
puts $out [format "PHY%02d_PAGE7_R16_CLKDIR_BIT12=%d" $phy [expr {($rmsr >> 12) & 1}]]
phy_write $base $phy 31 0
puts $out [format "PHY%02d_R31_AFTER=0x%04x" $phy [phy_read $base $phy 31]]
puts $out "RTL8201F_MODE_END"

close $out
disconnect
