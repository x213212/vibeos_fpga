$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$xsdb = "H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat"
$hwServer = "H:\Xilinx\vivado\Vivado\2019.2\bin\hw_server.bat"
$smokeTcl = Join-Path $projectDir "ddr_smoke_test.tcl"
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
    Write-Host "Running PS DDR smoke test..."
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $smokeOutput = & $xsdb $smokeTcl 2>&1
    $ErrorActionPreference = $oldErrorActionPreference
    $smokeOutput | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or ($smokeOutput -match "Memory write error|Memory read error|AP transaction error|JTAG port open error|DDR mismatch|invoked from within")) {
        throw "ddr_smoke_test.tcl failed"
    }
} finally {
    if ($null -ne $hwProc -and -not $hwProc.HasExited) {
        Stop-Process -Id $hwProc.Id -Force
    }
    Get-Process | Where-Object { $_.ProcessName -match 'hw_server|cs_server' } | ForEach-Object {
        Stop-Process -Id $_.Id -Force
    }
}
