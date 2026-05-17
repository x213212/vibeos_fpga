connect -url tcp:127.0.0.1:3121
set out [open "H:/testproject/project_2/probe_gpio_mio8.out" "w"]
puts $out "PROBE_GPIO_MIO8_BEGIN"
targets
targets -set -filter {name =~ "APU*"}
set GPIO_BASE 0xE000A000
set data [mrd -value [expr {$GPIO_BASE + 0x40}]]
set dirm [mrd -value [expr {$GPIO_BASE + 0x204}]]
set oen  [mrd -value [expr {$GPIO_BASE + 0x208}]]
puts $out [format "DATA0=0x%08x DIRM0=0x%08x OEN0=0x%08x MIO8_OUT=%d" $data $dirm $oen [expr {($data >> 8) & 1}]]
close $out
disconnect
