set out [open "H:/testproject/project_2/read_pl_irq_probe.out" "w"]
set dbg_base 0x41210000

proc rd_dbg {idx} {
    global dbg_base
    return [expr {[mrd -value [expr {$dbg_base + ($idx * 4)}]] & 0xffffffff}]
}

proc wr_dbg {idx value} {
    global dbg_base
    mwr [expr {$dbg_base + ($idx * 4)}] $value
}

proc dump_irq_sample {out tag} {
    set txclk [rd_dbg 0x40]
    set rxclk [rd_dbg 0x41]
    set rxdv  [rd_dbg 0x42]
    set rxact [rd_dbg 0x44]
    set pins  [rd_dbg 0x46]
    set plic  [rd_dbg 0x49]
    set claim [rd_dbg 0x4a]
    puts $out [format "%s TXCLK=0x%08x RXCLK=0x%08x RXDV_EDGE=0x%08x RXACT=0x%08x PINS=0x%08x PLIC=0x%08x CLAIM=0x%08x" \
        $tag $txclk $rxclk $rxdv $rxact $pins $plic $claim]
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

puts $out "PL_IRQ_PROBE_BEGIN"
dump_irq_sample $out "BEFORE_CLEAR"

# Debug write bit 2 to register 0x49 clears the PL net IRQ pending latch.
wr_dbg 0x49 0x00000004
after 20
dump_irq_sample $out "AFTER_CLEAR"

for {set i 0} {$i < 30} {incr i} {
    after 100
    dump_irq_sample $out [format "SAMPLE_%02d" $i]
}

puts $out "PL_IRQ_PROBE_END"
close $out
disconnect
