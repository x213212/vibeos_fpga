set out [open "H:/testproject/project_2/read_boot_phase_current.out" "w"]

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

proc show {out name addr} {
    if {[catch {mrd -value $addr} v]} {
        puts $out [format "%s_READ_ERROR=%s" $name $v]
    } else {
        puts $out [format "%s=0x%08x" $name [expr {$v & 0xffffffff}]]
    }
}

puts $out "READ_BOOT_PHASE_CURRENT_BEGIN"
show $out BOOT_PHASE_PHYS 0x011c4014
show $out OS_DEBUG_PHYS 0x011c401c
show $out STACK_OS_MAIN_RA 0x011d43dc
show $out STACK_OS_MAIN_TOP 0x011d43e0
show $out STACK_OS_MAIN_RA_SLOT 0x011d43fc
show $out RESET_GPIO 0x41200000
show $out DDR_OS_MAIN_00 0x010029f8
show $out DDR_OS_MAIN_04 0x010029fc
show $out DDR_OS_MAIN_18 0x01002a10
show $out DDR_OS_MAIN_20 0x01002a18
show $out DDR_OS_START_00 0x01002438

close $out
catch {con}
disconnect
