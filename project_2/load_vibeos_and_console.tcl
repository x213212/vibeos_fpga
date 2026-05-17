set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/vibeos/os.trimmed.bin"
set cpu_reset_gpio 0x41200000
set ddr_load_addr 0x01000000

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
    set sig [mrd -value [expr {$base + 0x00}]]
    set state [mrd -value [expr {$base + 0x04}]]
    set i_ar [mrd -value [expr {$base + 0x08}]]
    set i_ps_ar [mrd -value [expr {$base + 0x24}]]
    set i_r [mrd -value [expr {$base + 0x0c}]]
    set d_aw [mrd -value [expr {$base + 0x10}]]
    set d_w [mrd -value [expr {$base + 0x14}]]
    set d_b [mrd -value [expr {$base + 0x18}]]
    set last_d_addr [mrd -value [expr {$base + 0x2c}]]
    set last_d_data [mrd -value [expr {$base + 0x30}]]
    set mmio_aw [mrd -value [expr {$base + 0x1c}]]
    set last_mmio_addr [mrd -value [expr {$base + 0x34}]]
    set last_mmio_data [mrd -value [expr {$base + 0x38}]]
    set first_mmio_data [mrd -value [expr {$base + 0x68}]]
    set second_mmio_data [mrd -value [expr {$base + 0x6c}]]
    set uart_counts [mrd -value [expr {$base + 0x74}]]
    set last_decode_addr [mrd -value [expr {$base + 0x78}]]
    set last_wstrb_data [mrd -value [expr {$base + 0x7c}]]
    set i0 [mrd -value [expr {$base + 0x40}]]
    set i1 [mrd -value [expr {$base + 0x44}]]
    set i2 [mrd -value [expr {$base + 0x48}]]
    set i3 [mrd -value [expr {$base + 0x4c}]]
    set fifo_status [mrd -value [expr {$base + 0x54}]]
    set enqueue_count [mrd -value [expr {$base + 0x58}]]
    set dequeue_count [mrd -value [expr {$base + 0x5c}]]
    set last_bytes [mrd -value [expr {$base + 0x60}]]
    puts [format "%s SIG=0x%08x STATE=0x%08x I_AR_COUNT=%d I_R=%d D_AW=%d D_W=%d D_B=%d LAST_D_ADDR=0x%08x LAST_D_DATA=0x%08x LAST_I_ARADDR=0x%08x LAST_I_PS_ARADDR=0x%08x MMIO_AW=%d LAST_MMIO_ADDR=0x%08x LAST_MMIO_DATA=0x%08x FIRST_MMIO=0x%08x SECOND_MMIO=0x%08x UART_COUNTS=0x%08x LAST_DECODE=0x%08x LAST_WSTRB_DATA=0x%08x I0=0x%08x I1=0x%08x I2=0x%08x I3=0x%08x FIFO=0x%08x ENQ=%d DEQ=%d LAST=0x%08x" \
        $tag $sig $state $i_ar $i_r $d_aw $d_w $d_b $last_d_addr $last_d_data [mrd -value [expr {$base + 0x20}]] $i_ps_ar $mmio_aw $last_mmio_addr $last_mmio_data $first_mmio_data $second_mmio_data $uart_counts $last_decode_addr $last_wstrb_data $i0 $i1 $i2 $i3 $fifo_status $enqueue_count $dequeue_count $last_bytes]
}

proc usb_ulpi_wait_idle {ulpi_view limit} {
    for {set i 0} {$i < $limit} {incr i} {
        set v [mrd -value $ulpi_view]
        if {($v & 0x40000000) == 0} {
            return [list 1 $v]
        }
        after 1
    }
    return [list 0 [mrd -value $ulpi_view]]
}

proc usb0_phy_reset_release {} {
    set gpio_data0 0xE000A040
    set usb_cmd    0xE0002140
    set usb_sts    0xE0002144
    set ulpi_view  0xE0002170
    set configflag 0xE0002180
    set portsc1    0xE0002184
    set otgsc      0xE00021A4
    set usb_mode   0xE00021A8

    mwr $usb_cmd 0x00080002
    after 50
    mwr $usb_mode 0x00000003
    mwr $configflag 0x00000001
    set ulpi0 [mrd -value $ulpi_view]
    set otg0 [mrd -value $otgsc]
    mwr $otgsc [expr {($otg0 & ~0x007f0001) | 0x00000022}]
    mwr $ulpi_view 0xa0000000
    set wake [usb_ulpi_wait_idle $ulpi_view 200]
    set ulpi_ok 0
    if {[lindex $wake 0]} {
        mwr $ulpi_view 0x600b0060
        set drv [usb_ulpi_wait_idle $ulpi_view 200]
        set ulpi_ok [lindex $drv 0]
    }
    mwr $portsc1 [expr {[mrd -value $portsc1] | 0x00001000}]
    mwr $usb_sts [mrd -value $usb_sts]
    mwr $usb_cmd 0x00080001
    after 1000
    puts [format "USB0_PHY_RELEASE CMD=0x%08x STS=0x%08x PORT=0x%08x MODE=0x%08x OTG0=0x%08x OTG=0x%08x ULPI0=0x%08x ULPI=0x%08x ULPI_OK=%d GPIO=0x%08x" \
        [mrd -value $usb_cmd] [mrd -value $usb_sts] [mrd -value $portsc1] [mrd -value $usb_mode] \
        $otg0 [mrd -value $otgsc] $ulpi0 [mrd -value $ulpi_view] $ulpi_ok [mrd -value $gpio_data0]]
}

