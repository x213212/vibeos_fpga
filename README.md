# vibeos_fpga

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
