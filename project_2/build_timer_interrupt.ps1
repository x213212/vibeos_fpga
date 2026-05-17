$wslProject = "/root/trade_new/os/mini-riscv-os/04-TimerInterrupt"
$includeDir = "/mnt/h/testproject/project_2/riscv_build_include"
$winOut = "H:\testproject\project_2\timer_interrupt.bin"
$wslOut = "/mnt/h/testproject/project_2/timer_interrupt.bin"

$cmd = "cd $wslProject && rm -f os.elf timer_interrupt.bin && riscv64-unknown-elf-gcc -nostdlib -fno-builtin -mcmodel=medany -march=rv32ima -mabi=ilp32 -I$includeDir -T os.ld -o os.elf start.s sys.s lib.c timer.c os.c && riscv64-unknown-elf-objcopy -O binary os.elf timer_interrupt.bin && cp timer_interrupt.bin $wslOut && ls -l os.elf timer_interrupt.bin $wslOut"
wsl --exec /bin/sh -lc $cmd
if ($LASTEXITCODE -ne 0) {
    throw "Failed to build/copy 04-TimerInterrupt binary from WSL project"
}

$item = Get-Item -LiteralPath $winOut
Write-Host ("WROTE real 04-TimerInterrupt binary to {0} ({1} bytes)" -f $item.FullName, $item.Length)
