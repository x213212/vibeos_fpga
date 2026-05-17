connect -url tcp:127.0.0.1:3121
set out [open "H:/testproject/project_2/read_usb_ehci_structs.out" "w"]
puts $out "READ_USB_EHCI_STRUCTS_BEGIN"
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

# usb_mouse symbol is 0x851da000 in the current os.elf.
# RISC-V cached DDR 0x80000000 maps to PS physical 0x01000000.
set cpu_base  0x851da000
set phys_base 0x061da000

puts $out [format "CPU_BASE=0x%08x PHYS_BASE=0x%08x ASYNCLIST=0x%08x" $cpu_base $phys_base [mrd -value 0xE0002158]]
puts $out "ASYNC_HEAD_PHYS"
for {set off 0} {$off < 0x60} {incr off 4} {
    puts $out [format "+0x%02x 0x%08x" $off [mrd -value [expr {$phys_base + $off}]]]
}
puts $out "CTRL_QH_PHYS"
for {set off 0x080} {$off < 0x100} {incr off 4} {
    puts $out [format "+0x%03x 0x%08x" $off [mrd -value [expr {$phys_base + $off}]]]
}
puts $out "QTD0_SETUP_PHYS"
for {set off 0x180} {$off < 0x280} {incr off 4} {
    puts $out [format "+0x%03x 0x%08x" $off [mrd -value [expr {$phys_base + $off}]]]
}
puts $out "DATA_AND_STATE_PHYS"
for {set off 0x280} {$off < 0x360} {incr off 4} {
    puts $out [format "+0x%03x 0x%08x" $off [mrd -value [expr {$phys_base + $off}]]]
}

close $out
disconnect
