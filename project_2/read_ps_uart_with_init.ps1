param(
    [string]$PortName = "COM9",
    [int]$BaudRate = 115200,
    [int]$Seconds = 8,
    [string]$Message = "PS_UART0_OK`r`n"
)

$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$xsdb = "H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat"
$tcl = Join-Path $projectDir "send_ps_uart_with_init.tcl"

$port = New-Object System.IO.Ports.SerialPort $PortName, $BaudRate, 'None', 8, 'One'
$port.ReadTimeout = 100
$port.DtrEnable = $true
$port.RtsEnable = $true

try {
    $port.Open()
    Write-Host "Reading $PortName @ $BaudRate and sending through generated PS7 UART init..."

    $proc = Start-Process -FilePath $xsdb -ArgumentList @($tcl, $Message) -NoNewWindow -PassThru
    $deadline = (Get-Date).AddSeconds($Seconds)
    $seen = $false

    while ((Get-Date) -lt $deadline) {
        $data = $port.ReadExisting()
        if ($data.Length -gt 0) {
            $seen = $true
            Write-Host -NoNewline $data
        }
        if ($proc.HasExited -and (Get-Date) -gt $deadline.AddSeconds(-1)) {
            break
        }
        Start-Sleep -Milliseconds 50
    }

    if (-not $proc.HasExited) {
        $proc.WaitForExit(5000) | Out-Null
    }

    if (-not $seen) {
        Write-Host "[no UART data received]"
    } else {
        Write-Host ""
    }

    if ($proc.HasExited -and $null -ne $proc.ExitCode -and $proc.ExitCode -ne 0) {
        throw "xsdb exited with code $($proc.ExitCode)"
    }
} finally {
    if ($port.IsOpen) {
        $port.Close()
    }
}
