set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
set out [open "H:/testproject/project_2/probe_usb_pre_os.out" "w"]

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

mwr -force 0xF8000008 0x0000DF0D
mwr -force 0xF8000720 0x00000601
mwr 0xE000A204 [expr {[mrd -value 0xE000A204] | 0x00000100}]
mwr 0xE000A208 [expr {[mrd -value 0xE000A208] | 0x00000100}]
mwr 0xE000A040 [expr {[mrd -value 0xE000A040] & ~0x00000100}]
after 100
mwr 0xE000A040 [expr {[mrd -value 0xE000A040] | 0x00000100}]
after 500
mwr 0xE0002140 0x00080002
after 50
mwr 0xE00021A8 0x00000003
mwr 0xE0002184 [expr {[mrd -value 0xE0002184] | 0x00001000}]
mwr 0xE0002144 [mrd -value 0xE0002144]
mwr 0xE0002140 0x00080001
after 500

puts $out "PROBE_USB_PRE_OS_BEGIN"
foreach {name addr} {
    USB_USBCMD       0xE0002140
    USB_USBSTS       0xE0002144
    USB_CONFIGFLAG   0xE0002180
    USB_PORTSC1      0xE0002184
    USB_ULPI_VIEW    0xE0002170
    USB_OTGSC        0xE00021A4
    USB_USBMODE      0xE00021A8
    GPIO_DATA0       0xE000A040
    GPIO_DIRM0       0xE000A204
    GPIO_OEN0        0xE000A208
    SLCR_MIO_PIN_08  0xF8000720
    SLCR_MIO_PIN_28  0xF8000770
    SLCR_MIO_PIN_39  0xF8000798
} {
    if {[catch {mrd -value $addr} v]} {
        puts $out [format "%s_READ_ERROR=%s" $name $v]
    } else {
        puts $out [format "%s=0x%08x" $name $v]
    }
}

close $out
disconnect
