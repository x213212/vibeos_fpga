set init_file "H:/testproject/project_2/ps_uart0_hw/ps_uart0_hw.srcs/sources_1/bd/ps_uart0/ip/ps_uart0_processing_system7_0_0/ps7_init.tcl"
set port_base 0xE0000000
set message "PS_UART0_OK\r\n"

if {$argc >= 1} {
    set message [lindex $argv 0]
}
if {$argc >= 2} {
    set init_file [lindex $argv 1]
}

proc read32 {addr} {
    return [expr {[mrd -value $addr] & 0xffffffff}]
}

proc write32 {addr value} {
    mwr $addr [expr {$value & 0xffffffff}]
}

proc uart_wait_tx_space {} {
    global port_base
    set timeout 100000
    while {$timeout > 0} {
        set sr [read32 [expr {$port_base + 0x2c}]]
        if {($sr & 0x10) == 0} {
            return
        }
        incr timeout -1
    }
    error "PS UART TX FIFO stayed full"
}

proc uart_putc {ch} {
    global port_base
    uart_wait_tx_space
    write32 [expr {$port_base + 0x30}] $ch
}

proc uart_puts {s} {
    for {set i 0} {$i < [string length $s]} {incr i} {
        scan [string index $s $i] %c ch
        uart_putc $ch
    }
}

if {![file exists $init_file]} {
    error "Missing ps7_init.tcl: $init_file"
}

connect

if {[catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}]} {
    catch {targets -set -filter {name =~ "APU*"}}
}

source $init_file
ps7_init
if {[llength [info procs ps7_post_config]]} {
    ps7_post_config
}

# XUartPs-compatible 115200 8N1 setup after the generated PS clock/MIO init.
write32 [expr {$port_base + 0x0c}] 0x00003fff
write32 [expr {$port_base + 0x00}] 0x00000028
write32 [expr {$port_base + 0x04}] 0x00000020
write32 [expr {$port_base + 0x18}] 27
write32 [expr {$port_base + 0x34}] 15
write32 [expr {$port_base + 0x1c}] 0
write32 [expr {$port_base + 0x20}] 1
write32 [expr {$port_base + 0x14}] 0x00003fff
write32 [expr {$port_base + 0x00}] 0x00000117

uart_puts $message
after 200
puts "PS_UART_WITH_INIT_DONE"
disconnect
