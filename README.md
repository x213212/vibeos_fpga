# vibeos_fpga

VibeOS on a Zynq-7020 FPGA development board.

This repository keeps the source-level pieces needed for the current VibeOS
FPGA bring-up:

- `vibeos/`: RISC-V OS, USB HID, SD/VibeFS, GEM/lwIP, SSH, and GUI source
- `project_2/`: FPGA top-level RTL, HDMI/MMIO/USB debug RTL, Vivado Tcl, XSDB
  debug/load scripts, and board SOP notes

Generated artifacts are intentionally not tracked. Rebuild `os.bin` and Vivado
bitstreams locally from the source and scripts.
