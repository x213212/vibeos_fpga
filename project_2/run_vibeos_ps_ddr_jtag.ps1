param(
    [int]$Seconds = 30,
    [string]$BinFile = "H:\testproject\vibeos\os.bin"
)

$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$xsdb = "H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat"
$hwServer = "H:\Xilinx\vivado\Vivado\2019.2\bin\hw_server.bat"
$loadTcl = Join-Path $projectDir "load_vibeos_to_ps_ddr.tcl"
$consoleTcl = Join-Path $projectDir "read_jtag_console.tcl"
$toolTemp = Join-Path $projectDir "xilinx_tmp"
New-Item -ItemType Directory -Force -Path $toolTemp | Out-Null
$env:TEMP = $toolTemp
$env:TMP = $toolTemp

Get-Process | Where-Object { $_.ProcessName -match 'hw_server|cs_server' } | ForEach-Object {
    Stop-Process -Id $_.Id -Force
}

Write-Host "Starting hw_server with ARM GDB ports disabled..."
$hwProc = Start-Process -FilePath $hwServer -ArgumentList @("-s", "tcp::3121", "-p0") -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 3

try {
    Write-Host "Programming PL, loading $BinFile to PS DDR, and releasing PL RISC-V..."
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $loadOutput = & $xsdb $loadTcl $BinFile 2>&1
    $ErrorActionPreference = $oldErrorActionPreference
    $loadOutput | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or ($loadOutput -match "Failed to download|Memory write error|Unsupported command|invoked from within")) {
        throw "load_vibeos_to_ps_ddr.tcl failed"
    }

    Write-Host "Reading PL UART over USER1 JTAG console for $Seconds seconds..."
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $consoleOutput = & $xsdb $consoleTcl $Seconds 2>&1
    $ErrorActionPreference = $oldErrorActionPreference
    $consoleOutput | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or ($consoleOutput -match "Memory write error|Unsupported command|invoked from within")) {
        throw "read_jtag_console.tcl failed"
    }
} finally {
    if ($null -ne $hwProc -and -not $hwProc.HasExited) {
        Stop-Process -Id $hwProc.Id -Force
    }
    Get-Process | Where-Object { $_.ProcessName -match 'hw_server|cs_server' } | ForEach-Object {
        Stop-Process -Id $_.Id -Force
    }
}
