set root_dir "H:/testproject"
set bit_file "$root_dir/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "$root_dir/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set jtag_fast_freq 30000000

if {[info exists ::env(VIBE_JTAG_HZ)]} {
    set jtag_fast_freq $::env(VIBE_JTAG_HZ)
}

if {[llength $argv] >= 1} {
    set bit_file [lindex $argv 0]
}
if {[llength $argv] >= 2} {
    set init_file [lindex $argv 1]
} else {
    set derived_init [string map {
        "riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
        "riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
    } $bit_file]
    if {$derived_init ne $bit_file && [file exists $derived_init]} {
        set init_file $derived_init
    }
}

proc usb_ulpi_wait_idle {ulpi_view limit} {
    for {set i 0} {$i < $limit} {incr i} {
        set v [mrd -value $ulpi_view]
        if {($v & 0xc0000000) == 0} {
            return [list 1 $v]
        }
        after 1
    }
    return [list 0 [mrd -value $ulpi_view]]
}

proc set_jtag_fast {freq} {
    if {[catch {jtag frequency $freq} err]} {
        puts [format "JTAG_FREQ_SET_FAIL target=%s err=%s" $freq $err]
    } else {
        puts [format "JTAG_FREQ_SET=%s" $freq]
    }
}

proc z7lite_usb_phy_reset_release {} {
    set slcr_unlock 0xF8000008
    set slcr_lock   0xF8000004
    set mio46       0xF80007B8
    set gpio_data1  0xE000A044
    set gpio_dirm1  0xE000A244
    set gpio_oen1   0xE000A248
    set bit         0x00004000

    mwr -force $slcr_unlock 0x0000DF0D
    mwr -force $mio46 0x00001600
    mwr -force $slcr_lock 0x0000767B

    mwr $gpio_data1 [expr {[mrd -value $gpio_data1] & ~$bit}]
    mwr $gpio_dirm1 [expr {[mrd -value $gpio_dirm1] | $bit}]
    mwr $gpio_oen1  [expr {[mrd -value $gpio_oen1] | $bit}]
    after 10
    mwr $gpio_data1 [expr {[mrd -value $gpio_data1] | $bit}]
    after 500
}

proc usb0_host_release {} {
    set usb_cmd    0xE0002140
    set usb_sts    0xE0002144
    set ctrldsseg  0xE0002150
    set periodic   0xE0002154
    set async      0xE0002158
    set ttctrl     0xE000215C
    set burstsize  0xE0002160
    set ulpi_view  0xE0002170
    set configflag 0xE0002180
    set portsc1    0xE0002184
    set otgsc      0xE00021A4
    set usb_mode   0xE00021A8
    set sbuscfg    0xE0002090

    mwr $usb_cmd 0x00080000
    after 10
    mwr $usb_cmd 0x00080002
    after 50
    z7lite_usb_phy_reset_release
    mwr $usb_cmd 0x00080002
    after 100
    mwr $usb_mode 0x00000003
    mwr $configflag 0x00000001
    mwr $sbuscfg 0x00000007
    mwr $ctrldsseg 0x00000000
    mwr $ttctrl 0x00000000
    mwr $burstsize 0x00001010
    mwr $async 0x00000000
    mwr $periodic 0x00000000
    set otg0 [mrd -value $otgsc]
    mwr $otgsc [expr {($otg0 & ~0x007f0001) | 0x00000022}]
    mwr $ulpi_view 0xa0000000
    set wake [usb_ulpi_wait_idle $ulpi_view 200]
    if {[lindex $wake 0]} {
        mwr $ulpi_view 0x600a0086
        usb_ulpi_wait_idle $ulpi_view 200
        mwr $ulpi_view 0x60040041
        usb_ulpi_wait_idle $ulpi_view 200
        mwr $ulpi_view 0x60070000
        usb_ulpi_wait_idle $ulpi_view 200
        mwr $ulpi_view 0x600b0060
        usb_ulpi_wait_idle $ulpi_view 200
    }
    mwr $portsc1 [expr {[mrd -value $portsc1] | 0x00001000}]
    mwr $usb_sts [mrd -value $usb_sts]
    mwr $usb_cmd 0x00080001
    after 500
    puts [format "USB0_HOST_RELEASE CMD=0x%08x STS=0x%08x PORT=0x%08x MODE=0x%08x" \
        [mrd -value $usb_cmd] [mrd -value $usb_sts] [mrd -value $portsc1] [mrd -value $usb_mode]]
}

if {![file exists $bit_file]} {
    error "Missing bitstream: $bit_file"
}
if {![file exists $init_file]} {
    error "Missing ps7_init.tcl: $init_file"
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
jtag targets -set -filter {level == 0}
jtag targets -open -filter {level == 0}
after 500
set_jtag_fast $jtag_fast_freq

targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
catch {stop}
catch {rst -system}
after 1000
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
after 500
catch {stop}
usb0_host_release
puts [format "PROGRAM_PSINIT_BIT_USB_DONE BIT=%s INIT=%s" $bit_file $init_file]
disconnect
