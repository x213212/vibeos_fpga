# VibeOS USB HID + HDMI SOP

This SOP records the working Zynq Z7-Lite flow for VibeOS HDMI + USB mouse/keyboard.

## Current Preserved Checkpoint

Current checkpoint saved after the keyboard-pointer acceleration hold fix:

```text
H:\testproject\backups\vibeos_kb_mouse_accel_hold_20260514_2254
```

This checkpoint preserves:

- `vibeos/` source, `.build`, `os.bin`, `os.elf`, and `os.window_gui.bin`
- `project_2/` source files, Tcl/PowerShell debug/load scripts, SOP files, and
  HDL files, excluding large Vivado generated cache/run directories
- current hardware artifacts copied separately under `project_2_artifacts/`

The known-good bitstream for keyboard, keyboard-pointer mouse control, HDMI UI,
and SD-backed VibeFS is:

```text
H:\testproject\backups\vibeos_kb_mouse_accel_hold_20260514_2254\project_2_artifacts\riscv_ps_ddr_wrapper.bit
SHA256=399B2707FA999F83C20C2EF328D9A1E9BD2550A09B6EF7CFAA96D6D3F043FC62
```

On May 15, 2026 this known-good bitstream was copied back to the standard
current output path:

```text
H:\testproject\project_2\riscv_ps_ddr_hw\riscv_ps_ddr_hw.runs\impl_1\riscv_ps_ddr_wrapper.bit
```

The generated bitstream that replaced it was saved before overwrite:

```text
H:\testproject\backups\bad_generated_bitstream_20260515_180847\riscv_ps_ddr_wrapper.bit
SHA256=15D3C04D2C6A8446F42FD3AC12F83C893270AC6423382E4505956718FAD17CE1
```

Do not use a newly regenerated bitstream as the baseline unless it passes the
A/B boot checks below.

Expected user-visible behavior for this checkpoint:

- 1024x600 monitor visible area uses VibeOS logical `512x300`
- USB keyboard works from the single PS USB port
- numpad direction keys move the OS cursor, including diagonals:
  `KP7/KP9/KP1/KP3`
- `KP/` is left click, `KP*` is right click
- keyboard-pointer acceleration ramps to maximum and stays there while the
  direction key is held; a single empty HID report must not reset speed

## Bitstream A/B Rule

When UI or keyboard suddenly stops working after hardware or network edits, test
with the same OS binary and only swap the bitstream.

Known-bad generated bitstream symptom:

```text
DBG_I_AR_COUNT=0x00000006
DBG_I_R_COUNT=0x00000030
DBG_MMIO_AW_COUNT=0x00000000
DBG_IOP_AW_COUNT=0x00000000
DBG_LAST_D_AWADDR=0x01c331d8
MOUSE_DEBUG=0x00000001
```

This means the RISC-V is stuck before MMIO/UI/USB init. It is not a keyboard
driver problem.

Known-good bitstream with the same current `vibeos/os.bin`:

```text
DBG_I_AR_COUNT=0x0008974d
DBG_I_R_COUNT=0x0044ba68
DBG_MMIO_AW_COUNT=0x001194f5
DBG_IOP_AW_COUNT=0x00000056
MOUSE_DEBUG=0x03000004
MOUSE_DBG7=0x00000076
```

This proves the current OS can boot when the bitstream is correct. Therefore,
if a regenerated bitstream shows the bad pattern above, restore the known-good
bitstream first and do not chase USB, keyboard, or UI source code.

The hardware source defaults should stay aligned with the checkpoint:

- `project_2/build_riscv_ps_ddr_hw.tcl`: `PCW_FPGA0_PERIPHERAL_FREQMHZ=60`
- `project_2/vibe_hdmi_mmio.v`: display clock constants for 60 MHz input
- Ethernet/GMII changes are opt-in only through `-enable_eth`; default builds
  must remain the keyboard/mouse/UI baseline.
