param(
    [string]$PortName = "COM10",
    [int]$BaudRate = 115200,
    [int]$Seconds = 30
)

$port = New-Object System.IO.Ports.SerialPort $PortName, $BaudRate, 'None', 8, 'One'
$port.ReadTimeout = 200
$port.DtrEnable = $true
$port.RtsEnable = $true

try {
    $port.Open()
    Write-Host "Reading $PortName @ $BaudRate for $Seconds seconds..."
    $deadline = (Get-Date).AddSeconds($Seconds)
    $seen = $false
    while ((Get-Date) -lt $deadline) {
        $data = $port.ReadExisting()
        if ($data.Length -gt 0) {
            $seen = $true
            Write-Host -NoNewline $data
        }
        Start-Sleep -Milliseconds 50
    }
    if (-not $seen) {
        Write-Host "[no UART data received]"
    } else {
        Write-Host ""
    }
} finally {
    if ($port.IsOpen) {
        $port.Close()
    }
}
