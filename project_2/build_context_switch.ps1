$wslProject = "/root/trade_new/os/mini-riscv-os/02-ContextSwitch"
$includeDir = "/mnt/h/testproject/project_2/riscv_build_include"
$winOut = "H:\testproject\project_2\context_switch.bin"
$wslOut = "/mnt/h/testproject/project_2/context_switch.bin"

$cmd = "cd $wslProject && rm -f os.elf context_switch.bin && riscv64-unknown-elf-gcc -nostdlib -fno-builtin -mcmodel=medany -march=rv32ima -mabi=ilp32 -I$includeDir -T os.ld -o os.elf start.s sys.s lib.c os.c && riscv64-unknown-elf-objcopy -O binary os.elf context_switch.bin && cp context_switch.bin $wslOut && ls -l os.elf context_switch.bin $wslOut"
wsl --exec /bin/sh -lc $cmd
if ($LASTEXITCODE -ne 0) {
    throw "Failed to build/copy 02-ContextSwitch binary from WSL project"
}

$item = Get-Item -LiteralPath $winOut
Write-Host ("WROTE real 02-ContextSwitch binary to {0} ({1} bytes)" -f $item.FullName, $item.Length)
