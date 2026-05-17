set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set cpu_reset_gpio 0x41200000
set ddr_test_addr 0x00100000

if {$argc >= 1} {
    set ddr_test_addr [lindex $argv 0]
}
if {$argc >= 2} {
    set bit_file [lindex $argv 1]
}
if {$argc >= 3} {
    set init_file [lindex $argv 2]
}

if {![file exists $bit_file]} {
    error "Missing bitstream: $bit_file"
}
if {![file exists $init_file]} {
    error "Missing ps7_init.tcl: $init_file"
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

proc write_read_check {addr value} {
    mwr $addr $value
    set got [mrd -value $addr]
    set got32 [expr {$got & 0xffffffff}]
    set exp32 [expr {$value & 0xffffffff}]
    puts [format "DDR_CHECK addr=0x%08x expected=0x%08x got=0x%08x" $addr $exp32 $got32]
    if {$got32 != $exp32} {
        error [format "DDR mismatch at 0x%08x" $addr]
    }
}

proc require_dap_memory_access {} {
    set ps_ver_addr 0xF8007080
    if {[catch {mrd -value $ps_ver_addr} ps_ver]} {
        error "DAP_MEMORY_AP_NOT_READY: cannot read SLCR PS_VERSION at 0xF8007080: $ps_ver"
    }
    puts [format "DAP_SLCR_PS_VERSION=0x%08x" [expr {$ps_ver & 0xffffffff}]]
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

# The AXI GPIO drives cpu_resetn: 0 holds PL RISC-V in reset, 1 releases it.
mwr $cpu_reset_gpio 0x00000000

write_read_check $ddr_test_addr 0x12345678
write_read_check [expr {$ddr_test_addr + 4}] 0xa5a55a5a
write_read_check [expr {$ddr_test_addr + 8}] 0xdeadbeef
puts "DDR_SMOKE_OK"

disconnect
