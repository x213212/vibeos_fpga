set bit_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit"
set init_file "H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"

connect -url tcp:127.0.0.1:3121
jtag targets -set -filter {level == 0}
jtag targets -open -filter {level == 0}
after 1000
catch {jtag frequency 10000000}

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
if {[llength [info procs ps7_post_config]]} { ps7_post_config }
after 1000

proc rd {name addr} {
    puts [format "%s=0x%08x" $name [mrd -value $addr]]
}

rd SDIO0_00 0xE0100000
rd SDIO0_04 0xE0100004
rd SDIO0_24 0xE0100024
rd SDIO0_28 0xE0100028
rd SDIO0_2C 0xE010002C
rd SDIO0_30 0xE0100030
rd SDIO0_34 0xE0100034
rd SDIO0_38 0xE0100038
rd SDIO1_00 0xE0101000
rd SDIO1_04 0xE0101004
rd SLCR_SDIO_CLK_CTRL 0xF8000150

disconnect
