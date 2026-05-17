set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/project_2/rv32_tiny_putchar.bin"
if {[llength $argv] >= 1} {
    set bin_file [lindex $argv 0]
}

proc bits_to_int_lsb {bits} {
    set value 0
    set width [string length $bits]
    for {set bit 0} {$bit < $width} {incr bit} {
        if {[string index $bits $bit] eq "1"} {
            set value [expr {$value | (1 << $bit)}]
        }
    }
    return $value
}

proc dump_dbg_regs {tag} {
    set base 0x41210000
    set fifo_status [mrd -value [expr {$base + 0x54}]]
    set enqueue_count [mrd -value [expr {$base + 0x58}]]
    set dequeue_count [mrd -value [expr {$base + 0x5c}]]
    set last_bytes [mrd -value [expr {$base + 0x60}]]
    set jtag_state [mrd -value [expr {$base + 0x64}]]
    puts [format "%s FIFO_STATUS=0x%08x ENQ=%d DEQ=%d LAST_BYTES=0x%08x JTAG_STATE=0x%08x" \
        $tag $fifo_status $enqueue_count $dequeue_count $last_bytes $jtag_state]
}

set cpu_reset_gpio 0x41200000
set ddr_load_addr 0x00100000

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
jtag targets -set -filter {level == 0}
jtag targets -open -filter {level == 0}
after 1000
jtag frequency 1000000

targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}
source $init_file
ps7_init

targets -set -filter {name =~ "xc7z020*"}
fpga -file $bit_file

targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
if {[llength [info procs ps7_post_config]]} {
    ps7_post_config
}
after 1000
catch {stop}

mwr $cpu_reset_gpio 0x00000000
dow -data $bin_file $ddr_load_addr
puts [format "DOW_BINARY_FILE=%s" $bin_file]
mwr $cpu_reset_gpio 0x00000001
after 1000
dump_dbg_regs "DBG_BEFORE_CONSOLE"

jtag targets -set -filter {name =~ "xc7z020*" && irlen == 6}
set seq [jtag sequence]
set user1_ir_bits "010000"
set dr_width 10
set dr_read_bits "0000000000"
set dr_ack_bits "1001111001"

puts "JTAG_CONSOLE_BEGIN"
set suppress_next_valid 0
set last_printed_data -1
for {set n 0} {$n < 32} {incr n} {
    $seq clear
    $seq state IDLE
    $seq irshift -bits -state IDLE 6 $user1_ir_bits
    $seq drshift -capture -bits -state IDLE $dr_width $dr_read_bits
    set dr [$seq run -bits -single]
    set dr10 [string range $dr 0 9]
    set valid [string index $dr10 8]
    set data [bits_to_int_lsb [string range $dr10 0 7]]
    puts [format "RAW_DR=%s valid=%s data=0x%02x" $dr10 $valid $data]
    if {$valid eq "1"} {
        if {$suppress_next_valid && $data == $last_printed_data} {
            puts "STALE_AFTER_ACK_DISCARD"
            set suppress_next_valid 0
            after 1
            continue
        }
        set suppress_next_valid 0
        puts -nonewline [format "%c" $data]
        flush stdout
        set last_printed_data $data
        $seq clear
        $seq state IDLE
        $seq irshift -bits -state IDLE 6 $user1_ir_bits
        $seq drshift -bits -state IDLE $dr_width $dr_ack_bits
        $seq run
        set suppress_next_valid 1
    }
    after 1
}
$seq delete
dump_dbg_regs "DBG_AFTER_CONSOLE"
disconnect
