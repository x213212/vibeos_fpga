set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set bin_file "H:/testproject/vibeos/os.trimmed.bin"
set cpu_reset_gpio 0x41200000
set ddr_load_addr 0x00100000
set os_debug_addr 0x002e4018

if {$argc >= 1} {
    set bin_file [lindex $argv 0]
}

proc xsdb_connect {} {
    connect -url tcp:127.0.0.1:3121
    configparams force-mem-access 1
    catch {jtag targets -set 1}
    catch {jtag frequency 1000000}
}

proc select_arm_target_with_retry {{tries 12}} {
    set last_targets ""
    for {set i 0} {$i < $tries} {incr i} {
        set last_targets [targets]
        if {![catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}]} {
            return
        }
        puts "ARM_DAP_RETRY_[expr {$i + 1}]=$last_targets"
        flush stdout
        after 1000
    }
    error "Cannot get usable ARM Cortex-A9 target after retries. Last XSDB targets: $last_targets"
}

proc require_dap_memory_access {} {
    set ps_ver_addr 0xF8007080
    if {[catch {mrd -value $ps_ver_addr} ps_ver]} {
        error "DAP_MEMORY_AP_NOT_READY: cannot read SLCR PS_VERSION at 0xF8007080: $ps_ver"
    }
    puts [format "DAP_SLCR_PS_VERSION=0x%08x" [expr {$ps_ver & 0xffffffff}]]
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

xsdb_connect
targets -set -filter {name =~ "xc7z020*"}
fpga -file $bit_file

select_arm_target_with_retry
catch {stop}
require_dap_memory_access

source $init_file
ps7_init
if {[llength [info procs ps7_post_config]]} {
    ps7_post_config
}
after 1000

select_arm_target_with_retry
catch {stop}

mwr $cpu_reset_gpio 0x00000000
dow -data $bin_file $ddr_load_addr

set first_word [mrd -value $ddr_load_addr]
puts [format "DDR_LOAD_FIRST_WORD=0x%08x" [expr {$first_word & 0xffffffff}]]

mwr $cpu_reset_gpio 0x00000001
puts "RISC_V_RELEASED"

set end_ms [expr {[clock milliseconds] + 5000}]
while {[clock milliseconds] < $end_ms} {
    catch {mwr $os_debug_addr 0x00000001}
    after 10
}
set dbg [mrd -value $os_debug_addr]
puts [format "OS_DEBUG_WORD=0x%08x" [expr {$dbg & 0xffffffff}]]

disconnect
