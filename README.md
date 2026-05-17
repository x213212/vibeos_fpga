# vibeos_fpga
<img width="2048" height="956" alt="image" src="https://github.com/user-attachments/assets/414cf18e-129d-4edd-ad3f-3e7cd6be8898" />
<img width="2048" height="956" alt="image" src="https://github.com/user-attachments/assets/9a4c57aa-f8c3-4c2f-ba0e-bcb7d0bdc0cb" />
<img width="2048" height="956" alt="image" src="https://github.com/user-attachments/assets/4ddebbe3-368e-4991-8b8f-0aa879f96798" />
<img width="2048" height="956" alt="image" src="https://github.com/user-attachments/assets/90c8d172-1443-496c-be90-ae1e695e60f2" />
<img width="956" height="1883" alt="image" src="https://github.com/user-attachments/assets/f848b02e-138f-4d6f-97bc-c1353acd9f44" />
<img width="2048" height="956" alt="image" src="https://github.com/user-attachments/assets/b10eded8-33f7-4eed-935a-50eabc76a967" />
<img width="2048" height="956" alt="image" src="https://github.com/user-attachments/assets/75075d60-ea68-4245-b6ac-e19b1a367536" />

VibeOS on a Zynq-7020 FPGA development board.

This repository keeps the source-level pieces for the current VibeOS FPGA
bring-up:

- `vibeos/`: RISC-V OS, USB HID, SD/VibeFS, GEM/lwIP, SSH, and GUI source
- `project_2/`: FPGA top-level RTL, HDMI/MMIO/USB debug RTL, Vivado Tcl, XSDB
  debug/load scripts, and board SOP notes
- `ultraembedded_riscv/`: RISC-V core source used by the FPGA top

Generated artifacts are intentionally not tracked. Rebuild `os.bin` and Vivado
bitstreams locally from the source and scripts.

## Last Known Working Ethernet/UI Flow

### Tool Versions

This flow was last verified with:

```text
Vivado:            v2019.2 64-bit
Vivado SW Build:   2708876
Vivado IP Build:   2700528
Local Vivado path: H:\Xilinx\vivado\Vivado\2019.2

WSL:               2.6.2.0
WSL distro:        Ubuntu 22.04.5 LTS
WSL kernel:        6.6.87.2-microsoft-standard-WSL2
RISC-V GCC:        riscv64-unknown-elf-gcc 10.2.0
Windows:           10.0.26200.7171
```

Run from the repository root in Windows PowerShell:

```powershell
cd <repo-root>

wsl --cd ./vibeos --exec make profile=fpga_minimal FPGA_MTIME_HZ=50000000 os.bin

powershell.exe -ExecutionPolicy Bypass -File .\project_2\release_vibeos_eth_stable.ps1 ./vibeos/os.bin

Start-Sleep -Seconds 5
ping -n 6 192.168.0.154
curl.exe --max-time 5 http://192.168.0.154/
```

Expected HTTP response:

```html
<html><body><h1>VibeOS Ethernet OK</h1><p>lwIP + Zynq GEM0 is running.</p><p>IP: 192.168.0.154</p></body></html>
```

Inside VibeOS, useful network commands are:

```text
ethstat
ssh probe 192.168.0.152:2221
ssh set root@192.168.0.152:2221
ssh auth <password>
ssh exec uname -a
```

SD card root listing is also wired into the FPGA minimal shell. The stable
bitstream enables PS SD0 on MIO 40..45; the OS uses a small PIO SDIO reader and
FAT16/FAT32 root directory parser.

```text
sd status
sd ls
sdls
ls /sd
ls
```

In `profile=fpga_minimal`, plain `ls` falls back to the SD card root when the
temporary `/root` RAM disk has not been formatted yet.

The stable Ethernet loader uses:

```text
project_2/riscv_ps_ddr_hw_eth_clkdomain_probe_50/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit
project_2/riscv_ps_ddr_hw_eth_clkdomain_probe_50/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl
```

Those Vivado build outputs are not tracked in git. Rebuild them locally or copy
them from a known-good local backup before running the stable loader.

## Notes

- Do not commit `vibeos/.build`, `os.bin`, bitstreams, DCPs, or Vivado run
  directories.
- The detailed recovery and debug notes live in
  `project_2/STABLE_UI_ETH_MINIMAL_SOP.md`.
- Current FPGA minimal input/network handling is mostly cooperative polling.
  The PL IRQ probe work starts in `project_2/read_pl_irq_probe.tcl` and the
  fake PLIC/IRQ RTL path in `project_2/pl_mmio_jtag_console.v`.
