set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/project_2/mini_hello_os.bin"
set cpu_reset_gpio 0x41200000
set ddr_load_addr 0x01000000
set dbg_base 0x41210000

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

proc read_dbg {name offset} {
    global dbg_base
    set value [mrd -value [expr {$dbg_base + $offset}]]
    puts [format "%s=0x%08x" $name [expr {$value & 0xffffffff}]]
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
        set b0 [expr {[lindex $bytes 0] & 0xff}]
        set b1 [expr {[lindex $bytes 1] & 0xff}]
        set b2 [expr {[lindex $bytes 2] & 0xff}]
        set b3 [expr {[lindex $bytes 3] & 0xff}]
        set word [expr {$b0 | ($b1 << 8) | ($b2 << 16) | ($b3 << 24)}]
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
catch {jtag frequency 1000000}

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
if {[llength [info procs ps7_post_config]]} {
    ps7_post_config
}
after 1000
catch {stop}

mwr $cpu_reset_gpio 0x00000000
after 200
mwr_binary $bin_file $ddr_load_addr
puts [format "DDR_WORDS=0x%08x 0x%08x 0x%08x 0x%08x" \
    [mrd -value $ddr_load_addr] [mrd -value [expr {$ddr_load_addr + 4}]] [mrd -value [expr {$ddr_load_addr + 8}]] [mrd -value [expr {$ddr_load_addr + 12}]]]
puts [format "DDR_0040=0x%08x DDR_0080=0x%08x DDR_0084=0x%08x" \
    [mrd -value [expr {$ddr_load_addr + 0x40}]] [mrd -value [expr {$ddr_load_addr + 0x80}]] [mrd -value [expr {$ddr_load_addr + 0x84}]]]
after 200

jtag targets -set -filter {name =~ "xc7z020*" && irlen == 6}
set seq [jtag sequence]
set user1_ir_bits "010000"
set dr_width 18
set dr_ack_width 10
set dr_ack_bits "1001111001"
set dr_read_bits "000000000000000000"

mwr $cpu_reset_gpio 0x00000001
after 1

puts "JTAG_CONSOLE_BEGIN"
set printed 0
set last_seq -1
for {set n 0} {$n < 80} {incr n} {
    $seq clear
    $seq state IDLE
    $seq irshift -bits -state IDLE 6 $user1_ir_bits
    $seq drshift -capture -bits -state IDLE $dr_width $dr_read_bits
    set dr [$seq run -bits -single]
    set dr18 [string range $dr 0 17]
    set valid [string index $dr18 16]
    set seq_id [bits_to_int_lsb [string range $dr18 8 15]]
    set data [bits_to_int_lsb [string range $dr18 0 7]]
    if {$n < 12} {
        puts [format "RAW_DR_%02d=%s valid=%s seq=0x%02x data=0x%02x" $n $dr18 $valid $seq_id $data]
    }
    if {$valid eq "1"} {
        set last_seq $seq_id
        puts -nonewline [format "%c" $data]
        flush stdout
        incr printed
        after 5
    }
    after 1
}
puts ""
puts [format "JTAG_CONSOLE_END PRINTED=%d" $printed]
$seq delete

read_dbg DBG_FIFO_ENQUEUE_COUNT 0x58
read_dbg DBG_FIFO_DEQUEUE_COUNT 0x5c
read_dbg DBG_FIFO_LAST_BYTES 0x60
read_dbg DBG_FIFO_STATUS 0x54
read_dbg DBG_JTAG_STATE 0x64
disconnect
