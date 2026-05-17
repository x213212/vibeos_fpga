set out [open "H:/testproject/project_2/read_boot_phase.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
set target_selected 0
if {![catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}]} {
    set target_selected 1
}
puts $out "READ_BOOT_PHASE_BEGIN"
if {!$target_selected} {
    puts $out "TARGET_SELECT_ERROR"
    puts $out [targets]
    close $out
    disconnect
    exit
}
puts $out [format "BOOT_PHASE=0x%08x" [mrd -value 0x011d4014]]
puts $out [format "BOOT_PHASE_NEED_RESCHED=0x%08x" [mrd -value 0x011d4018]]
puts $out [format "BOOT_PHASE_OS_DEBUG=0x%08x" [mrd -value 0x011d401c]]
close $out
disconnect
