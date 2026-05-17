connect -url tcp:127.0.0.1:3121
set out [open "H:/testproject/project_2/force_mio8_usb_reset_high.out" "w"]
puts $out "FORCE_MIO8_USB_RESET_HIGH_BEGIN"
targets
targets -set -filter {name =~ "APU*"}

set GPIO_BASE 0xE000A000
set GPIO_DATA0 [expr {$GPIO_BASE + 0x40}]
set GPIO_DIRM0 [expr {$GPIO_BASE + 0x204}]
set GPIO_OEN0  [expr {$GPIO_BASE + 0x208}]

set dirm [mrd -value $GPIO_DIRM0]
set oen  [mrd -value $GPIO_OEN0]
set data [mrd -value $GPIO_DATA0]
puts $out [format "before DATA0=0x%08x DIRM0=0x%08x OEN0=0x%08x MIO8=0x%08x" $data $dirm $oen [mrd -value 0xF8000720]]

mwr $GPIO_DIRM0 [expr {$dirm | (1 << 8)}]
mwr $GPIO_OEN0  [expr {$oen  | (1 << 8)}]
mwr $GPIO_DATA0 [expr {$data | (1 << 8)}]

after 50

set dirm [mrd -value $GPIO_DIRM0]
set oen  [mrd -value $GPIO_OEN0]
set data [mrd -value $GPIO_DATA0]
puts $out [format "after  DATA0=0x%08x DIRM0=0x%08x OEN0=0x%08x MIO8=0x%08x" $data $dirm $oen [mrd -value 0xF8000720]]

close $out
disconnect
