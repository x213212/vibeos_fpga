$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$xsdb = "H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat"
$hwServer = "H:\Xilinx\vivado\Vivado\2019.2\bin\hw_server.bat"
$testTcl = Join-Path $projectDir "load_and_check_sig.tcl"
$consoleTcl = Join-Path $projectDir "read_jtag_console.tcl"
$toolTemp = Join-Path $projectDir "xilinx_tmp"

function Write-TinyPutcharBinary {
    param([string]$Path)

    # RISC-V payload:
    #   *(volatile uint32_t *)0x10000000 = 'A';
    #   *(volatile uint32_t *)0x10000000 = '\n';
    #   while (1) {}
    $words = @(
        0x00000013, # nop; tolerate cores that begin executing at RESET_VECTOR+4
        0x100002b7, # lui  t0,0x10000
        0x04100313, # addi t1,zero,65
        0x0062a023, # sw   t1,0(t0)
        0x00a00313, # addi t1,zero,10
        0x0062a023, # sw   t1,0(t0)
        0x0000006f  # jal  zero,0
    )

    $bytes = New-Object System.Collections.Generic.List[byte]
    foreach ($word in $words) {
        $u = [uint32]$word
        $bytes.Add([byte]($u -band 0xff))
        $bytes.Add([byte](($u -shr 8) -band 0xff))
        $bytes.Add([byte](($u -shr 16) -band 0xff))
        $bytes.Add([byte](($u -shr 24) -band 0xff))
    }
    [System.IO.File]::WriteAllBytes($Path, $bytes.ToArray())
}

$mode = if ($args.Count -ge 1) { $args[0] } else { "default" }
$binFile = Join-Path $projectDir "rv32_store_sig.bin"
$runLoad = $true
$runRawProbe = $false

switch ($mode) {
    "tiny_putchar" {
        $binFile = Join-Path $projectDir "rv32_tiny_putchar.bin"
        Write-TinyPutcharBinary $binFile
        $runRawProbe = $true
    }
    "raw_jtag_probe" {
        $runLoad = $false
        $runRawProbe = $true
    }
    "default" {
        if ($args.Count -ge 2) {
            $binFile = $args[1]
        }
    }
    default {
        $binFile = $mode
    }
}

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
    if ($runLoad) {
        Write-Host "Running PL RISC-V debug load test with $binFile..."
        $oldErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $testOutput = & $xsdb $testTcl $binFile 2>&1
        $ErrorActionPreference = $oldErrorActionPreference
        $testOutput | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0 -or ($testOutput -match "Memory write error|Memory read error|AP transaction error|JTAG port open error|invoked from within")) {
            throw "load_and_check_sig.tcl failed"
        }
    }

    if ($runRawProbe) {
        Write-Host "Running USER1 raw JTAG probe..."
        $env:JTAG_RAW_PROBE = "1"
        $env:JTAG_RAW_LIMIT = "200"
        $probeOutput = & $xsdb $consoleTcl 3 2>&1
        $probeOutput | ForEach-Object { Write-Host $_ }
        Remove-Item Env:\JTAG_RAW_PROBE -ErrorAction SilentlyContinue
        Remove-Item Env:\JTAG_RAW_LIMIT -ErrorAction SilentlyContinue
        if ($LASTEXITCODE -ne 0 -or ($probeOutput -match "JTAG port open error|invoked from within")) {
            throw "read_jtag_console.tcl raw probe failed"
        }
    }
} finally {
    if ($null -ne $hwProc -and -not $hwProc.HasExited) {
        Stop-Process -Id $hwProc.Id -Force
    }
    Get-Process | Where-Object { $_.ProcessName -match 'hw_server|cs_server' } | ForEach-Object {
        Stop-Process -Id $_.Id -Force
    }
}
