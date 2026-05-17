# VibeOS Backup / Restore SOP

Updated: 2026-05-14

Workspace:

```text
H:\testproject
```

Current known-good checkpoint:

```text
H:\testproject\backups\vibeos_kb_mouse_accel_hold_20260514_2254
```

## What This Checkpoint Represents

This checkpoint preserves the VibeOS HDMI + USB HID keyboard state after the
keyboard-controlled mouse acceleration hold fix.

Expected behavior:

- HDMI panel visible area: 1024x600.
- VibeOS logical GUI visible area: 512x300.
- USB keyboard works through the single PS USB port.
- Numpad direction keys move the cursor.
- Diagonal numpad movement is preserved with `KP7`, `KP9`, `KP1`, and `KP3`.
- `KP/` is left click.
- `KP*` is right click.
- Keyboard pointer acceleration ramps to max and stays at max while a direction
  key is held.

## Included In The Backup

- `vibeos/`: full source tree, `.build`, `os.bin`, `os.elf`,
  `os.window_gui.bin`, and disk image files.
- `project_2/`: HDL source, Tcl/PowerShell scripts, SOP files, debug scripts,
  and project support files.
- `project_2_artifacts/`: current `riscv_ps_ddr_wrapper.bit` and
  `riscv_ps_ddr.hwdef`.

Large Vivado generated folders are not copied wholesale:

- `.Xil`
- `project_2.cache`
- `project_2.hw`
- `project_2.ip_user_files`
- `project_2.runs`
- `project_2.sim`
- `riscv_ps_ddr_hw`
- `xilinx_tmp`
- `xsim.dir`

The required current bitstream and hwdef are copied separately under
`project_2_artifacts/`.

## Restore This Checkpoint

Run from PowerShell:

```powershell
$SRC = "H:\testproject\backups\vibeos_kb_mouse_accel_hold_20260514_2254"

robocopy "$SRC\vibeos" H:\testproject\vibeos /E
robocopy "$SRC\project_2" H:\testproject\project_2 /E

Copy-Item "$SRC\project_2_artifacts\riscv_ps_ddr_wrapper.bit" `
  "H:\testproject\project_2\riscv_ps_ddr_hw\riscv_ps_ddr_hw.runs\impl_1\riscv_ps_ddr_wrapper.bit" -Force

Copy-Item "$SRC\project_2_artifacts\riscv_ps_ddr.hwdef" `
  "H:\testproject\project_2\riscv_ps_ddr_hw\riscv_ps_ddr.hwdef" -Force
```

Then load the OS:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' `
  H:\testproject\project_2\load_vibeos_quick.tcl `
  H:/testproject/vibeos/os.bin
```

## Rebuild OS If Needed

```powershell
wsl --cd /mnt/h/testproject/vibeos --exec make FPGA_MINIMAL=1 os.bin
Copy-Item H:\testproject\vibeos\os.bin H:\testproject\vibeos\os.window_gui.bin -Force
```

## Do Not Change For Simple Input/UI Fixes

- Do not move the USB HID DMA descriptor window unless all debug scripts and
  EHCI QH/qTD physical addresses are revalidated.
- Do not change `vibeos/start.S` hart parking.
- Do not change the FPGA timer path in `vibeos/timer.c` back to QEMU CLINT
  MMIO; the FPGA build uses CSR timer registers.
- Do not change HDMI framebuffer format in `project_2/vibe_hdmi_mmio.v` for a
  keyboard/mouse UI fix.

## Main Detailed SOP

The detailed USB/HDMI behavior record is:

```text
H:\testproject\project_2\USB_MOUSE_HDMI_SOP.md
```

