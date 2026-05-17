set out [open "H:/testproject/project_2/read_pl_stack_words.out" "w"]

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

foreach a {
    0x0100ffc0 0x0100ffc4 0x0100ffc8 0x0100ffcc
    0x0100ffd0 0x0100ffd4 0x0100ffd8 0x0100ffdc
    0x0100ffe0 0x0100ffe4 0x0100ffe8 0x0100ffec
    0x0100fff0 0x0100fff4 0x0100fff8 0x0100fffc
    0x011d4000 0x011d4004 0x011d4008 0x011d400c
    0x011d4010 0x011d4014 0x011d4018 0x011d401c
} {
    if {[catch {mrd -value $a} v]} {
        puts $out [format "0x%08x ERR %s" $a $v]
    } else {
        puts $out [format "0x%08x=0x%08x" $a [expr {$v & 0xffffffff}]]
    }
}

close $out
disconnect
