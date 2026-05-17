# Z7-LITE USB Mouse Bring-up Notes

Date: 2026-05-06

## Current Status - 2026-05-09

Mouse-only VibeOS is verified working at the USB report path with HDMI disconnected.

Latest good sample:

```text
PORTSC1=0x84001405          connected + enabled
USBCMD=0x00080031           run + async + periodic enabled
USBSTS=0x000cc08b/0x000ce08b
INTR_QH_01=0x00070101       EP1, MPS=7, EPS=FS, RL=0, DTC=0
INTR_QH_02=0x40801c01       mult=1, root-port TT port=1, S-mask=1, C-mask=0x1c
QTD3_INTR_02=0x00078d80     pending interrupt IN, 7-byte report buffer
MOUSE_DBG7=0x70008d00       normal interrupt completion observed
MOUSE_DEBUG seq changed     report path updates mouse state
MOUSE_DBG6 changed          VibeOS gui_mx/gui_my changed
```

Critical findings:

- The Z7-LITE USB3320 external reset is `MIO46` (`OTG_nRST`). Do not use MIO8.
- Use `project_2/usb_mio46_recover.tcl` before loading VibeOS if USB state is uncertain.
- Use `project_2/release_vibeos_no_usb_reset.tcl` to load the OS after recovery. It must not reset USB again.
- Enumeration can use `PORTSC1.PSPD`, but the interrupt QH must use EHCI `EPS=FS` (`0`) for this mouse. Using `EPS=LS` (`1`) caused interrupt qTD halt (`...8d40`) even though enumeration worked.
- Interrupt QH must match Linux EHCI behavior: no QH toggle-from-qTD for interrupt (`DTC=0`) and no Nak reload (`RL=0`). Control QH still uses DTC and RL.
- Keep periodic enabled during pending interrupt IN. Do not use control `GET_REPORT` as a timeout fallback; it can hide the real periodic schedule state and stall progress.
- The real endpoint descriptor is `81 03 07 00 0a`: EP1 IN interrupt, MPS=7, interval=10.
- `MOUSE_DBG7=0x7200xxxx` means the interrupt qTD is still active. That is not automatically a failure.
- `MOUSE_DBG7=0x70008d00` is the desired normal completion. `0x70008d40` means halted.

Current load/test sequence:

```powershell
wsl -d Ubuntu --cd /mnt/h/testproject/vibeos --exec /usr/bin/make FPGA_MINIMAL=1 os.bin
wsl -d Ubuntu --cd /mnt/h/testproject/vibeos --exec riscv64-unknown-elf-objcopy --remove-section=.apppt -O binary os.elf os.fpga_minimal.bin
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\usb_mio46_recover.tcl
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\release_vibeos_no_usb_reset.tcl H:/testproject/vibeos/os.fpga_minimal.bin
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\sample_mouse_debug.tcl
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\dump_usb_ehci_live.tcl
```

## Current Status - 2026-05-08

Do not debug the UI until the USB control qTD retires again.

Latest verified good board-level state before the DAP/JTAG failure:

```text
USB_PHY_RESET_RELEASE MIO46=0x00001600 ...
USB0_HOST_RELEASE ... PORT=0x84001405 MODE=0x00000003 ... ULPI_OK=1

SBUSCFG=0x00000007
USBCMD=0x00080021
USBSTS=0x0000a088
ASYNCLIST=0x061da000
PORTSC1=0x84001405
USBMODE=0x00000003

ASYNC_HEAD_00=0x061da082
ASYNC_HEAD_01=0x4040e000
ASYNC_HEAD_02=0x40000000
ASYNC_HEAD_06=0x00000040

CTRL_QH_00=0x061da002
CTRL_QH_01=0x48085000
CTRL_QH_02=0x40800000
CTRL_QH_04=0x061da180

QTD0_SETUP_02=0x00080e80
QTD1_DATA_02=0x80080d80
QTD2_STATUS_02=0x80008c80
SETUP_PKT_00=0x01000680
SETUP_PKT_01=0x00080000
```

Meaning:

- The board/PHY/VBUS side is up: `PORTSC1=0x84001405`, not no-connect.
- EHCI async schedule is enabled: `USBSTS.ASS=1`, `USBCMD.ASE=1`.
- DDR descriptors are visible and correctly linked at fixed physical base `0x061da000`.
- The blocker remains that the first control transfer qTDs stay `ACTIVE`; the controller is not retiring the async control transfer.
- A fixed DMA base was restored to the previously successful region:
  - CPU virtual: `0x851da000`
  - USB/PS physical: `0x061da000`
  - Size reserved from heap: `0x10000`
- Because the fixed DMA region is not linker `.bss`, the driver must explicitly clear it before reading `ready` or `intr_pending`. This is now done by `usb_mouse_ensure_state()`.

Current external blocker:

```text
DAP (JTAG port open error. AP transaction error, DAP status 0x30000021)
```

When this appears, XSDB cannot select `ARM Cortex-A9 MPCore #0`; therefore it cannot run `ps7_init`, download VibeOS to DDR, or read USB registers. USB debugging must pause until the ARM DAP target comes back. Killing `hw_server`, lowering JTAG frequency, JTAG TAP reset, and PL reprogramming were tried; they did not restore the DAP. The next recovery step is a real board/PS power-cycle or host-side JTAG stack reset.

Keep these current code rules:

- Loader owns Z7-LITE USB3320 reset through `MIO46`.
- VibeOS must not touch SLCR or MIO reset registers.
- VibeOS USB controller mode stays `USBMODE=0x00000003` until `0x13` is tested from a working DAP session; the `0x13` stream-disable experiment was reverted because it was never loaded.
- `SBUSCFG=0x00000007`.
- FS/LS QH endpoint capabilities use embedded root-port TT port number 1: `CTRL_QH_02=0x40800000`.
- QH Nak reload is `RL=4`, matching the previously successful `CTRL_QH_01` shape.

## Previous Status

The USB PHY / host-connect layer is fixed. The mouse cursor still does not move. The current blocker is **EHCI schedule consumption**, not board wiring, USB3320 reset, VBUS enable, or UI cursor drawing.

Latest known live debug after the 2026-05-06 cleanup:

```text
USB0_HOST_RELEASE ... PORT=0x84001405 ... ULPI_OK=1

MOUSE_DEBUG=0x0300000a / 0x03000010
MOUSE_DBG5=0x84001405
MOUSE_DBG7=0x00000083 or 0x00000014
MOUSE_DBG0=0x00080e80
MOUSE_DBG1=0x80080d80
MOUSE_DBG2=0x80008c80
MOUSE_DBG3=0x0000a088
MOUSE_DBG4=0x00080021
MOUSE_DBG6=0x48085000
```

Meaning:

- `PORTSC1=0x84001405`: USB port is connected and enabled.
- `ULPI_OK=1`: ULPI viewport transaction completed.
- `MOUSE_DBG7=0x82/0x83`: `GET_DESCRIPTOR(8)` failed after LS/FS retry.
- `MOUSE_DBG0..2` are setup/data/status qTD tokens from `usb_control()` failure path.
- `MOUSE_DBG4=0x00080021`: `USBCMD` has run + async schedule enabled.
- qTD tokens still contain active bits, so the controller is not consuming the async control transfer.

Earlier, a previous version did reach:

```text
MOUSE_DBG7=0x00000064  mouse enumeration ready
MOUSE_DBG7=0x71008d80  first interrupt IN qTD submitted
```

That proves descriptor parsing and HID endpoint detection have worked before. The current blocker is back at async control schedule consumption, before interrupt polling.

Current live EHCI dump after the 2026-05-06 QH cleanup:

```text
SBUSCFG=0x00000007
CTRLDSSEG=0x00000000
USBCMD=0x00080021
USBSTS=0x0000a088
ASYNCLIST=0x0608a000
PORTSC1=0x84001405
USBMODE=0x00000003

ASYNC_HEAD_00=0x0608a082
ASYNC_HEAD_01=0x4040e000
ASYNC_HEAD_02=0x40000000
ASYNC_HEAD_06=0x00000040

CTRL_QH_00=0x0608a002
CTRL_QH_01=0x48085000
CTRL_QH_02=0x40800000
CTRL_QH_04=0x0608a180

QTD0_SETUP_02=0x00080e80
QTD1_DATA_02=0x80080d80
QTD2_STATUS_02=0x80008c80
SETUP_PKT_00=0x01000680
SETUP_PKT_01=0x00080000
```