- `project_2/build_riscv_ps_ddr_hw.tcl` now builds into
  `project_2/riscv_ps_ddr_hw_candidate` by default. Do not clobber
  `project_2/riscv_ps_ddr_hw` until the candidate bitstream passes the A/B
  boot checks.

## Known Good Constants

- RISC-V reset vector: `0x80000000`
- PS DDR physical load address: `0x01000000`
- RISC-V DDR remap: `0x80000000 -> 0x01000000`
- USB HID DMA state: fixed EHCI descriptor window in `vibeos/drivers/virtio/virtio_input.c`
- Current USB HID DMA window: CPU `0x851da000`, physical `0x061da000`
- FPGA minimal VibeOS filesystem backend: PS SDIO0 raw block device, with
  RAM disk fallback if SD init fails
- VibeFS raw SD LBA offset: `32768`
- VibeOS block size: `4096` bytes = 8 SD sectors
- HDMI hardware framebuffer size: `512x384`
- HDMI output timing: `1024x768`
- PL FCLK baseline: `60 MHz`
- Current window GUI logical size for the 1024x600 panel: `512x300`
- Expected USB connected/enabled port: `PORTSC1=0x84001405`
- Bad USB no-connect port: `PORTSC1=0x8c001000`
- FPGA minimal timer source: ultraembedded CSR `mtime/mtimeh` (`0xc01/0xc81`)
  and CSR `mtimecmp` (`0x7c0`), not QEMU CLINT MMIO

## Do Not Change For Mouse UI Latency

Do not change these while only fixing cursor flicker or cursor latency:

- `vibeos/start.S` hart parking. Hart 0 is the only hart that may run VibeOS.
- `project_2/riscv_ps_ddr_top.v` DDR remap constants
- USB HID descriptor placement must stay consistent with `ASYNCLIST`; if the
  DMA base changes, update the debug scripts and revalidate QH/qTD retirement.
- HDMI/VRAM hardware format in `project_2/vibe_hdmi_mmio.v`
- HDMI/VRAM format in `project_2/vibe_hdmi_mmio.v`
- `vibeos/timer.c` FPGA path must not use `CLINT_MTIME` or
  `CLINT_MTIMECMP`; the FPGA core exposes the timer through CSRs.
- `vibeos/os.c` FPGA boot path must call `trap_init()` and `timer_init()`
  before returning from `os_start()`.

Mouse display latency can be affected by the UI redraw path and by the FPGA
timer path. If cursor blink, keyboard repeat, USB retry delays, or redraw
throttling look frozen or bursty, validate the CSR timer before touching USB.

## Current SD Card / VibeFS Status

The FPGA minimal block backend is connected through
`vibeos/drivers/virtio/virtio_disk.c`.

Current behavior:

- `virtio_disk_init()` tries PS SDIO0 first.
- If SD init succeeds, VibeOS uses the SD card as a raw block device.
- If SD init fails, VibeOS falls back to the old volatile RAM disk so the UI
  can still boot.
- One VibeOS filesystem block is 4096 bytes, mapped to 8 SD sectors.
- VibeOS block `N` maps to SD LBA `32768 + N * 8`.
- `os_start()` now calls `virtio_disk_init()` before `user_init()` on
  `FPGA_MINIMAL`, so terminal `ls`, `format`, `write`, `cat`, `mkdir`, and
  editor/app FS calls go through the SD-backed block device.

Important:

- This is the native VibeOS custom block filesystem, not FAT16 mounting.
- The existing `project_2/sdio_api.c` and `project_2/fat16_api.c` remain useful
  probe/reference code, but the OS filesystem uses the VibeOS block layer.
- On boot, `fs_ensure_root()` reads VibeOS block 1. If it already contains
  `FS_MAGIC`, the allocator scans and mounts the existing `/root`. If it does
  not contain `FS_MAGIC`, the OS initializes the minimal `/root` directory once
  so `ls`, `write`, `cat`, and `mkdir` work immediately.
