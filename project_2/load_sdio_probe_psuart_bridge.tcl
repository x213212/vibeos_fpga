set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/project_2/sdio_probe.bin"

set cpu_reset_gpio 0x41200000
set ddr_load_addr 0x01000000
set dbg_base 0x41210000
set ps_console_pop_addr [expr {$dbg_base + 0xA0}]
set uart_base 0xE0000000
set output_file "H:/testproject/project_2/sdio_probe_output.txt"

proc read32 {addr} { return [expr {[mrd -value $addr] & 0xffffffff}] }
proc write32 {addr value} { mwr $addr [expr {$value & 0xffffffff}] }

proc uart_putc {ch} {
    global uart_base
    set timeout 100000
    while {$timeout > 0} {
        if {([read32 [expr {$uart_base + 0x2C}]] & 0x10) == 0} { break }
        incr timeout -1
    }
    write32 [expr {$uart_base + 0x30}] $ch
}

proc mwr_binary {path base_addr} {
    set fd [open $path rb]
    fconfigure $fd -translation binary
    set data [read $fd]
    close $fd
    set len [string length $data]
    set count 0
    for {set i 0} {$i < $len} {incr i 4} {
        binary scan [string range $data $i [expr {$i + 3}]] c4 bytes
        set word [expr {([lindex $bytes 0] & 0xff) | (([lindex $bytes 1] & 0xff) << 8) | (([lindex $bytes 2] & 0xff) << 16) | (([lindex $bytes 3] & 0xff) << 24)}]
        mwr [expr {$base_addr + $i}] $word
        incr count 4
    }
    puts [format "MWR_BINARY_BYTES=%d" $count]
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
jtag targets -set -filter {level == 0}
jtag targets -open -filter {level == 0}
after 1000
catch {jtag frequency 10000000}

targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}
catch {rst -system}
after 2000
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
source $init_file
ps7_init

targets -set -filter {name =~ "xc7z020*"}
fpga -file $bit_file
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
if {[llength [info procs ps7_post_config]]} { ps7_post_config }
after 1000
catch {stop}

mwr $cpu_reset_gpio 0x00000000
after 200
mwr_binary $bin_file $ddr_load_addr
puts "SDIO_PROBE_RELEASE_CPU"
mwr $cpu_reset_gpio 0x00000001

set printed 0
set idle 0
set captured ""
for {set n 0} {$n < 50000} {incr n} {
    set v [read32 $ps_console_pop_addr]
    if {($v & 0x100) != 0} {
        set ch [expr {$v & 0xff}]
        uart_putc $ch
        puts -nonewline [format "%c" $ch]
        append captured [format "%c" $ch]
        flush stdout
        incr printed
        set idle 0
        if {$printed >= 3000} { break }
    } else {
        after 1
        incr idle
        if {$printed > 0 && $idle > 5000} { break }
    }
}

puts ""
puts [format "SDIO_PROBE_PRINTED=%d" $printed]
set out_fd [open $output_file w]
puts $out_fd $captured
puts $out_fd [format "SDIO_PROBE_PRINTED=%d" $printed]
close $out_fd
disconnect
