$wslProject = "/root/trade_new/os/mini-riscv-os/05-Preemptive"
$includeDir = "/mnt/h/testproject/project_2/riscv_build_include"
$winOut = "H:\testproject\project_2\preemptive.bin"
$wslOut = "/mnt/h/testproject/project_2/preemptive.bin"

$cmd = "cd $wslProject && rm -f os.elf preemptive.bin && riscv64-unknown-elf-gcc -nostdlib -fno-builtin -mcmodel=medany -march=rv32ima -mabi=ilp32 -I$includeDir -T os.ld -o os.elf start.s sys.s lib.c timer.c task.c os.c user.c trap.c && riscv64-unknown-elf-objcopy -O binary os.elf preemptive.bin && cp preemptive.bin $wslOut && ls -l os.elf preemptive.bin $wslOut"
wsl --exec /bin/sh -lc $cmd
if ($LASTEXITCODE -ne 0) {
    throw "Failed to build/copy 05-Preemptive binary from WSL project"
}

$item = Get-Item -LiteralPath $winOut
Write-Host ("WROTE real 05-Preemptive binary to {0} ({1} bytes)" -f $item.FullName, $item.Length)
