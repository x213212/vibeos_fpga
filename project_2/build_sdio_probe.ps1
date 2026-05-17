$includeDir = "/mnt/h/testproject/project_2/riscv_build_include"
$workDir = "/mnt/h/testproject/project_2"
$cmd = "cd $workDir && rm -f sdio_probe.elf sdio_probe.bin && riscv64-unknown-elf-gcc -nostdlib -fno-builtin -mcmodel=medany -march=rv32ima -mabi=ilp32 -I$includeDir -I. -T sdio_probe.ld -o sdio_probe.elf sdio_probe_start.s console_api.c sdio_api.c fat16_api.c sdio_probe.c && riscv64-unknown-elf-objcopy -O binary sdio_probe.elf sdio_probe.bin && ls -l sdio_probe.elf sdio_probe.bin"
wsl --exec /bin/sh -lc $cmd
if ($LASTEXITCODE -ne 0) {
    throw "Failed to build SDIO probe"
}