Interpretation:

- The async list is running (`USBSTS.AS=1`) and is not halted (`USBSTS.HCH=0`).
- The dummy async head and control QH are a valid circular list: `0x0608a000 -> 0x0608a080 -> 0x0608a000`.
- `CTRLDSSEG=0`, so high 32-bit descriptor addressing is not the blocker.
- Control qTDs still keep `ACTIVE`; the host controller is not retiring the first control transfer.
- A temporary test that copied the first active qTD into the QH overlay also did not change the overlay token. That points to host/DMA schedule fetch or controller bring-up, not HID parser or UI.

## Critical Hardware Finding

Z7-LITE USB3320 reset is **not MIO8**.

From `Z7-LITE_R11.pdf`:

```text
USB3320 RESETB -> OTG_nRST
OTG_nRST -> PS_MIO46_501
USB3320 CPEN -> USB_OTG_CPEN -> TPS2051BDBV EN
```

Important correction:

- `MIO8` is `PS_KEY1`.
- `MIO46` is `OTG_nRST`.
- Do not use PS key / MIO8 as USB reset.

## Correct PS7 Configuration

`project_2/build_riscv_ps_ddr_hw.tcl` must keep these settings:

```tcl
CONFIG.PCW_USB0_PERIPHERAL_ENABLE {1}
CONFIG.PCW_EN_USB0 {1}
CONFIG.PCW_USB0_USB0_IO {MIO 28 .. 39}
CONFIG.PCW_USB0_PERIPHERAL_FREQMHZ {60}

CONFIG.PCW_USB0_RESET_ENABLE {0}
CONFIG.PCW_USB_RESET_ENABLE {0}

CONFIG.PCW_GPIO_MIO_GPIO_ENABLE {1}
CONFIG.PCW_GPIO_MIO_GPIO_IO {MIO}
CONFIG.PCW_MIO_46_DIRECTION {inout}
CONFIG.PCW_MIO_46_IOTYPE {LVCMOS 3.3V}
CONFIG.PCW_MIO_46_PULLUP {enabled}
CONFIG.PCW_MIO_46_SLEW {slow}
```

Why:

- USB0 uses ULPI on `MIO28..39`.
- USB3320 reset is board-specific on `MIO46`.
- Vivado's old USB reset setting was wrong when it targeted `MIO8`.
- MIO46 is controlled as PS GPIO from the loader.

## Correct Loader Behavior

`project_2/load_vibeos_quick.tcl` owns USB3320 reset release.

Correct sequence:

```text
ps7_init
ps7_post_config
configure MIO46 as GPIO
drive MIO46 low
delay
drive MIO46 high
USB controller reset
USBMODE = host
CONFIGFLAG = 1
OTGSC = VBUS/ID support bits
ULPI wake
ULPI write USB3320 OTG Control Set 0x0b = 0x60
PORTSC1 port power
USBCMD run
download VibeOS
release RISC-V CPU
```

Expected loader output:

```text
USB_PHY_RESET_RELEASE MIO46=0x00001600 ... DIRM1=0x00004000 OEN1=0x00004000
USB0_HOST_RELEASE ... PORT=0x84001405 ... ULPI=0x280b0060 ULPI_OK=1
```

If output instead shows:

```text
PORT=0x8c001000
ULPI_OK=0
ULPI read/write timeout
```

then the failure is back at PHY/reset/VBUS level.

## What VibeOS Must Not Do

The RISC-V CPU must not access SLCR registers such as:

```text
0xF8000008  SLCR unlock
0xF80007B8  MIO46 pin config
```

Reason:

- The current PL RISC-V AXI mapping does not include the `0xF8000000` SLCR region.
- When VibeOS tried to write `0xF8000008`, the CPU got stuck repeatedly issuing writes there.

