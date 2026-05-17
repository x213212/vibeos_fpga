set out [open "H:/testproject/project_2/release_vibeos_no_usb_reset.out" "w"]
set bin_file "H:/testproject/vibeos/os.fpga_minimal.bin"
set ddr_load_addr 0x01000000
set cpu_reset_gpio 0x41200000
set jtag_fast_freq 30000000

if {[llength $argv] >= 1} {
    set bin_file [lindex $argv 0]
}
if {[info exists ::env(VIBE_JTAG_HZ)]} {
    set jtag_fast_freq $::env(VIBE_JTAG_HZ)
}

proc set_jtag_fast {freq} {
    catch {jtag targets -set -filter {level == 0}}
    catch {jtag targets -open -filter {level == 0}}
    after 100
    if {[catch {jtag frequency $freq} err]} {
        puts [format "JTAG_FREQ_SET_FAIL target=%s err=%s" $freq $err]
    } else {
        puts [format "JTAG_FREQ_SET=%s" $freq]
    }
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
set_jtag_fast $jtag_fast_freq
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

puts $out "RELEASE_VIBEOS_NO_USB_RESET_BEGIN"
puts $out [format "PORTSC1_BEFORE=0x%08x" [mrd -value 0xE0002184]]
puts $out [format "ULPI_BEFORE=0x%08x" [mrd -value 0xE0002170]]
mwr $cpu_reset_gpio 0x00000000
dow -data $bin_file $ddr_load_addr
set first_word [mrd -value $ddr_load_addr]
puts $out [format "DOW_BINARY_FILE=%s DDR_LOAD_ADDR=0x%08x FIRST_WORD=0x%08x" $bin_file $ddr_load_addr [expr {$first_word & 0xffffffff}]]
after 100
mwr $cpu_reset_gpio 0x00000001
after 3000
puts $out [format "PORTSC1_AFTER=0x%08x" [mrd -value 0xE0002184]]
puts $out [format "ULPI_AFTER=0x%08x" [mrd -value 0xE0002170]]
puts $out "RELEASE_DONE"

close $out
disconnect
