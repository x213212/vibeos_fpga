param(
    [string]$PortName = "COM10",
    [int]$BaudRate = 115200,
    [int]$Seconds = 5,
    [string]$Message = "PS_UART0_OK`r`n",
    [string]$MioMode = "drive",
    [int]$BaudGen = 27,
    [int]$BaudDiv = 15,
    [int]$SetUartClk = 0,
    [int]$UartIndex = 0,
    [int]$MioPair = 14
)

$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$xsdb = "H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat"
$tcl = Join-Path $projectDir "ps_uart0_send.tcl"

$port = New-Object System.IO.Ports.SerialPort $PortName, $BaudRate, 'None', 8, 'One'
$port.ReadTimeout = 100
$port.DtrEnable = $true
$port.RtsEnable = $true

try {
    $port.Open()
    Write-Host "Reading $PortName @ $BaudRate and sending through PS UART0..."

    $proc = Start-Process -FilePath $xsdb -ArgumentList @($tcl, $Message, $MioMode, $BaudGen, $BaudDiv, $SetUartClk, $UartIndex, $MioPair) -NoNewWindow -PassThru
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
