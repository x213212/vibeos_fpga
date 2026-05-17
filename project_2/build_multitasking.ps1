$wslProject = "/root/trade_new/os/mini-riscv-os/03-MultiTasking"
$includeDir = "/mnt/h/testproject/project_2/riscv_build_include"
$winOut = "H:\testproject\project_2\multitasking.bin"
$wslOut = "/mnt/h/testproject/project_2/multitasking.bin"

$cmd = "cd $wslProject && rm -f os.elf multitasking.bin && riscv64-unknown-elf-gcc -nostdlib -fno-builtin -mcmodel=medany -march=rv32ima -mabi=ilp32 -I$includeDir -T os.ld -o os.elf start.s sys.s lib.c task.c os.c user.c && riscv64-unknown-elf-objcopy -O binary os.elf multitasking.bin && cp multitasking.bin $wslOut && ls -l os.elf multitasking.bin $wslOut"
wsl --exec /bin/sh -lc $cmd
if ($LASTEXITCODE -ne 0) {
    throw "Failed to build/copy 03-MultiTasking binary from WSL project"
}

$item = Get-Item -LiteralPath $winOut
Write-Host ("WROTE real 03-MultiTasking binary to {0} ({1} bytes)" -f $item.FullName, $item.Length)
