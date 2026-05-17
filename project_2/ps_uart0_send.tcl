set message "PS_UART0_OK\r\n"
if {$argc >= 1} {
    set message [lindex $argv 0]
}
set mio_mode "drive"
if {$argc >= 2} {
    set mio_mode [lindex $argv 1]
}
set baudgen 27
if {$argc >= 3} {
    set baudgen [expr {[lindex $argv 2]}]
}
set bauddiv 15
if {$argc >= 4} {
    set bauddiv [expr {[lindex $argv 3]}]
}
set set_uart_clk 0
if {$argc >= 5} {
    set set_uart_clk [expr {[lindex $argv 4]}]
}
set uart_index 0
if {$argc >= 6} {
    set uart_index [expr {[lindex $argv 5]}]
}
set mio_pair 14
if {$argc >= 7} {
    set mio_pair [expr {[lindex $argv 6]}]
}

proc read32 {addr} {
    return [expr {[mrd -value $addr] & 0xffffffff}]
}

proc write32 {addr value} {
    mwr $addr [expr {$value & 0xffffffff}]
}

proc or32 {addr mask} {
    write32 $addr [expr {[read32 $addr] | $mask}]
}

proc uart0_wait_tx_space {} {
    global uart_base
    set sr [expr {$uart_base + 0x2C}]
    set timeout 100000
    while {$timeout > 0} {
        set v [read32 $sr]
        # Channel_sts_reg0 bit 4 is TFUL. Wait until TX FIFO is not full.
        if {($v & 0x10) == 0} {
            return
        }
        incr timeout -1
    }
    error "UART0 TX FIFO stayed full"
}

proc uart0_putc {ch} {
    global uart_base
    uart0_wait_tx_space
    write32 [expr {$uart_base + 0x30}] $ch
}

proc uart0_puts {s} {
    for {set i 0} {$i < [string length $s]} {incr i} {
        scan [string index $s $i] %c ch
        uart0_putc $ch
    }
}

connect

if {[catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}]} {
    if {[catch {targets -set -filter {name =~ "APU*"}}]} {
        puts "WARN: could not select ARM/APU target explicitly; using current XSDB target"
    }
}

# MicroPhase Zynq-7020 Type-C/CH340 UART is normally wired to PS MIO14/15
# (RX=MIO14, TX=MIO15). The UART index and MIO pair are parameterized so
# board variants can be checked without editing this file.
write32 0xF8000008 0x0000DF0D

if {$uart_index == 0} {
    set uart_base 0xE0000000
    set aper_mask 0x00100000
} else {
    set uart_base 0xE0001000
    set aper_mask 0x00200000
}

# Keep UART0 peripheral clock visible to the PS bus and set the UART ref clock
# control to the standard PS7 50 MHz-style setting used by Xilinx templates.
or32 0xF800012C $aper_mask
if {$set_uart_clk} {
    write32 0xF8000154 0x00001402
}

# Route MIO14/15 to UART0. "template" follows the Xilinx ps7_init value;
# "drive" enables the same UART function with explicit output/input controls.
set mio_rx_addr [expr {0xF8000700 + ($mio_pair * 4)}]
set mio_tx_addr [expr {$mio_rx_addr + 4}]
if {$mio_mode eq "template"} {
    write32 $mio_rx_addr 0x00000600
    write32 $mio_tx_addr 0x00000600
} elseif {$mio_mode eq "zc702"} {
    write32 $mio_rx_addr 0x00001200
    write32 $mio_tx_addr 0x00001301
} else {
    write32 $mio_rx_addr 0x000016E1
    write32 $mio_tx_addr 0x000016E0
}

# Configure Cadence UART0 as 115200 8N1. The common PS UART ref clock is 50 MHz,
# so CD=27 and BDIV=15 gives about 115740 baud, within normal UART tolerance.
write32 [expr {$uart_base + 0x0C}] 0x00003FFF
write32 [expr {$uart_base + 0x00}] 0x00000028
write32 [expr {$uart_base + 0x04}] 0x00000020
write32 [expr {$uart_base + 0x18}] $baudgen
write32 [expr {$uart_base + 0x34}] $bauddiv
write32 [expr {$uart_base + 0x1C}] 0x0000000A
write32 [expr {$uart_base + 0x20}] 0x00000001
write32 [expr {$uart_base + 0x14}] 0x00003FFF
write32 [expr {$uart_base + 0x00}] 0x00000117

uart0_puts $message
after 100

puts "PS_UART0_SEND_DONE"
disconnect
