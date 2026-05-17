$wslProject = "/root/trade_new/os/mini-riscv-os/01-HelloOs"
$winOut = "H:\testproject\project_2\helloO.bin"
$wslOut = "/mnt/h/testproject/project_2/helloO.bin"

wsl --exec /bin/sh -lc "cd $wslProject && make clean && make && riscv64-unknown-elf-objcopy -O binary os.elf os.bin && cp os.bin $wslOut && ls -l os.elf os.bin $wslOut"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to build/copy 01-HelloOs binary from WSL project"
}

$item = Get-Item -LiteralPath $winOut
Write-Host ("WROTE real 01-HelloOs binary to {0} ({1} bytes)" -f $item.FullName, $item.Length)