if {![file exists $bit_file]} {
    error "Missing bitstream: $bit_file"
}
if {![file exists $init_file]} {
    error "Missing ps7_init.tcl: $init_file"
}
if {![file exists $bin_file]} {
    error "Missing firmware binary: $bin_file"
}

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
usb0_phy_reset_release

mwr $cpu_reset_gpio 0x00000000
dow -data $bin_file $ddr_load_addr
if {[string first "z7lite_probe" $bin_file] >= 0} {
    # CPU 0x801e4018 maps to PS DDR 0x011e4018 with the current 0x80000000 -> 0x01000000 remap.
    mwr 0x011e4018 0x00000001
    puts "PATCHED_OS_DEBUG=1"
}
set first_word [mrd -value $ddr_load_addr]
set word_04 [mrd -value [expr {$ddr_load_addr + 4}]]
set word_08 [mrd -value [expr {$ddr_load_addr + 8}]]
set word_0c [mrd -value [expr {$ddr_load_addr + 12}]]
set word_10 [mrd -value [expr {$ddr_load_addr + 16}]]
set word_20 [mrd -value [expr {$ddr_load_addr + 0x20}]]
set word_24 [mrd -value [expr {$ddr_load_addr + 0x24}]]
set low_word [mrd -value 0x00100000]
puts [format "DOW_BINARY_FILE=%s" $bin_file]
puts [format "DDR_LOAD_ADDR=0x%08x FIRST_WORD=0x%08x" $ddr_load_addr [expr {$first_word & 0xffffffff}]]
puts [format "DDR_WORDS_04_10=0x%08x 0x%08x 0x%08x 0x%08x" \
    [expr {$word_04 & 0xffffffff}] [expr {$word_08 & 0xffffffff}] [expr {$word_0c & 0xffffffff}] [expr {$word_10 & 0xffffffff}]]
puts [format "DDR_WORD_20=0x%08x" [expr {$word_20 & 0xffffffff}]]
puts [format "DDR_WORD_24=0x%08x" [expr {$word_24 & 0xffffffff}]]
puts [format "DDR_LOW_WORD=0x%08x" [expr {$low_word & 0xffffffff}]]
after 1000
mwr $cpu_reset_gpio 0x00000001
after 250
dump_dbg_regs "DBG_BEFORE_CONSOLE"

jtag targets -set -filter {name =~ "xc7z020*" && irlen == 6}
set seq [jtag sequence]
set user1_ir_bits "010000"
set dr_width 10
set dr_read_bits "0000000000"
set dr_ack_bits "1001111001"
set suppress_next_valid 0
set last_printed_data -1
set printed_count 0
set idle_count 0

puts "JTAG_CONSOLE_BEGIN"
set end_ms [expr {[clock milliseconds] + 20000}]
while {[clock milliseconds] < $end_ms} {
    $seq clear
    $seq state IDLE
    $seq irshift -bits -state IDLE 6 $user1_ir_bits
    $seq drshift -capture -bits -state IDLE $dr_width $dr_read_bits
    set dr [$seq run -bits -single]
    set dr10 [string range $dr 0 9]
    set valid [string index $dr10 8]
    set data [bits_to_int_lsb [string range $dr10 0 7]]

    if {$valid eq "1"} {
        set idle_count 0
        if {$suppress_next_valid && $data == $last_printed_data} {
            set suppress_next_valid 0
            after 1
            continue
        }
        set suppress_next_valid 0
        puts -nonewline [format "%c" $data]
        flush stdout
        set last_printed_data $data
        incr printed_count
        $seq clear
        $seq state IDLE
        $seq irshift -bits -state IDLE 6 $user1_ir_bits
        $seq drshift -bits -state IDLE 10 $dr_ack_bits
        $seq run
        set suppress_next_valid 1
    } else {
        incr idle_count
    }
    if {$printed_count > 0 && $idle_count > 2000} {
        break
    }
    after 1
}
puts "\nJTAG_CONSOLE_END PRINTED=$printed_count"
$seq delete
dump_dbg_regs "DBG_AFTER_CONSOLE"
disconnect
