set out [open "H:/testproject/project_2/reload_vibeos_with_ps7_init.out" "w"]
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/vibeos/os.fpga_minimal.bin"
set cpu_reset_gpio 0x41200000
set ddr_load_addr 0x01000000

if {[llength $argv] >= 1} {
    set bin_file [lindex $argv 0]
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}
source $init_file
ps7_init
if {[llength [info procs ps7_post_config]]} {
    ps7_post_config
}

puts $out "RELOAD_WITH_PS7_INIT_BEGIN"
puts $out [format "PORTSC1_BEFORE=0x%08x" [mrd -value 0xE0002184]]
mwr $cpu_reset_gpio 0x00000000
dow -data $bin_file $ddr_load_addr
puts $out [format "DDR_FIRST=0x%08x" [mrd -value $ddr_load_addr]]
after 100
mwr $cpu_reset_gpio 0x00000001
after 3000
puts $out [format "PORTSC1_AFTER=0x%08x" [mrd -value 0xE0002184]]
puts $out "RELOAD_WITH_PS7_INIT_DONE"
close $out
disconnect
