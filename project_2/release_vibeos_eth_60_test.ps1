param(
    [string]$BinFile = "H:/testproject/vibeos/os.bin"
)

$ErrorActionPreference = "Stop"

$xsdb = "H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat"
$root = "H:/testproject"
$project = "$root/project_2"
$bit = "$project/riscv_ps_ddr_hw_eth_clkdomain_probe_60_postroute_opt/riscv_ps_ddr_wrapper.bit"
$init = "$project/riscv_ps_ddr_hw_eth_clkdomain_probe_60/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl"
$program = "$project/program_riscv_psinit_bit_usb.tcl"
$release = "$project/release_vibeos_no_usb_reset.tcl"

& $xsdb $program $bit $init
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $xsdb $release $BinFile
exit $LASTEXITCODE