- The terminal `format YES_I_AM_SURE` command still exists, but it is only for
  intentionally wiping/rebuilding the VibeOS filesystem region.
- Formatting or first-boot root initialization writes the raw VibeFS region.
  Use an SD card/partition layout reserved for this board; do not use a card
  containing files you need in that region.

## Current USB Keyboard Status

Current test result on May 14, 2026 with the keyboard plugged directly into the
single PS USB port:

```text
USBCMD=0x00080011
PERIODIC=0x061db000
ASYNC=0x00000000
TTCTRL=0x00000000
PORTSC1=0x84001405
MOUSE_DBG5=0x84001405
MOUSE_DBG7=0x75008d80 or 0x79008d80 while keyboard polling is pending
PHYS_KBD_QH[ep_char]=0x00081101
PHYS_KBD_QH[ep_cap]=0x40801c01
PHYS_KBD_QTD[token]=0x80008d00 after a completed 8-byte interrupt IN report
```

Meaning:

- the USB port is connected and enabled
- endpoint-0 enumeration passed device/config descriptor, set address, and set
  config
- the async control schedule is off after enumeration, which is expected
- the periodic interrupt schedule is running for the keyboard endpoint
- the keyboard interrupt QH uses the real port speed (`PORTSC1.PSPD=1`, low
  speed), EP1 IN, MPS=8
- low-level keyboard reports have been observed in `MOUSE_DBG1/MOUSE_DBG2`;
  a report such as `MOUSE_DBG1=0x00260000` means HID usage `0x26` arrived

This is no longer an endpoint-0 enumeration failure. If characters do not show
in the terminal while these values are present, debug the VibeOS input queue,
`gui_key`, active window, and terminal mailbox path.

Expected idle state after a USB keyboard successfully enumerates:

```text
MOUSE_DBG5=0x84001405
MOUSE_DBG7=0x75008d80
MOUSE_DBG3=0x010800..   # EP1 IN, MPS=8, poll counter
```

Keyboard report complete marker after a key press:

```text
MOUSE_DBG7=0x7800....
MOUSE_DBG1/MOUSE_DBG2 contain the raw 8-byte keyboard report
```

Avoid treating a single LED blink as proof of keyboard input. It only proves
power/reset. HID input is proven by `MOUSE_DBG3=0x010800..` plus report
completion after a key press.

To avoid UI stalls while debugging this, VibeOS now:

- waits for stable connect before enumeration
- does not run blocking USB enumeration from the FPGA timer interrupt path
- does not call `usb_mouse_init_hw()` after `usb_mouse.ready` becomes true.
  Ready means the OS must poll the interrupt endpoint only; re-entering
  enumeration here makes the UI clock stop and the keyboard LED blink
  repeatedly.
- uses bounded USB waits so a stalled `FRINDEX` cannot freeze the UI for a long
  time
- stops the async schedule before rebuilding a control-transfer QH/qTD chain
  and stops it again after completion. Rewriting the same QH while the EHCI
  async schedule is still scanning it caused `USBSTS` Host System Error/Halted
  around `SET_ADDRESS`.
- keeps `USB0_TTCTRL=0`; do not write a fake hub number for root-port
  low-speed devices
- does not require `PORTSC.PE` inside `usb_host_prepare()`. A port can be
  connected but not enabled (`0x84001401`); `usb_port_reset()` is what enables it
  and should produce `0x84001405`.

Important: the working mouse binary starts with `csrr a0, mhartid` and parks
non-zero harts. If that code is removed, more than one hart can run the same OS
and corrupt shared USB EHCI QH/qTD/DMA state. The symptom is usually
`MOUSE_DEBUG seq` stuck near `1` and interrupt tokens like `0x...8d40`.

## Cold Recovery Flow

Use this when mouse LED is off, USB is no-connect, or the board was reset.

