Set-Location 'H:\testproject'
Remove-Item 'H:\testproject\project_2\eth_build_stdout.log' -ErrorAction SilentlyContinue
Remove-Item 'H:\testproject\project_2\eth_build_exitcode.txt' -ErrorAction SilentlyContinue

$VivadoBat = 'H:\Xilinx\vivado\Vivado\2019.2\bin\vivado.bat'
$LogPath = 'H:\testproject\project_2\eth_build_stdout.log'
$OutDir = 'H:/testproject/project_2/riscv_ps_ddr_hw_eth_ui'
$BitPath = 'H:\testproject\project_2\riscv_ps_ddr_hw_eth_ui\riscv_ps_ddr_wrapper.bit'

function Invoke-VivadoBatch {
    param(
        [string]$Source,
        [string]$TclArgs
    )

    Add-Content -Path $LogPath -Value ""
    Add-Content -Path $LogPath -Value "==== VIVADO STEP: $Source $TclArgs ===="
    $cmd = "`"$VivadoBat`" -mode batch -source `"$Source`" -tclargs $TclArgs >> `"$LogPath`" 2>>&1"
    $proc = Start-Process -FilePath "$env:ComSpec" `
                          -ArgumentList @('/c', $cmd) `
                          -WorkingDirectory 'H:\testproject' `
                          -WindowStyle Hidden `
                          -Wait `
                          -PassThru
    Add-Content -Path $LogPath -Value "==== VIVADO EXITCODE: $($proc.ExitCode) ===="
    return $proc.ExitCode
}

$commonArgs = "-enable_eth -fclk_mhz 50 -out_dir $OutDir -jobs 8"
$code = Invoke-VivadoBatch 'H:\testproject\project_2\build_riscv_ps_ddr_hw.tcl' $commonArgs
if ($code -ne 0) {
    "EXITCODE=$code" | Set-Content 'H:\testproject\project_2\eth_build_exitcode.txt'
    exit $code
}

if (-not (Test-Path $BitPath)) {
    $code = Invoke-VivadoBatch 'H:\testproject\project_2\finish_riscv_ps_ddr_hw.tcl' $commonArgs
    if ($code -ne 0) {
        "EXITCODE=$code" | Set-Content 'H:\testproject\project_2\eth_build_exitcode.txt'
        exit $code
    }
}

if (-not (Test-Path $BitPath)) {
    "EXITCODE=2" | Set-Content 'H:\testproject\project_2\eth_build_exitcode.txt'
    Add-Content -Path $LogPath -Value "ERROR: bitstream was not generated: $BitPath"
    exit 2
}

"EXITCODE=0" | Set-Content 'H:\testproject\project_2\eth_build_exitcode.txt'
exit 0
