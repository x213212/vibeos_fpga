Set-Location 'H:\testproject'
Remove-Item 'H:\testproject\project_2\eth60_build_stdout.log' -ErrorAction SilentlyContinue
Remove-Item 'H:\testproject\project_2\eth60_build_exitcode.txt' -ErrorAction SilentlyContinue

$VivadoBat = 'H:\Xilinx\vivado\Vivado\2019.2\bin\vivado.bat'
$LogPath = 'H:\testproject\project_2\eth60_build_stdout.log'
$ExitPath = 'H:\testproject\project_2\eth60_build_exitcode.txt'
$OutDir = 'H:/testproject/project_2/riscv_ps_ddr_hw_eth_clkdomain_probe_60'

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

$commonArgs = "-enable_eth -fclk_mhz 60 -out_dir $OutDir -jobs 8"
$code = Invoke-VivadoBatch 'H:\testproject\project_2\build_riscv_ps_ddr_hw.tcl' $commonArgs
"EXITCODE=$code" | Set-Content $ExitPath -Encoding ASCII
exit $code