Preferred one-step flow:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\release_vibeos_with_usb_recover.tcl H:/testproject/vibeos/os.window_gui.bin
Get-Content H:\testproject\project_2\release_vibeos_with_usb_recover.out
```

Success means:

```text
AFTER_RECOVER_PORTSC1=0x84001405
ULPI_WAKE_OK=1
ULPI_DRVVBUS_OK=1
AFTER_RELEASE_PORTSC1=0x84001405
```

This script keeps the PL RISC-V in reset, pulses the external USB3320 reset on
PS MIO46 from XSDB, restores EHCI/ULPI host state, loads VibeOS into DDR, and
then releases the RISC-V.

Manual two-step flow, only if the one-step loader needs debugging:

1. Recover USB PHY through MIO46:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\usb_mio46_recover.tcl
Get-Content H:\testproject\project_2\usb_mio46_recover.out
```

Success means:

```text
AFTER_PORTSC1=0x84001405
ULPI_WAKE_OK=1
ULPI_DRVVBUS_OK=1
```

2. Load VibeOS without resetting USB again:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\release_vibeos_no_usb_reset.tcl H:/testproject/vibeos/os.fpga_minimal.bin
Get-Content H:\testproject\project_2\release_vibeos_no_usb_reset.out
```

Success means:

```text
PORTSC1_BEFORE=0x84001405
PORTSC1_AFTER=0x84001405
DDR_LOAD_ADDR=0x01000000
```

3. Read mouse state:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\read_mouse_debug.tcl
Get-Content H:\testproject\project_2\read_mouse_debug.out
```

Good signs after moving the mouse:

```text
MOUSE_DBG5=0x84001405
MOUSE_DBG7=0x71008d80 or 0x72008d80 while a qTD is posted/pending
MOUSE_DEBUG seq changes while moving
MOUSE_DBG6 changes while moving
MOUSE_DBG1 contains non-zero report bytes, for example 0x00fe0500
```

For the single PS USB port keyboard test, unplug the mouse and plug the USB
keyboard, then load with:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\release_vibeos_with_usb_recover.tcl H:/testproject/vibeos/os.window_gui.bin
```

Good keyboard idle state:

```text
MOUSE_DBG5=0x84001405
MOUSE_DBG7=0x75008d80
MOUSE_DBG3=0x010800..   # EP1 IN, MPS=8, poll counter
```

Keyboard report complete marker after a key press:

```text
MOUSE_DBG7=0x7800....
MOUSE_DBG1/MOUSE_DBG2 contain the raw 8-byte keyboard report
```

`MOUSE_DBG7=0x75008d80` means an interrupt IN qTD is active/pending. That is a
normal idle HID state, not a failure by itself. `MOUSE_DBG7=0x79008d80` means a
fresh keyboard interrupt IN qTD was posted.

The keyboard path maps USB HID usage IDs into the existing Linux-style key
codes used by the VibeOS terminal/UI input path. On FPGA minimal builds,
`virtio_input_poll()` must not return immediately after mouse polling; it must
also drain the input event queue so keyboard reports reach `gui_key`.

`MOUSE_DBG7=0x72008d80` can be normal while an interrupt IN qTD is pending and
the mouse is idle. `MOUSE_DBG7=0x70008d40` means the interrupt qTD halted. If
`MOUSE_DEBUG seq` is also stuck, the UI will not move even though enumeration
already reached ready.

Endpoint recovery markers:

```text
MOUSE_DBG7=0x7300....  interrupt IN qTD halted repeatedly; OS clears only the interrupt endpoint/QH
MOUSE_DBG7=0x7400....  interrupt IN qTD stayed active too long; OS clears only the interrupt endpoint/QH
```

These paths must not reset the full USB controller and must not touch MIO46.
The next poll posts a fresh interrupt IN qTD.

## Interrupt Polling Rule

The working interrupt endpoint flow is:

- Start the periodic schedule once after `usb_intr_qh_init()`.
- On each poll, create a fresh interrupt IN qTD.
- Update `intr_qh.overlay.next`, clear the QH overlay token, write back the qTD,
  write back the QH and periodic list, then set `USBCMD` run + periodic enable.
- Use the real enumerated port speed for interrupt QHs. Do not force non-high
  speed endpoints to full-speed; low-speed keyboards need `QH.EPS=1`.
- Do not call `usb_periodic_start()` on every poll. That function stops and
  restarts the periodic schedule; doing that repeatedly can make the next
  interrupt qTD return halted (`0x...8d40`) and leave report bytes at zero.

The relevant source is `vibeos/drivers/virtio/virtio_input.c`.

## Full Bitstream/PS Reload Flow

Use full reload only when the bitstream was rebuilt or the FPGA content is unknown.

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\load_vibeos_and_console.tcl H:/testproject/vibeos/os.fpga_minimal.bin
```

