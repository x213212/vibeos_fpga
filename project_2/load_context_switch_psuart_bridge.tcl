set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/project_2/context_switch.bin"

set cpu_reset_gpio 0x41200000
set ddr_load_addr 0x01000000
set dbg_base 0x41210000
set ps_console_pop_addr [expr {$dbg_base + 0xA0}]

set uart_base 0xE0000000

proc read32 {addr} {
    return [expr {[mrd -value $addr] & 0xffffffff}]
}

proc write32 {addr value} {
    mwr $addr [expr {$value & 0xffffffff}]
}

proc uart_putc {ch} {
    global uart_base

    set timeout 100000
    while {$timeout > 0} {
        set sr [read32 [expr {$uart_base + 0x2C}]]
        if {($sr & 0x10) == 0} {
            break
        }
        incr timeout -1
    }

    write32 [expr {$uart_base + 0x30}] $ch
}

proc read_dbg {name offset} {
    global dbg_base
    set value [read32 [expr {$dbg_base + $offset}]]
    puts [format "%s=0x%08x" $name $value]
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

proc zero_words {base_addr byte_count} {
    set end_addr [expr {$base_addr + $byte_count}]
    for {set addr $base_addr} {$addr < $end_addr} {incr addr 4} {
        mwr $addr 0x00000000
    }
    puts [format "ZERO_BYTES=%d @0x%08x" $byte_count $base_addr]
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
zero_words [expr {$ddr_load_addr + 0x3000}] 0x2000

puts [format "DDR_WORDS=0x%08x 0x%08x 0x%08x 0x%08x" \
    [read32 $ddr_load_addr] \
    [read32 [expr {$ddr_load_addr + 4}]] \
    [read32 [expr {$ddr_load_addr + 8}]] \
    [read32 [expr {$ddr_load_addr + 12}]]]

puts "CONTEXT_SWITCH_PSUART_BRIDGE_RELEASE_CPU"

mwr $cpu_reset_gpio 0x00000001

set printed 0
set idle 0

for {set n 0} {$n < 12000} {incr n} {
    set v [read32 $ps_console_pop_addr]

    if {($v & 0x100) != 0} {
        set ch [expr {$v & 0xff}]

        uart_putc $ch

        puts -nonewline [format "%c" $ch]
        flush stdout

        incr printed
        set idle 0
    } else {
        after 1
        incr idle

        if {$printed > 0 && $idle > 500} {
            break
        }
    }
}

puts ""
puts [format "CONTEXT_SWITCH_PSUART_BRIDGE_PRINTED=%d" $printed]

read_dbg DBG_FIFO_ENQUEUE_COUNT 0x58
read_dbg DBG_FIFO_DEQUEUE_COUNT 0x5c
read_dbg DBG_FIFO_LAST_BYTES 0x60
read_dbg DBG_FIFO_STATUS 0x54
read_dbg DBG_D_AR_COUNT 0x80
read_dbg DBG_D_R_COUNT 0x84
read_dbg DBG_LAST_D_ARADDR 0x88
read_dbg DBG_LAST_D_RDATA 0x90

disconnect
