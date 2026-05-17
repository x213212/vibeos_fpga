set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/project_2/mini_hello_os.bin"
set cpu_reset_gpio 0x41200000
set ddr_load_addr 0x01000000
set dbg_base 0x41210000

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

puts "PLUART_RELEASE_CPU"
mwr $cpu_reset_gpio 0x00000001
after 1500

read_dbg DBG_FIFO_ENQUEUE_COUNT 0x58
read_dbg DBG_FIFO_DEQUEUE_COUNT 0x5c
read_dbg DBG_FIFO_LAST_BYTES 0x60
read_dbg DBG_FIFO_STATUS 0x54
read_dbg DBG_UART_COUNTS 0x6c
read_dbg DBG_LAST_UART_DECODE_ADDR 0x70
read_dbg DBG_LAST_WSTRB_WDATA 0x74
disconnect