If full reload leaves USB at:

```text
PORT=0x8c001000
```

then immediately run the cold recovery flow above. Full reload can disturb USB PHY/VBUS; `usb_mio46_recover.tcl` is the recovery step.

## OS Recovery Boundary

VibeOS may reset EHCI controller state, clear async/periodic schedules, rebuild
QH/qTD endpoint state, and issue ULPI soft wake/DRVVBUS writes.

VibeOS must not directly pulse USB3320 reset through PS GPIO/MIO46. A test that
accessed `0xE000A044`, `0xE000A244`, and `0xE000A248` from the PL RISC-V
stalled during boot around `usb_mouse_mark(10)`. Hard PHY reset stays in
`release_vibeos_with_usb_recover.tcl` / `usb_mio46_recover.tcl` unless the
hardware design later exposes a safe PL MMIO reset bit.

At VibeOS startup, do not run a full controller reset after the loader has
already recovered the PHY. The working OS path only calls `usb_host_prepare()`
before enumeration. Full reset during OS startup stalled at `MOUSE_DBG7=0x0a`.

## HDMI/CPU Boot Check

Good CPU boot after loading:

```text
I0=0xf1402573
LAST_I_PS_ARADDR=0x010...
```

This first instruction is the `mhartid` read used to park non-zero harts.

Bad mouse build:

```text
I0=0x00010117
```

This usually means `vibeos/start.S` no longer parks non-zero harts. USB mouse
enumeration can appear ready, but interrupt reports can stop or halt because
multiple harts touch the same global USB DMA structures.

FPGA minimal startup must clear BSS through the uncached physical DDR alias:

```text
cached CPU BSS: 0x801d4000
physical DDR:   0x011d4000
alias offset:   0x7f000000
```

Clearing BSS through the cached alias can stop before `os_main()`; debug then
shows no USB markers, no MMIO writes, and the last D-side read around
`0x801d4000`. `vibeos/start.s` therefore subtracts `0x7f000000` from
`_bss_start/_bss_end` for the FPGA minimal BSS clear loop.

Debug scripts must resume the ARM target after memory reads. If a script leaves
ARM0 suspended, the OS is paused and keyboard/mouse input will appear dead even
though USB is ready. `read_mouse_debug.tcl` and `read_pl_cpu_debug_live.tcl`
must end with `catch {con}` before `disconnect`.

Bad DDR/bitstream mismatch:

```text
I0=0xdec0de1c
LAST_I_PS_ARADDR=0x000...
```

If this appears, the bitstream and loader DDR address do not match. Rebuild or reload the bitstream that maps RISC-V `0x80000000` to PS DDR `0x01000000`.

## PS AXI Routing Requirement

The working hardware routes RISC-V memory and PS register access through two
different PS AXI ports:

- PS DDR / OS instruction and data fetch: `processing_system7_0/S_AXI_HP0`
- PS IOP / USB registers such as `0xE0002140`: `processing_system7_0/S_AXI_GP0`