Confirmed bad live debug from that broken version:

```text
DBG_LAST_D_AWADDR=0xf8000008
DBG_LAST_D_WDATA=0x0000df0d
```

Therefore:

- XSDB/PS loader handles MIO46 reset.
- VibeOS only handles USB controller registers in the mapped `0xE0000000` IOP range.

## Correct VibeOS Host Init Scope

`vibeos/os.c` may touch:

```text
0xE0002140 USB0_USBCMD
0xE0002144 USB0_USBSTS
0xE0002150 USB0_CTRLDSSEG
0xE0002154 USB0_PERIODICLISTBASE
0xE0002158 USB0_ASYNCLISTADDR
0xE000215C USB0_TTCTRL
0xE0002160 USB0_BURSTSIZE
0xE0002170 USB0_ULPI_VIEWPORT
0xE0002180 USB0_CONFIGFLAG
0xE0002184 USB0_PORTSC1
0xE00021A4 USB0_OTGSC
0xE00021A8 USB0_USBMODE
```

It must not configure PS MIO or SLCR.

Current rule after debugging:

- The XSDB loader owns USB3320 reset and PS-side PHY bring-up.
- VibeOS should not reset MIO/SLCR.
- VibeOS may set USB controller host mode/configflag/run and may build EHCI QH/qTD schedules.
- Be careful with controller reset in VibeOS: repeated controller reset can leave async qTDs active/not fetched on the current board state.

## Current Software Cleanup

These changes are intentional and should be kept for the FPGA minimal profile:

- `vibeos/user_utils.c`: `lib_printf()` is a no-op under `FPGA_MINIMAL`.
  - Reason: CPU was observed stuck in `user_utils.c:638` (`lib_printf("%s")` path), preventing mouse poll re-entry.
- `vibeos/lib.c`: UART output has bounded wait under `FPGA_MINIMAL`.
  - Reason: console/JTAG output must not block UI or polling.
- `vibeos/user.c`: FPGA minimal `user_init()` starts only GUI + mouse poll task, then returns.
  - Reason: terminal/app/netsurf tasks are unrelated to USB mouse bring-up and were observed leading into `panic/abort`.
- `vibeos/os.c`: FPGA minimal returns after `mbedtls_os_init()`, `user_init()`, `trap_init()`, and `plic_init()`.
  - Reason: avoid full-profile reinitialization and `page_test()` while validating mouse.
- `vibeos/drivers/virtio/virtio_input.c`: interrupt QH `ep_cap` must include:

```text
0x40801c01 for FS/LS interrupt endpoint:
  bit 30      mult=1
  bit 23      root-port TT
  bits 10..12 C-mask
  bit 0       S-mask
```

Do not regress it to `0x00001c01`.

- QH `EPS` must use EHCI encoding, not raw `PORTSC1[PSPD]`:

```text
QH.EPS = 00 Full-Speed
QH.EPS = 01 Low-Speed
QH.EPS = 10 High-Speed
```

Linux `ehci-q.c` also treats full-speed as "do not set bit 12" and low-speed as "set bit 12". The current driver tries full-speed first, then low-speed if `GET_DESCRIPTOR(8)` fails.

- Async control schedule should use the Linux-style ring:

```text
dummy async head QH:
  H-bit set
  horizontal link -> real control QH
  overlay halted/inactive

real control QH:
  horizontal link -> dummy async head
  overlay.next -> first setup qTD
  overlay.token -> inactive

ASYNCLISTADDR -> dummy async head physical address
```

Do not replace this with a single active QH as the reclamation head unless a later register dump proves that this controller requires it.

- With `OTGSC.HAAR` enabled, do not manually set `PORTSC1` reset in VibeOS. UG585 says hardware starts reset after connect when HAAR is set. VibeOS now waits for `PORTSC1.PE` and does not write port reset.

## Debug Register Meaning

Important mouse debug markers:

```text
MOUSE_DBG7=0x78        no connect, PORTSC1 CCS is 0
MOUSE_DBG7=0x14        connect detected, before/around port reset
MOUSE_DBG7=0x82/0x83   GET_DESCRIPTOR(8) failure/retry path
MOUSE_DBG7=0x64        mouse enumeration ready
MOUSE_DBG7=0x7100xxxx  interrupt IN qTD submitted / poll path active
MOUSE_DBG7=0x7200xxxx  poll saw active token and skipped
```

