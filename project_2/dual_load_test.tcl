set bin_file "H:/testproject/vibeos/os.fpga_minimal.bin"
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
mwr 0x41200000 0x00000000
after 100
dow -data $bin_file 0x00000000
dow -data $bin_file 0x01000000
puts [format "LOW0=0x%08x HIGH0=0x%08x" [mrd -value 0x00000000] [mrd -value 0x01000000]]
mwr 0x41200000 0x00000001
after 2000
set base 0x41210000
puts [format "DBG_FLAGS=0x%08x" [mrd -value [expr {$base+0x04}]]]
puts [format "I_AR=0x%08x I_PS=0x%08x I0=0x%08x MMIO=%d LAST_MMIO=0x%08x" [mrd -value [expr {$base+0x20}]] [mrd -value [expr {$base+0x24}]] [mrd -value [expr {$base+0x40}]] [mrd -value [expr {$base+0x1c}]] [mrd -value [expr {$base+0x38}]]]
disconnect