Do not collapse these back to a single port. A pure GP0 route can stall CPU
instruction fetch. A pure HP0 route can let the CPU boot but break USB register
writes.

Good hardware address map contains both segments for `riscv_0/M_AXI_I` and
`riscv_0/M_AXI_D`:

```text
SEG_processing_system7_0_HP0_DDR_LOWOCM 0x00000000 range 512M
SEG_processing_system7_0_GP0_IOP        0xE0000000 range 4M
```

## Cursor Latency Fix

The cursor latency/rendering fix is:

- Keep `vbuf` as the content layer only: desktop, windows, terminal text, and
  terminal caret.
- Draw the mouse cursor as a HDMI VRAM overlay from `vga_present_cursor()`;
  do not draw it into `vbuf`.
- Before presenting a new cursor position, `vga_present_cursor()` restores the
  previous cursor rectangle from `vbuf` through `vga_update_rect()`, then draws
  the cursor pixels directly to VRAM and updates `vram_shadow`.
- Because the cursor is not part of `vbuf`, terminal caret blink and mouse
  cursor motion are independent. Caret redraw changes the content layer; cursor
  redraw restores/overlays only the cursor rectangle.
- `vga.c` keeps a software front-buffer shadow (`vram_shadow`) for FPGA
  minimal builds. `vga_update()` and `vga_update_rect()` compare packed
  4-pixel words from `vbuf` against this shadow and write HDMI VRAM only when
  the word changed.
- This means a full GUI redraw may still rebuild the software back buffer, but
  the HDMI MMIO bus only receives changed pixels. It is safer than scattering
  ad-hoc redraw shortcuts through every widget.
- Do not flush cursor rects immediately from the USB/input path. Input updates
  state only; `gui_task()` owns rendering. Cursor-only overlay refresh has its
  own 8 ms pacing; full UI/window redraw remains 16 ms.
- For terminal input/caret blinking, redraw the software framebuffer normally
  but flush only the active window rectangle, taskbar, and cursor rectangles.
- Keyboard input uses a small `gui_key` queue. USB/input polling may collect a
  burst of key events, while `gui_task()` drains a bounded batch per frame.
  This avoids losing keys or delaying later keys behind a single-character
  `gui_key` slot.
- The USB poll task calls `virtio_input_poll()` after `usb_mouse_poll()` on
  FPGA minimal builds. This moves HID report conversion into the input task
  instead of waiting for the next GUI task iteration.
- After a keyboard or mouse interrupt qTD completes, the driver immediately
  posts the next interrupt IN qTD in the same poll call. Do not add a `return`
  after `usb_keyboard_apply_report()` or `usb_mouse_apply_report()`; that
  reintroduces a scheduler gap between HID reports.
- For normal terminal editing keys, `gui_task()` yields once to the terminal
  worker before drawing. This lets the terminal update its prompt buffer before
  the 60 Hz dirty-rect flush, avoiding a one-frame visual lag.
- Current faster path: on FPGA minimal builds, normal terminal edit keys
  (printable ASCII, Backspace, Left/Right, Up/Down history, Delete, Home, End)
  are processed synchronously in `gui_task()` through `handle_window_mailbox()`.
  The GUI then redraws only the active terminal window and presents the cursor
  overlay immediately. This bypasses the 16 ms full-frame limiter for ordinary
  typing. Enter, Tab, Ctrl+C, paste, and command execution still use the worker
  path because they can run heavier shell logic.
- USB numpad pointer control is handled from the current HID report, not from
  Linux key repeat events. The pointer usages `0x55..0x63` are filtered out of
  the terminal key queue so they do not both type characters and move the
  cursor.
- Correct numpad pointer mapping:
  `KP4/KP1/KP7 -> left`, `KP6/KP3/KP9 -> right`,
  `KP8/KP7/KP9 -> up`, `KP2/KP1/KP3 -> down`,
  `KP/ -> left click`, `KP* -> right click`, `KP-` scroll.