Useful registers:

```text
MOUSE_DBG5   latest PORTSC1 in several paths
MOUSE_DBG7   high-level driver stage
MOUSE_DBG8   qTD token in some failure paths
MOUSE_DBG9   report bytes in some poll paths
MOUSE_DBG10  report bytes / token depending path
MOUSE_DBG11  endpoint / MPS / poll return packing
MOUSE_DBG13  PORTSC1 during poll
MOUSE_DBG14  cursor x/y packing in poll path
```

## Current Next Debug Target

Since current state is:

```text
PORTSC1=0x84001405
MOUSE_DBG7=0x82/0x83 or 0x14
USBCMD=0x00080021
setup/data/status qTD tokens still active
```

the next work item is to inspect and fix:

1. Why the async QH/qTD control transfer is not being fetched even though `USBCMD` has async enabled.
2. Check PS USB host/DMA access path to PS DDR at `0x0608a000`.
3. Verify whether the current RISC-V cache writeback CSR actually writes dirty descriptor lines to PS DDR, not only to a local cache view.
4. Compare Linux ChipIdea/Zynq host init for any missing reset/host-mode bits beyond `USBMODE`, `CONFIGFLAG`, `SBUSCFG`, `CTRLDSSEG`, `TTCTRL`, `BURSTSIZE`, `ASYNCLIST`, and `USBCMD`.
5. Only after `MOUSE_DBG7=0x64` returns, resume interrupt IN / periodic polling debug.

Do not go back to MIO8, PS key, random jumper changes, or HID parser debugging unless `PORTSC1` falls back to no-connect or `MOUSE_DBG7` falls back below enumeration.

## Useful Commands

Build OS:

```powershell
wsl --cd /mnt/h/testproject/vibeos --exec /usr/bin/make FPGA_MINIMAL=1 os.bin
wsl --cd /mnt/h/testproject/vibeos --exec riscv64-unknown-elf-objcopy --remove-section=.apppt -O binary os.elf os.fpga_minimal.bin
```

Rebuild bitstream / PS7 config:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\vivado.bat' -mode batch -source H:\testproject\project_2\build_riscv_ps_ddr_hw.tcl
```

Load current bitstream and OS:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\load_vibeos_quick.tcl H:/testproject/vibeos/os.fpga_minimal.bin
```

Read mouse debug without reflashing:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\read_mouse_debug.tcl
Get-Content H:\testproject\project_2\read_mouse_debug.out
```

Read live CPU debug without reflashing:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\read_pl_cpu_debug_live.tcl
Get-Content H:\testproject\project_2\read_pl_cpu_debug_live.out
```

Do not use `read_pl_cpu_debug.tcl` for live checking because it reflashes the FPGA and reruns `ps7_init`.

Dump live EHCI descriptor state:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\dump_usb_ehci_live.tcl
Get-Content H:\testproject\project_2\dump_usb_ehci_live.out
```

## External References Checked

- AMD UG585: Zynq USB host is EHCI-compatible and has embedded TT support for FS/LS host mode.
- AMD UG585 USBSTS: `AS=bit15`, `PS=bit14`, `RCL=bit13`, `HCH=bit12`, `SEI=bit4`, `UEI=bit1`, `UI=bit0`.
- AMD UG585 QH DWords: `QH.EPS` uses EHCI encoding; `00=FS`, `01=LS`, `10=HS`; control endpoint flag must be set for non-HS control endpoints.
- Linux `drivers/usb/host/ehci-q.c`: control/bulk/interrupt transfers use QH/qTD lists; `qh_update()` primes the QH overlay with the first qTD pointer while keeping the overlay token inactive.
- `vanbwodonk/zynq_z7lite_training`: useful for board-level Z7-LITE context, but the Tutorial PS7 examples do not enable USB0 host (`PCW_EN_USB0=0` / USB0 disabled), so they are not a working USB mouse host reference.
