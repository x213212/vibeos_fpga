set out [open "H:/testproject/project_2/release_vibeos_full_psinit_usb.out" "w"]
set script_dir [file dirname [file normalize [info script]]]
# Default to the current keyboard/UI/Ethernet-capable 50 MHz bitstream.
# The older riscv_ps_ddr_hw default does not preserve the same PS/PL IOP path
# and can make the OS early-trap when it reads PS USB registers at boot.
set bit_file "$script_dir/riscv_ps_ddr_hw_eth_clkdomain_probe_50/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "$script_dir/riscv_ps_ddr_hw_eth_clkdomain_probe_50/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/vibeos/os.window_gui.bin"
set ddr_load_addr 0x01000000

if {[llength $argv] >= 1} {
    set bin_file [lindex $argv 0]
}
if {[llength $argv] >= 2} {
    set bit_file [lindex $argv 1]
}
if {[llength $argv] >= 3} {
    set init_file [lindex $argv 2]
}

proc rd {addr} {
    return [expr {[mrd -value $addr] & 0xffffffff}]
}

proc show {out name addr} {
    puts $out [format "%s=0x%08x" $name [rd $addr]]
}

proc wait_clear {addr mask limit_ms} {
    for {set i 0} {$i < $limit_ms} {incr i} {
        set v [mrd -value $addr]
        if {($v & $mask) == 0} {
            return [list 1 $v]
        }
        after 1
    }
    return [list 0 [mrd -value $addr]]
}

proc ulpi_write {view reg val} {
    mwr $view 0xa0000000
    wait_clear $view 0x80000000 500
    mwr $view [expr {0x60000000 | (($reg & 0xff) << 16) | ($val & 0xff)}]
    return [wait_clear $view 0x40000000 500]
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1

puts $out "RELEASE_VIBEOS_FULL_PSINIT_USB_BEGIN"
puts $out [format "BIN_FILE=%s" $bin_file]

catch {rst -system}
after 2000
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
source $init_file
ps7_init
targets -set -filter {name =~ "xc7z020*"}
fpga -file $bit_file
after 500
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}
if {[llength [info procs ps7_post_config]]} {
    ps7_post_config
}
after 500

set cpu_reset_gpio 0x41200000
set slcr_unlock   0xF8000008
set slcr_lock     0xF8000004
set mio46         0xF80007B8
set gpio_data1    0xE000A044
set gpio_dirm1    0xE000A244
set gpio_oen1     0xE000A248
set bit46         0x00004000

set usb_cmd       0xE0002140
set usb_sts       0xE0002144
set ctrldsseg     0xE0002150
set periodiclist  0xE0002154
set asynclist     0xE0002158
set ttctrl        0xE000215C
set burstsize     0xE0002160
set ulpi_view     0xE0002170
set configflag    0xE0002180
set portsc1       0xE0002184
set otgsc         0xE00021A4
set usb_mode      0xE00021A8
set sbuscfg       0xE0002090

catch {mwr $cpu_reset_gpio 0x00000000}

mwr $usb_cmd 0x00080000
after 10
mwr $usb_cmd 0x00080002
after 50

mwr -force $slcr_unlock 0x0000DF0D
mwr -force $mio46 0x00001600
mwr -force $slcr_lock 0x0000767B
mwr $gpio_dirm1 [expr {[rd $gpio_dirm1] | $bit46}]
mwr $gpio_oen1  [expr {[rd $gpio_oen1]  | $bit46}]
mwr $gpio_data1 [expr {[rd $gpio_data1] & ~$bit46}]
after 50
mwr $gpio_data1 [expr {[rd $gpio_data1] | $bit46}]
after 500

mwr $usb_cmd 0x00080002
after 100
mwr $usb_mode 0x00000003
mwr $configflag 0x00000001
mwr $sbuscfg 0x00000007
mwr $ctrldsseg 0x00000000
mwr $ttctrl 0x00000000
mwr $burstsize 0x00001010
mwr $asynclist 0x00000000
mwr $periodiclist 0x00000000
mwr $otgsc [expr {([rd $otgsc] & ~0x007f0001) | 0x00000022}]
ulpi_write $ulpi_view 0x0a 0x86
ulpi_write $ulpi_view 0x04 0x41
ulpi_write $ulpi_view 0x07 0x00
ulpi_write $ulpi_view 0x0b 0x60
mwr $portsc1 [expr {[rd $portsc1] | 0x00001000}]
mwr $usb_sts [rd $usb_sts]
mwr $usb_cmd 0x00080001
after 100

show $out AFTER_USB_RECOVER_USBCMD $usb_cmd
show $out AFTER_USB_RECOVER_USBSTS $usb_sts
show $out AFTER_USB_RECOVER_PORTSC1 $portsc1
show $out AFTER_USB_RECOVER_ULPI $ulpi_view

# Match the proven manual recovery flow:
# 1. program PS/PL and release USB
# 2. disconnect/reconnect DAP
# 3. then load the RISC-V OS while holding the PL CPU reset GPIO
#
# Loading immediately after fpga/ps7_post_config in the same DAP session has
# produced a bad first fetch at 0x80000040 on this board. The split session
# keeps the full-release script behavior identical to the working
# program_riscv_psinit_bit_usb.tcl + release_vibeos_no_usb_reset.tcl sequence.
flush $out
disconnect
after 500

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
jtag targets -set -filter {level == 0}
jtag targets -open -filter {level == 0}
after 100
catch {jtag frequency 30000000}
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}

mwr $cpu_reset_gpio 0x00000000
dow -data $bin_file $ddr_load_addr
set first_word [mrd -value $ddr_load_addr]
puts $out [format "DOW_BINARY_FILE=%s DDR_LOAD_ADDR=0x%08x FIRST_WORD=0x%08x" $bin_file $ddr_load_addr [expr {$first_word & 0xffffffff}]]
after 100
mwr $cpu_reset_gpio 0x00000001
after 1000

# Do not touch PS USB registers after the PL CPU is released.  On this board
# the ARM DAP can latch an AHB AP transaction error while the standalone USB
# host is being reinitialized by VibeOS, which then breaks later debug reads.
puts $out "RELEASE_DONE"

close $out
disconnect
