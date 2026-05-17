set out_file "H:/testproject/project_2/probe_gem0_mdio.out"
set out [open $out_file w]

proc log {msg} {
    global out
    puts $out $msg
    puts $msg
}

proc rd32 {addr} {
    return [mrd -value $addr]
}

proc wr32 {addr val} {
    mwr $addr $val
}

proc wait_mdio_idle {base limit} {
    set nwsr [expr {$base + 0x0008}]
    for {set i 0} {$i < $limit} {incr i} {
        set v [rd32 $nwsr]
        if {($v & 0x00000004) != 0} {
            return [list 1 $v $i]
        }
    }
    return [list 0 [rd32 $nwsr] $limit]
}

proc phy_read {base phy reg} {
    set phymntnc [expr {$base + 0x0034}]
    set idle0 [wait_mdio_idle $base 200000]
    if {[lindex $idle0 0] == 0} {
        return [list -1 [lindex $idle0 1]]
    }
    set cmd [expr {0x40020000 | 0x20000000 | (($phy & 31) << 23) | (($reg & 31) << 18)}]
    wr32 $phymntnc $cmd
    set idle1 [wait_mdio_idle $base 200000]
    if {[lindex $idle1 0] == 0} {
        return [list -2 [rd32 $phymntnc]]
    }
    set raw [rd32 $phymntnc]
    return [list 0 $raw]
}

proc enable_gem0_aper_clock {} {
    set slcr_unlock 0xF8000008
    set slcr_lock   0xF8000004
    set gem0_clk    0xF8000140
    set aper_clk    0xF800012C

    wr32 $slcr_unlock 0x0000DF0D
    wr32 $gem0_clk [expr {([rd32 $gem0_clk] & ~0x03F03F71) | 0x00100141}]
    wr32 $aper_clk [expr {([rd32 $aper_clk] & ~0x01FFCCCD) | 0x01DC044D}]
    wr32 $slcr_lock 0x0000767B
}

connect
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}
enable_gem0_aper_clock

set base 0xE000B000
set nwctrl [expr {$base + 0x0000}]
set nwcfg  [expr {$base + 0x0004}]
set nwsr   [expr {$base + 0x0008}]
set phym   [expr {$base + 0x0034}]

log "GEM0_MDIO_PROBE_BEGIN"
log [format "GEM0_BEFORE NWCTRL=0x%08x NWCFG=0x%08x NWSR=0x%08x PHYMNTNC=0x%08x" \
    [rd32 $nwctrl] [rd32 $nwcfg] [rd32 $nwsr] [rd32 $phym]]

set cfg [rd32 $nwcfg]
set cfg [expr {$cfg & ~0x001c0000}]
set cfg [expr {$cfg | (2 << 18) | 0x00000100 | 0x00000002 | 0x00000001}]
wr32 $nwcfg $cfg
wr32 $nwctrl [expr {[rd32 $nwctrl] | 0x00000010}]
after 10

log [format "GEM0_AFTER  NWCTRL=0x%08x NWCFG=0x%08x NWSR=0x%08x PHYMNTNC=0x%08x" \
    [rd32 $nwctrl] [rd32 $nwcfg] [rd32 $nwsr] [rd32 $phym]]

foreach phy {0 1 2 3 7 31} {
    set r0 [phy_read $base $phy 0]
    set r1a [phy_read $base $phy 1]
    set r1b [phy_read $base $phy 1]
    set r2 [phy_read $base $phy 2]
    set r3 [phy_read $base $phy 3]
    log [format "PHY%02d BMCR=%s BMSR1=%s BMSR2=%s ID1=%s ID2=%s" \
        $phy \
        [format "rc=%d raw=0x%08x" [lindex $r0 0] [lindex $r0 1]] \
        [format "rc=%d raw=0x%08x" [lindex $r1a 0] [lindex $r1a 1]] \
        [format "rc=%d raw=0x%08x" [lindex $r1b 0] [lindex $r1b 1]] \
        [format "rc=%d raw=0x%08x" [lindex $r2 0] [lindex $r2 1]] \
        [format "rc=%d raw=0x%08x" [lindex $r3 0] [lindex $r3 1]]]
}

log "GEM0_MDIO_PROBE_END"
close $out