- Keyboard-pointer acceleration is time-based in milliseconds with fractional
  pixel accumulation. It starts at roughly `320 px/s`, ramps to roughly
  `900 px/s` over 600 ms, and then stays at that maximum while the direction
  key remains held. Shift forces roughly `1200 px/s`. Do not use a raw
  "every N polls move M pixels" divider; it makes motion speed depend on UI
  redraw load and creates bursty movement.
- The keyboard pointer integrator runs every 4 ms and clamps long elapsed
  gaps to 16 ms. If it moves in visible chunks, check that cursor-only refresh
  is still independent from `last_gui_frame_ms`; tying cursor motion to the
  16 ms full-frame limiter causes 10+ pixel jumps at high pointer speed.
- Keyboard pointer acceleration must not reset on a single empty keyboard HID
  report. Keep the last direction for a short 48 ms grace window and only reset
  acceleration after 120 ms without pointer direction. Otherwise holding a
  numpad direction can ramp up, receive one empty report, and restart from the
  slow initial speed.

Files:

- `vibeos/drivers/virtio/virtio_input.c`
- `vibeos/user_gui.c`
- `vibeos/user_internal.h`
- `vibeos/user.c`
- `vibeos/vga.c`
- `vibeos/vga.h`

Do not optimize latency by drawing directly from `virtio_input_poll()` or by
calling `vga_update_rect()` for every raw USB report. That makes pointer/input
motion bypass the frame limiter and causes visibly uneven speed. The intended
model is input queue -> GUI state -> 60 Hz dirty-rect flush -> shadow-buffer
diff present.

## Window GUI Boot

For the window manager / terminal build, build and load:

```powershell
wsl --cd /mnt/h/testproject/vibeos --exec make FPGA_MINIMAL=1 os.bin
wsl --cd /mnt/h/testproject/vibeos --exec riscv64-unknown-elf-objcopy --remove-section=.apppt -O binary os.elf os.window_gui.bin
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\usb_mio46_recover.tcl
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\release_vibeos_no_usb_reset.tcl H:\testproject\vibeos\os.window_gui.bin
```

The FPGA minimal block smoke test must not hard-panic before `user_init()`.
If the ramdisk smoke check fails, log a warning and still start the window
manager so mouse polling, terminal creation, and dragging can run.

Window dragging is handled in `vibeos/user_gui.c`:

- click title bar to begin dragging
- dragging a maximized window restores it under the cursor first
- geometry redraw happens only when position or size changes
- under `FPGA_MINIMAL`, `gui_task()` creates one terminal window at boot
- under `FPGA_MINIMAL`, right click is a no-op placeholder; it must not paste
  or inject terminal input

For the 1024x600 monitor, VibeOS uses a 512x300 logical GUI so the 2x-scaled
visible area fits the panel height. The HDMI hardware still generates 1024x768;
only the software UI layout is constrained to the 1024x600 visible region.

## Current Runnable Binary Notes

Build from source:

```powershell
wsl --cd /mnt/h/testproject/vibeos --exec make FPGA_MINIMAL=1 os.bin
Copy-Item H:\testproject\vibeos\os.bin H:\testproject\vibeos\os.hartfix.bin -Force
```

Load the freshly built binary without resetting USB:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\release_vibeos_no_usb_reset.tcl H:/testproject/vibeos/os.hartfix.bin
```

Current practical binaries:

- Known-good USB driver binary:
  `H:\testproject\backups\usb_mouse_periodic_fixed_20260509\bin\os.fpga_minimal.bin`
- Current rebuilt source binary with hart parking restored:
  `H:\testproject\vibeos\os.hartfix.bin`
- Current window manager / terminal binary:
  `H:\testproject\vibeos\os.window_gui.bin`

When using either binary, preserve USB state with `release_vibeos_no_usb_reset.tcl` after `usb_mio46_recover.tcl`.
