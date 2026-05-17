set out [open "H:/testproject/project_2/test_pl_fetch_visibility.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1

set cpu_reset_gpio 0x41200000
set dbg_base 0x41210000

targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}

mwr $cpu_reset_gpio 0x00000000
after 50

mwr 0x00000000 0x11111111
mwr 0x00000004 0x22222222
mwr 0x00000008 0x33333333
mwr 0x0000000c 0x44444444
mwr 0x00000010 0x55555555
mwr 0x00000014 0x66666666
mwr 0x00000018 0x77777777
mwr 0x0000001c 0x88888888

puts $out [format "ARM_00000000=0x%08x" [mrd -value 0x00000000]]
puts $out [format "ARM_00000004=0x%08x" [mrd -value 0x00000004]]
puts $out [format "ARM_00000008=0x%08x" [mrd -value 0x00000008]]
puts $out [format "ARM_0000000c=0x%08x" [mrd -value 0x0000000c]]

mwr $cpu_reset_gpio 0x00000001
after 100

puts $out [format "DBG_FLAGS=0x%08x" [mrd -value [expr {$dbg_base + 0x04}]]]
puts $out [format "DBG_I_AR_COUNT=0x%08x" [mrd -value [expr {$dbg_base + 0x08}]]]
puts $out [format "DBG_I_R_COUNT=0x%08x" [mrd -value [expr {$dbg_base + 0x0c}]]]
puts $out [format "DBG_LAST_I_ARADDR=0x%08x" [mrd -value [expr {$dbg_base + 0x20}]]]
puts $out [format "DBG_LAST_I_PS_ARADDR=0x%08x" [mrd -value [expr {$dbg_base + 0x24}]]]
puts $out [format "DBG_LAST_I_RDATA=0x%08x" [mrd -value [expr {$dbg_base + 0x28}]]]
puts $out [format "DBG_I_RDATA0=0x%08x" [mrd -value [expr {$dbg_base + 0x40}]]]
puts $out [format "DBG_I_RDATA1=0x%08x" [mrd -value [expr {$dbg_base + 0x44}]]]
puts $out [format "DBG_I_RDATA2=0x%08x" [mrd -value [expr {$dbg_base + 0x48}]]]
puts $out [format "DBG_I_RDATA3=0x%08x" [mrd -value [expr {$dbg_base + 0x4c}]]]
puts $out [format "DBG_RESP_OR=0x%08x" [mrd -value [expr {$dbg_base + 0x50}]]]

close $out
disconnect
