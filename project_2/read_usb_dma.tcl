set out [open "H:/testproject/project_2/read_usb_dma.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
set target_selected 0
if {![catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}]} {
    set target_selected 1
}
if {!$target_selected && ![catch {targets -set -filter {name =~ "APU*"}}]} {
    set target_selected 1
}
foreach {name addr count} {
    PHYS_ASYNC_HEAD 0x061da000 32
    PHYS_CTRL_QH    0x061da080 32
    PHYS_INTR_QH    0x061da100 32
    PHYS_QTD0       0x061da180 16
    PHYS_QTD1       0x061da1c0 16
    PHYS_QTD2       0x061da200 16
    PHYS_QTD3       0x061da240 16
    PHYS_KBD_QH     0x061da280 32
    PHYS_KBD_QTD    0x061da300 16
    PHYS_SETUP      0x061dc000 4
    PHYS_DATA       0x061dc020 8
    PHYS_KBD_REPORT 0x061dc140 8
} {
    puts $out $name
    if {[catch {mrd $addr $count} v]} {
        puts $out "READ_ERROR=$v"
    } else {
        puts $out $v
    }
}
close $out
catch {con}
disconnect
