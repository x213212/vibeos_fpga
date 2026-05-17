# Stable UI + Minimal Ethernet SOP

Date: 2026-05-17

This is the only allowed direction for adding Ethernet to the working VibeOS UI
profile.

## Goal

Use the already working keyboard/mouse/UI OS path and add Ethernet in the
smallest possible steps:

1. keep the known-good bitstream as rollback only
2. keep the keyboard/mouse/UI source path unchanged
3. build a separate Ethernet candidate bitstream with only `-enable_eth`
4. confirm the candidate wrapper really exposes GEM0 EMIO/MDIO pins
5. confirm the PHY can be read
6. confirm the UI still boots and accepts keyboard input
7. use the current GEM DMA/lwIP driver without touching UI/input code

Do not overwrite the known-good UI bitstream path. Ethernet work must use a
separate output directory.

## Known-Good Baseline

Known-good UI rollback bitstream:

```text
H:\testproject\backups\vibeos_kb_mouse_accel_hold_20260514_2254\project_2_artifacts\riscv_ps_ddr_wrapper.bit
SHA256=399B2707FA999F83C20C2EF328D9A1E9BD2550A09B6EF7CFAA96D6D3F043FC62
```

Known-good OS path:

```text
H:\testproject\vibeos
H:\testproject\vibeos\os.bin
```

Expected baseline debug after boot:

```text
DBG_MMIO_AW_COUNT != 0
DBG_IOP_AW_COUNT  != 0
DBG_I_AR_COUNT    keeps increasing
MOUSE_DBG7 must not start with 0xee
```

Bad regenerated-bitstream signature:

```text
DBG_I_AR_COUNT=0x00000006
DBG_I_R_COUNT=0x00000030
DBG_MMIO_AW_COUNT=0x00000000
DBG_IOP_AW_COUNT=0x00000000
```

If this appears, stop. Restore the known-good bitstream. Do not debug keyboard,
mouse, UI, or Ethernet source from that state.

Known-good restored state observed on 2026-05-16 after loading
`H:\testproject\vibeos\os.bin`:

```text
DBG_I_AR_COUNT=0x000a1bea
DBG_MMIO_AW_COUNT=0x00032e15
DBG_IOP_AW_COUNT=0x00000098
MOUSE_DBG7=0x75008d80
```

`MOUSE_DBG7=0x75008d80` is a USB HID polling state, not a crash. A crash marker
uses `0xee0000xx`.

Current Ethernet/UI 50 MHz bitstream after the icache refill fix:

```text
H:\testproject\project_2\riscv_ps_ddr_hw_eth_ui_stabletop_50\riscv_ps_ddr_hw.runs\impl_1\riscv_ps_ddr_wrapper.bit
```

Current ping-passing Ethernet candidate:

```text
H:\testproject\project_2\riscv_ps_ddr_hw_eth_clkdomain_probe_50\riscv_ps_ddr_hw.runs\impl_1\riscv_ps_ddr_wrapper.bit
```

This candidate uses the Z7-LITE tutorial Ethernet XDC pinmap:

```text
RX_CLK K17
TX_CLK L14
RX_DV  K18
RXD0   J14
RXD1   K14
RXD2   M18
RXD3   M17
TX_EN  N16
TXD0   M14
TXD1   L15
TXD2   M15
TXD3   N15
MDIO   J15
MDC    G14
RESET  H20
```

Do not switch to the OCR-derived `RX_CLK=K18/RX_DV=H16/TXD3=L17` mapping. That
mapping leaves RX clock nearly flat and does not answer ping.

Required post-boot proof from this version:

```text
far-call probe: MOUSE_DBG7=0x00000666
VibeOS boot:    DBG_MMIO_AW_COUNT increases, DBG_IOP_AW_COUNT increases
Ethernet:       ETH_DBG7=0x45544891, IP=192.168.0.154
USB HID:        PORTSC1=0x84001405, MOUSE_DBG7=0x75008d80 while polling
```

Important correction: this known-good UI rollback bitstream is not an Ethernet
candidate. Direct GEM0 probing on it produced all-zero GEM registers:

```text
GEM0_BEFORE NWCTRL=0x00000000 NWCFG=0x00000000 NWSR=0x00000000 PHYMNTNC=0x00000000
GEM0_AFTER  NWCTRL=0x00000000 NWCFG=0x00000000 NWSR=0x00000000 PHYMNTNC=0x00000000
```

So `eth0 phy FAIL` on that bitmap is expected. Do not change keyboard/UI code
to fix that state.

## Already Proven Ethernet Fact

The GEM0/MDIO hardware path has already produced real PHY values:

```text
PHY01 BMCR=0x3100 BMSR2=0x786d ID1=0x001c ID2=0xc816
```

That means the board can read the Ethernet PHY when the Ethernet-capable
bitstream and matching `ps7_init.tcl` are used. The OS driver has now moved
past PHY-only probing: `vibeos/drivers/ps_gem.c` owns GEM DMA descriptors,
lwIP netif registration, RX polling, TX linkoutput, and static IP
`192.168.0.154`.

## Interrupt / Polling Boundary

Do not assume that M/S/U privilege support means USB, GEM, or GUI updates are
already interrupt driven. CPU trap support exists, but the current FPGA minimal
Ethernet/UI bitstream does not expose real external device interrupts to the
RISC-V core.

Current facts for this 50 MHz Ethernet/UI baseline:

```text
project_2/pl_mmio_jtag_console.v:
    assign ext_intr = 1'b0;

project_2/riscv_ps_ddr_top.v:
    .intr_i(mmio_ext_intr_w)
    .timer_intr_i(mmio_timer_intr_w)

vibeos/os.c:
    FPGA minimal uses cooperative polling and does not enable machine IRQs.

vibeos/drivers/ps_gem.c:
    ps_gem_has_pending_irq() returns 0
```

So the current working Ethernet path is intentionally cooperative:

```text
USB HID task      -> EHCI interrupt endpoint qTD polling
network_task      -> GEM RX descriptor polling + lwIP timeouts
gui_task          -> dirty/cursor/clock redraw checks
```

This is why changing only C code cannot make USB/GEM "interrupt-only". A true
device-interrupt migration needs a matching bitstream:

1. add an IRQ latch in PL instead of `assign ext_intr = 1'b0`
2. return real IRQ IDs from the fake PLIC claim register
3. wire a source for USB event / GEM RX / software event into that latch
4. enable `trap_init()`, `plic_init()`, and only the needed interrupt bits in
   the FPGA minimal boot path
5. keep the ISR tiny: acknowledge/latch flags and wake tasks only
6. run EHCI/GEM/lwIP work in task context, not inside the ISR

Until that hardware path exists, the safe optimization direction is software
event-driven rendering: input/network code sets dirty flags and wakes GUI or
network tasks, while periodic clock/caret/demo updates run from a bounded timer
tick. Do not put blocking USB enumeration, lwIP, SSH, or full HDMI redraws in a
machine interrupt handler.

## SSH/SFTP Direction

VibeOS currently has an SSH client and SFTP client, not an SSH server. So this
works:

```text
VibeOS 192.168.0.154  ->  ssh target on another machine
```

This is not implemented yet:

```text
PC ssh 192.168.0.154
```

Use `ssh probe` first. It only checks TCP connect and does not require a
password:

```text
ethstat
ssh probe 192.168.0.152:2221
ssh set root@192.168.0.152:2221
ssh auth <password>
ssh exec uname -a
```

The default `ssh boot init` template now uses the same target and documents
these four manual test commands.

Current VibeOS SSH client preference is fixed to the Suite-B-compatible ECC
set:

```text
KEX:      ecdh-sha2-nistp256
HOSTKEY:  ecdsa-sha2-nistp256
CIPHER:   aes128-ctr
MAC:      hmac-sha2-256
```

Do not switch the client to `diffie-hellman-group14-*`. The WSL server accepts
that legacy DH family, but this bare-metal mbedTLS Suite-B build has
`MBEDTLS_MPI_MAX_SIZE=48`, so 2048-bit DH cannot work. The verified WSL-side
exact match is:

```text
ecdh-sha2-nistp256 + ecdsa-sha2-nistp256 + aes128-ctr + hmac-sha2-256
```

Repeated `ssh exec` commands must not call `libssh2_exit()` after every
command. Keep libssh2 initialized for the OS lifetime and only create/free the
per-command session and TCP transport. The bad signature was:

```text
first ssh exec ... works
second ssh exec ... -> ERR: SSH handshake timeout
```

The fix is in `vibeos/apps/ssh/ssh_client.c`:

```text
ssh_runtime_init(): mbedtls_os_platform_init(); libssh2_init(0) once
ssh_runtime_release(): no-op
```

Also close TCP transport with `altcp_close()` first and use `altcp_abort()` only
as fallback. Do not restore the old destroy path that always aborted the PCB.

If a failure shows:

```text
st=40 ret=0 tcp_rx=1490 ssh_tx=396 ssh_rx=1490 ... handshake timeout
```

then KEX already completed and the client is timing out after NEWKEYS while
waiting for the server's `SSH_MSG_SERVICE_ACCEPT`. This is not a GEM/lwIP
receive failure if `tcp_rx` and `ssh_rx` are both increasing.

```text
sshdbg: sst=<session_stage> sret=<session_rc> nb=<libssh2_nb_state>
        pkt=<last_packet_type> plen=<last_packet_len> mac=<mac_state>
        lseq=<local_seqno> rseq=<remote_seqno>
```

The old AES-CTR continuous-stream hypothesis was tested and did not resolve the
`st=40 tcp_rx=1490 ssh_tx=396` signature. Keep the libssh2 mbedTLS cipher
backend in its known-compatible form unless a packet-level trace proves
otherwise. The current diagnostic patch records the session startup phase and
the last decoded transport packet in:

```text
vibeos/third_party/libssh2/src/session.c
vibeos/third_party/libssh2/src/transport.c
vibeos/apps/ssh/ssh_client.c
```

Interpretation for the current debug build:

```text
sst=751  -> service request was sent
sst=761  -> waiting for SSH_MSG_SERVICE_ACCEPT
pkt=6    -> SERVICE_ACCEPT was actually decoded
mac=-1   -> invalid MAC path
pkt=0 with tcp_rx increasing -> bytes are arriving but no SSH packet decoded
pkt=7 plen=287 mac=0 -> OpenSSH sent RFC8308 EXT_INFO after NEWKEYS; crypto is OK
```

If `sst=761 sret=-37 pkt=7 mac=0` appears, do not change cipher, MAC, or
network code. The packet is decrypted and authenticated. The fix direction is
session startup ordering: consume the optional OpenSSH `SSH_MSG_EXT_INFO` before
waiting for `SSH_MSG_SERVICE_ACCEPT`. The minimal patch lives in:

```text
vibeos/third_party/libssh2/src/session.c
```

## Keyboard-Controlled Mouse

The 50 MHz stable UI uses USB HID keyboard reports directly for numpad mouse
movement. Do not drive cursor movement from key-repeat events. Key repeat is
for text only; cursor movement must use held key state.

Verified direction/click mapping:

```text
KP7/KP8/KP9 -> up-left/up/up-right
KP4/KP6     -> left/right
KP1/KP2/KP3 -> down-left/down/down-right
KP/         -> left mouse button
KP*         -> right mouse button
```

The implementation is in:

```text
vibeos/drivers/virtio/virtio_input.c
```

Pointer usages are latched with a short release grace. A USB HID interrupt IN
report can momentarily return no key while the user is physically still holding
a key. If the cursor code treats that empty report as an immediate key release,
the pointer reaches maximum speed, stops, then starts accelerating again. The
fix is:

```text
key present in report -> held=1, cancel pending release
key absent in report  -> schedule release after USB_KBD_POINTER_RELEASE_GRACE_MS
movement tick         -> use held state, not raw report pulses
```

Acceleration state is reset only when the direction has been released long
enough, or when the direction actually changes. Holding one direction should
ramp to maximum speed and stay there.

Current 50 MHz keyboard-pointer acceleration constants:

```text
USB_KBD_POINTER_RELEASE_GRACE_MS=160
USB_KBD_POINTER_RAMP_MS=850
USB_KBD_POINTER_BASE_SPEED=340
USB_KBD_POINTER_TOP_SPEED=1000
```

Do not tie pointer motion to terminal key repeat, terminal caret blink, or the
taskbar clock. In `vibeos/user_gui.c`, cursor movement uses the lightweight
`vga_present_cursor()` overlay path. Terminal caret blink is deferred while the
pointer is moving, and the taskbar clock is refreshed through a taskbar-only
rect update. The bad signature was smooth cursor movement except every 500 ms
when the focused terminal caret blink forced a window redraw.

Demo3D is still software-rendered. Its low FPS is primarily caused by the GUI
redrawing the desktop/window stack and then scanning VRAM shadow state on each
animation frame. Do not implement a risky single-window fast path unless network
ping and HTTP are rechecked afterward; a previous attempt made Ethernet init
look dead because the UI path changed task scheduling. The safe next software
step is a proper dirty-region queue shared by cursor, taskbar, terminal, and
demo windows. A hardware step can be added later as a small blitter/sprite or
line/triangle raster MMIO block, but the OS must first have correct dirty-region
ownership so hardware acceleration has a clean contract.

Do not use a small poll-count timeout for keyboard interrupt IN qTDs. While a
key state is unchanged, the HID keyboard endpoint may legally leave the IN qTD
active/NAK until the next state change. The broken signature was cursor motion
that moved for a short segment, slowed/stopped, then accelerated again. The
source fix is:

```text
USB_KBD_ACTIVE_TIMEOUT_POLLS=0
```

Keyboard qTD active state with `MOUSE_DBG7=0x75008d80` is normal. Recover only
on halt/error or real disconnect; do not periodically tear down a healthy
pending interrupt qTD.

For 50 MHz bitstreams, compile the OS with:

```text
make profile=fpga_minimal FPGA_MTIME_HZ=50000000 os.bin
```

For 60 MHz test bitstreams, compile with:

```text
make profile=fpga_minimal FPGA_MTIME_HZ=60000000 os.bin
```

Do not load a 60 MHz-timer OS onto a 50 MHz bitstream when testing UI latency;
`sys_now()`-based movement, cursor blink, lwIP timeouts, and SNTP scheduling
will all be skewed.

For WSL on the Windows host, do not expect the FPGA to reach a private WSL NAT
address directly. Expose WSL sshd through Windows, for example
`192.168.0.152:2221 -> WSL sshd`, then point VibeOS at `192.168.0.152:2221`.
The current Windows ssh config uses:

```text
Host 127.0.0.1
  HostName 127.0.0.1
  User root
  Port 2221
```

That is only for Windows-local clients. VibeOS must use the Windows LAN IP,
not `127.0.0.1`, so the target is `root@192.168.0.152:2221`.

If `ssh probe 192.168.0.152:2221` fails, fix the Windows/WSL SSH listener or
portproxy before changing VibeOS networking.

If `ssh exec ...` still prints `rc=-8 libssh2=-8`, do not change USB, HDMI,
GEM, or the Windows portproxy first. `-8` is libssh2
`KEY_EXCHANGE_FAILURE`. Run:

```text
ssh diag
```

The current build appends the selected SSH methods and allocator aux fields to
the failure line:

```text
kexdbg: st=<stage> ret=<inner rc> a0=<aux0> a1=<aux1> a2=<aux2> kex=<method> hk=<hostkey> c=<cipher> mac=<mac>
```

The libssh2 KEX wrapper also changes the old generic
`Unrecoverable error exchanging keys` message into:

```text
KEX exchange failed st=<stage> ret=<inner rc> kex=<method> hk=<hostkey> c=<cipher> mac=<mac>
```

Use that inner `ret` and `st` to decide the next fix. Important ECDH stages:

```text
180: mbedTLS CTR_DRBG seed failed
220/222: preparing curve/local ECDH key
2231: local ECDH key allocation failed
2232: mbedTLS ECDSA/ECDH key generation failed; `ret` is the real mbedTLS/PSA error
2233: local ECDH public-key buffer allocation failed
2234: local ECDH public-key export failed; `ret` is the real mbedTLS/PSA error
2291: mbedTLS OS pool allocation failed; `a0` requested bytes, `a1` total free
      bytes, `a2` largest free block
224/225/226: sending ECDH_INIT
227/228/229: waiting for ECDH_REPLY
232/233: parsing reply / creating ECDH shared secret
236: ECDSA hostkey signature verification failed
237: sending NEWKEYS failed
40: KEX completed
```

If methods are all `-`, debug KEXINIT parsing or TCP receive. If methods are
selected and `st=2232`, do not change Ethernet/USB/UI; fix the mbedTLS keygen
reason shown in `ret`. If `st=236`, debug the ECDSA hostkey verify path or
temporarily force an RSA host key. If `st=233`, debug ECDH shared-secret
generation in the mbedTLS backend.

Observed fix for:

```text
kexdbg: st=2232 ret=-141
```

`ret=-141` is `PSA_ERROR_INSUFFICIENT_MEMORY`. In this codebase there are two
required memory fixes and one RNG fix:

1. `vibeos/os.ld` must place `_heap_start` after `.apppt`:

```text
PROVIDE(_heap_start = ALIGN(_apppt_end, 4096));
```

Do not restore `_heap_start = ALIGN(_bss_end, 4096)`. That old value made
`_heap_start` overlap `app_l2_pt/app_root_pt`, so allocator metadata could
overwrite app page tables. Check after every linker edit:

```text
wsl --cd /mnt/h/testproject/vibeos --exec sh -lc "riscv64-unknown-elf-nm -n os.elf | grep -E '_heap_start|_apppt_end|app_l2_pt|app_root_pt'"
```

Good result:

```text
app_l2_pt   < _apppt_end == _heap_start
```

2. `vibeos/ports/mbedtls/mbedtls_port.c` must use the dedicated mbedTLS OS
pool allocator. Do not point mbedTLS directly at the FPGA page allocator:
ECC KEX creates many small MPI/ECP objects, and 4KB page allocation wastes too
much memory and fragments diagnostics.

`vibeos/alloc.c` still must keep the bitmap page allocator with a working
`free()`. Do not restore the old bump-only FPGA allocator where `free()` is a
no-op: libssh2/mbedTLS allocate and free many temporary KEX objects, and the
no-op free path can make ECDH/ECDSA key generation fail even when Ethernet and
TCP are already working. Keep the USB DMA region reserved:

```text
FPGA_USB_DMA_CPU_BASE=0x851da000
FPGA_USB_DMA_CPU_SIZE=0x00010000
```

3. If `st=2232 ret=-141 a0=0 a1=0 a2=0`, the failure did not come from the
mbedTLS pool allocator. In this version, libssh2's mbedTLS backend must use
`vibe_mbedtls_rng()` instead of `mbedtls_ctr_drbg_random()` for SSH KEX/key
operations. Do not restore CTR_DRBG in `vibeos/third_party/libssh2/src/mbedtls.c`
unless the entropy/PSA path is fixed first.

If an OS reload leaves the CPU almost idle, for example:

```text
DBG_I_AR_COUNT=0x00000010
DBG_IOP_AW_COUNT=0x00000000
DBG_CPU_PC=0x00000040
ETH_DBG7=0x00000000
```

do not debug SSH or Ethernet source. Reprogram the Ethernet bitstream with PS7
init first, then reload OS:

```text
xsdb.bat project_2/program_riscv_psinit_bit_usb.tcl H:/testproject/project_2/riscv_ps_ddr_hw_eth_clkdomain_probe_50/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit H:/testproject/project_2/riscv_ps_ddr_hw_eth_clkdomain_probe_50/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/ip/riscv_ps_ddr_processing_system7_0_0/ps7_init.tcl
xsdb.bat project_2/release_vibeos_no_usb_reset.tcl H:/testproject/vibeos/os.bin
```

Specific boot-fail signature observed on 2026-05-17:

```text
DBG_CPU_PC=0x00000040
DBG_LAST_MMIO_AWADDR=0x1000403c
DBG_LAST_MMIO_WDATA=0x00000014
MOUSE_DBG0=0x00000014
ping 192.168.0.154 -> Destination host unreachable
```

`0x14` means `fpga_minimal_mouse_mark(20)`, so the OS reached the USB boot
snapshot and then trapped while reading PS USB registers. That is a PL/PS
initialization mismatch, not an SSH, lwIP, keyboard, or HDMI source bug. The
verified recovery is:

```text
program_riscv_psinit_bit_usb.tcl  ->  USB0_HOST_RELEASE PORT=0x84001403
release_vibeos_no_usb_reset.tcl   ->  load current vibeos/os.bin
ping -S 192.168.0.152 192.168.0.154 -> replies
```

`release_vibeos_full_psinit_usb.tcl` now defaults to the same
`riscv_ps_ddr_hw_eth_clkdomain_probe_50` bitstream to avoid accidentally
restoring the old non-Ethernet default.

Preferred recovery command from PowerShell:

```powershell
H:\testproject\project_2\release_vibeos_eth_stable.ps1 H:/testproject/vibeos/os.bin
```

This wrapper intentionally runs the proven two-stage sequence. Do not replace it
with a single in-session Tcl download unless that single-session path is proved
with both CPU debug counters and ping. A single-session full release has booted
the UI but failed Ethernet ping in this setup, while the two-stage wrapper
restores both OS boot and ping.

## Strict No-Touch List

Do not modify these while doing the first Ethernet integration step:

- `project_2/vibe_hdmi_mmio.v`
- `project_2/pl_mouse_mmio.v`
- USB HID polling logic
- keyboard/numpad mapping
- GUI rendering/layout
- terminal input path
- stable bitstream output path

Only change these for the first step:

- `vibeos/Makefile`
- `vibeos/os.c`
- `vibeos/drivers/ps_gem.c`
- `vibeos/user_cmd.c`
- `vibeos/lwipopts.h`
- optionally this SOP

## Step 1: OS GEM Driver Patch

Minimal source changes:

1. Add `drivers/ps_gem.c` to `DRIVER_SOURCES` in `vibeos/Makefile`.
2. Add this declaration in `vibeos/os.c`:

```c
extern void ps_gem_init(void);
```

3. Do not call `ps_gem_init()` synchronously before `user_init()`. GEM/PHY or
   lwIP faults must not prevent UI/keyboard from booting. Start it once from
   `network_task()` after the UI tasks have been created.
4. Keep a cached copy of the GEM debug words inside `ps_gem.c`, because the
   old PL debug MMIO window is shared with USB mouse diagnostics and can be
   overwritten after boot.
5. Keep `ps_gem_probe()` as a fallback diagnostic, but normal boot must use
   `ps_gem_init()`.
6. Add a terminal command:

```text
ethstat
```

Expected PHY-only fallback output shape:

```text
eth0: phy OK link=up
nwcfg=........ nwctrl=........ nwsr=........
phyid=001c:c816 bmsr=786d rc=00000000 status=45544881
```

Expected driver output shape after `ps_gem_init()` succeeds:

```text
eth0: DRIVER ip=192.168.0.154 link=up
nwcfg=........ nwctrl=........ nwsr=........
tx=........ rx=........ status=45544891
```

`ethstat` must classify `0x45544891` with a nibble mask, not a byte mask:

```c
(status & 0xfffffff0UL) == 0x45544890UL
```

Using `0xffffff00UL` is wrong because `0x45544891 & 0xffffff00` becomes
`0x45544800`, so the command falls through to the PHY fallback display and
prints `phy unknown link=up` even though the GEM DMA driver is ready.

The intended order is:

```c
fpga_minimal_usb0_host_init();
fpga_minimal_mouse_mark(3);
/* USB HID is lazy-polled by virtio_mouse_poll_task(). */
user_init();
/* network_task() defers ps_gem_init(); */
return;
```

Do not add an infinite loop after `ps_gem_init()`. The UI must continue to
boot.

For `FPGA_MINIMAL`, `user.c` creates `network_task()` so `ps_gem_poll()` keeps
running. Do not replace it with a sleep-only loop; this driver is currently
polling based, not interrupt based.

`lwipopts.h` must set:

```c
#define MEM_ALIGNMENT 4
```

Without this, lwIP defaults to byte alignment and `memp_init_pool()` can write
free-list words to unaligned addresses, producing an early trap such as:

```text
MOUSE_DBG5=0x800fa734
MOUSE_DBG6=0x00000006
```

## Step 2: Build the OS

Do not rebuild the stable rollback bitstream in this step.

```powershell
wsl --cd /mnt/h/testproject/vibeos --exec make profile=fpga_minimal
```

`profile=fpga_minimal` must set `FPGA_MINIMAL=1` and build `os.bin`. The default
`make` target must not launch QEMU. Use `make qemu` only when explicitly testing
the QEMU profile.

If the build fails, fix only the compile error. Do not touch bitmap/HDMI/USB.

## Step 3: Build Ethernet Candidate Bitstream

Build in a separate directory:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File H:\testproject\project_2\run_eth_build.ps1
```

For the Ethernet candidate, use `-fclk_mhz 50`. The 60 MHz Ethernet build did
produce a bitstream, but timing failed (`WNS=-3.243ns`) and the RISC-V CPU did
not reach MMIO/UI after boot. The loader Tcl was not the cause:
`release_vibeos_no_usb_reset.tcl` matches the known-good keyboard backup.
50 MHz requires the `HDMI_FCLK_50` Verilog define so the HDMI MMCM still
generates a 65 MHz pixel clock. The build Tcl sets this define automatically
when `-fclk_mhz 50` is used.

Do not use these existing Ethernet experiment bitstreams as a new baseline:

```text
riscv_ps_ddr_hw_eth_ui
riscv_ps_ddr_hw_eth_ui_fetchfix
riscv_ps_ddr_hw_eth_ui_axiwbuffix
riscv_ps_ddr_hw_eth_ui_branchbubble
```

Observed failures on 2026-05-16:

```text
eth_ui / fetchfix: MOUSE_DBG7=0xee000006 near task_create/task_add
                  stack/register corruption such as sp=0x2
axiwbuffix:       CPU stayed in BSS clear, DBG_MMIO_AW_COUNT=0
branchbubble:     GEM init reached, then task/register corruption
```

If an Ethernet bitmap produces `MOUSE_DBG7=0xee000006` while the rollback UI
bitmap runs the same OS, treat it as a bitmap/CPU-timing candidate failure.
Rollback first, then rebuild a fresh Ethernet candidate at a more conservative
clock or with the minimal known-good UI hardware as the base.

## Critical I-Cache Fix

Do not revert this source change:

```text
H:\testproject\ultraembedded_riscv\top_cache_axi\src_v\icache.v
```

The original refill logic aligned the AXI read address to the 32-byte cache line
base but started writing returned words into the data RAM at the unaligned
requested PC index. A branch to a cold, non-line-aligned target such as
`0x80002404` then returned the word from `0x80002400` as the instruction for
`0x80002404`. If that preceding word is `0x00000000`, the CPU traps with:

```text
MOUSE_DBG0=0x80002404
MOUSE_DBG1=0x00000002
MOUSE_DBG7=0x80002404
```

The correct fix is to start the data RAM write index at the same 32-byte line
base as the AXI read:

```verilog
data_write_addr_q <= {refill_addr_w[CACHE_DATA_ADDR_W+2-1:ICACHE_LINE_SIZE_W],
                      {(ICACHE_LINE_SIZE_W-2){1'b0}}};
```

This was verified with `project_2/boot_probe_far_call.S`: before the fix the
probe trapped at `0x80002404`; after the fix it wrote `MOUSE_DBG7=0x00000666`.
If this signature regresses, do not debug keyboard, UI, Ethernet, or USB first.

The program/release Tcl scripts set JTAG to 30 MHz by default. If the cable or
hub is unstable, override with `VIBE_JTAG_HZ`, for example `15000000`. Do not
put `jtag frequency 1000000` back into the normal loader path; that makes
bitstream and DDR `dow -data` uploads unnecessarily slow.

The build script must set:

```tcl
set_property synth_checkpoint_mode None $bd_file
```

This avoids Vivado 2019.2 Windows BD/IP OOC child runs that can hang with:

```text
rundef.js JavaScript runtime error: Access denied
```

The build script must also avoid project run wrappers:

```tcl
synth_design
opt_design
place_design
route_design
write_bitstream
```

Do not use `launch_runs synth_1` / `wait_on_run synth_1` for this flow. In
this project Vivado completed child synthesis in about 3 minutes, but the
parent process stayed stuck waiting for run state.

Do not wrap `vivado.bat` inside another `.bat` as the primary runner. That
path triggered a Vivado IP Integrator Tcl initialization error:

```text
couldn't read file "addr.tcl": no such file or directory
```

Use `project_2/run_eth_build.ps1` or run the Vivado command directly from
PowerShell.

Before programming the candidate, verify the generated wrapper contains:

```text
MDIO_ETHERNET_0_0_mdio_io
MDIO_ETHERNET_0_0_mdc
ENET0_GMII_RX_CLK_0
ENET0_GMII_TX_CLK_0
ENET0_GMII_TX_EN_0
enet0_gmii_rxd
enet0_gmii_txd
ETH_RESET
```

If these ports are missing, it is not an Ethernet bitmap even if the file name
looks like an Ethernet candidate.

## Step 4: Program Candidate Bitstream and Load OS

Program the Ethernet candidate bitstream. The direct-flow build writes both of
these paths for the current stable 50 MHz Ethernet/UI build:

```text
H:\testproject\project_2\riscv_ps_ddr_hw_eth_ui_stabletop_50\riscv_ps_ddr_wrapper.bit
H:\testproject\project_2\riscv_ps_ddr_hw_eth_ui_stabletop_50\riscv_ps_ddr_hw.runs\impl_1\riscv_ps_ddr_wrapper.bit
```

### Current Verified Boot Flow: 2026-05-16

This is the exact flow that restored UI + USB keyboard polling after the
Ethernet/UI experiments. Use this first before changing any driver code.

1. Program the stable 50 MHz Ethernet/UI bitstream and its matching
   `ps7_init.tcl`:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\program_riscv_psinit_bit_usb.tcl H:/testproject/project_2/riscv_ps_ddr_hw_eth_clkdomain_probe_50/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper.bit
```

Expected program output:

```text
JTAG_FREQ_SET=30000000
USB0_HOST_RELEASE CMD=0x00080001 ... PORT=0x84001403 MODE=0x00000003
PROGRAM_PSINIT_BIT_USB_DONE
```

2. Build the OS in FPGA minimal profile:

```powershell
wsl --cd /mnt/h/testproject/vibeos --exec make profile=fpga_minimal os.bin
```

3. Load `os.bin` with the USB recovery loader:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\release_vibeos_with_usb_recover.tcl H:/testproject/vibeos/os.bin
```

Current `start.s` skips the large BSS clear in `FPGA_MINIMAL`; therefore the
flat `os.bin` must include the zero-filled BSS hole. Do not load
`os.fpga_minimal.bin` with the current `start.s` unless the start code is
changed back to clearing BSS itself. Loading the wrong image can make the OS
park in `page_init()` before tasks, UI, USB, or Ethernet start.

4. Wait about 5 seconds, then read USB/HID debug:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\read_mouse_debug.tcl
Get-Content H:\testproject\project_2\read_mouse_debug.out
```

Verified good result from this flow:

```text
MOUSE_DBG5=0x84001405
MOUSE_DBG7=0x75008d80
USBCMD=0x00080011
PERIODIC=0x061db000
PORTSC1=0x84001405
```

`MOUSE_DBG7=0x75008d80` means the keyboard interrupt IN qTD is pending in the
periodic schedule. This is a working HID polling state, not a crash.

USB HID reliability note from 2026-05-16: do not add a full OS-side EHCI
controller reset inside `virtio_input.c`. That made `PORTSC1` fall back to
`0x8c00100a` no-connect on this board. The stable behavior is:

- Tcl `program_riscv_psinit_bit_usb.tcl` releases PS7/USB3320/MIO46 and leaves
  the port connected.
- Tcl `release_vibeos_with_usb_recover.tcl` resets the external USB PHY and
  downloads `os.bin`.
- VibeOS `virtio_input.c` keeps host mode/registers, starts enumeration from
  the current port state, and only does a USB bus reset as a fallback if the
  first control transfer fails.
- `USB_ENUM_RETRY_MS` is short, so a transient failed GET_DESCRIPTOR retry does
  not leave keyboard input dead for 10 seconds.

5. Read GEM debug:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\read_ps_gem_debug.tcl
Get-Content H:\testproject\project_2\read_ps_gem_debug.out
```

Verified current result:

```text
ETH_DBG0=0x00080103
ETH_DBG1=0x0000001c
ETH_DBG2=0x00000006
ETH_DBG7=0x45544891
```

`ETH_DBG7=0x45544891` means the GEM DMA/lwIP driver initialized and configured
IP `192.168.0.154`. It does not by itself prove that ICMP ping works; ping
requires RX/TX descriptor processing and host subnet/wiring to be correct.

Do not run two XSDB scripts at the same time. They fight over `hw_server` and
can print `Socket bind error`; run USB debug, GEM debug, and ping sequentially.

### Older No-USB-Reset Loader

`release_vibeos_no_usb_reset.tcl` can still be useful after the board is already
in a clean USB state, but it is not the verified recovery flow for the current
Ethernet/UI bring-up. If keyboard/UI fail after a reset or bitstream swap, use
`release_vibeos_with_usb_recover.tcl` first.

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\release_vibeos_no_usb_reset.tcl H:/testproject/vibeos/os.bin
```

`program_riscv_psinit_bit_usb.tcl` must use the matching `ps7_init.tcl` derived
from the bitstream directory. Do not program an Ethernet bitstream with the old
stable UI `ps7_init.tcl`.

## Step 5: Required Pass Criteria

After boot, both must be true:

1. UI is visible and terminal accepts keyboard input.
2. Ethernet debug shows a real PHY result, not all `0xffff` and not all zero.

Read CPU/UI health:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\read_pl_cpu_debug_live.tcl
Get-Content H:\testproject\project_2\read_pl_cpu_debug_live.out
```

Read Ethernet status from the VibeOS terminal:

```text
ethstat
```

Do not use `load_vibeos_and_console.tcl` to validate this step; that loader has
previously produced a bad early-reset/download state with low instruction
counts. Use `program_riscv_psinit_bit_usb.tcl` plus
`release_vibeos_no_usb_reset.tcl`. Use `probe_gem0_mdio.tcl` only as a
standalone pre-OS PS/GEM probe, not as the main proof that the stable VibeOS UI
build is alive.

Good Ethernet signs:

```text
ID1=0x001c
ID2=0xc816
BMSR has link/status bits, for example 0x786d
terminal command `ethstat` prints `DRIVER ip=192.168.0.154` after driver init
or `phy OK` when using the fallback PHY probe path
```

Good UI signs:

```text
DBG_MMIO_AW_COUNT != 0
DBG_IOP_AW_COUNT  != 0
keyboard input appears in terminal
```

Bad Ethernet signatures:

```text
GEM registers all 0: bitmap/PS init does not expose or clock GEM0
GEM registers live but all PHY reads 0: MDIO/EMIO pins are not really connected
GEM registers live but all PHY reads ffff: PHY address/wiring/link reset issue
```

## Step 6: Packet Test

After Step 4 passes:

1. run `ethstat` in the terminal and confirm driver mode
2. ping `192.168.0.154` from a host on the same hub/subnet
3. open `http://192.168.0.154/` if ICMP/lwIP routing is alive

If PHY is OK but ping fails, debug only `vibeos/drivers/ps_gem.c` RX/TX
descriptor handling and cache coherency. Do not change keyboard, mouse, HDMI,
terminal, or taskbar code for a packet problem.

### Verified PHY Mode and Ping Proof

The RTL8201F must be in MII mode for this PS GEM EMIO design. The board can
come up in RMII mode: MDIO/link still look valid, but GMII clock and packet IO
do not work. `vibeos/drivers/ps_gem.c` must call
`rtl8201f_force_mii_mode()` immediately after `gem_mdio_init()` and before GEM
DMA rings are enabled.

Verify PHY mode:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\read_rtl8201f_mode.tcl
Get-Content H:\testproject\project_2\read_rtl8201f_mode.out
```

Required result:

```text
PHY01_PAGE7_R16_RMSR=0x1ff2
PHY01_PAGE7_R16_RMII_MODE_BIT3=0
```

Verify GMII clocks:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\read_eth_pin_debug.tcl
Get-Content H:\testproject\project_2\read_eth_pin_debug.out
```

Good result from the ping-passing build:

```text
ETH_TXCLK_EDGES changes quickly
ETH_RXCLK_EDGES changes quickly
ETH_TXCLK_DOMAIN changes quickly
ETH_RXCLK_DOMAIN changes quickly
```

Bad result from the wrong pinmap:

```text
ETH_TXCLK_EDGES changes quickly
ETH_RXCLK_EDGES only changes by a few hundred or stays almost flat
```

Ping from the Windows host on this setup:

```powershell
ping -S 192.168.0.152 -n 5 192.168.0.154
```

Verified pass on 2026-05-16:

```text
Reply from 192.168.0.154: bytes=32 time=350ms TTL=255
Reply from 192.168.0.154: bytes=32 time=1ms TTL=255
Reply from 192.168.0.154: bytes=32 time=138ms TTL=255
Reply from 192.168.0.154: bytes=32 time=327ms TTL=255
Reply from 192.168.0.154: bytes=32 time=1ms TTL=255
Packets: Sent = 5, Received = 5, Lost = 0 (0% loss)
```

### Verified HTTP Proof

HTTP is now served by the lwIP TCP listener started from `start_http()` in
`vibeos/drivers/virtio/virtio_net.c`. The PS GEM path calls `start_http()` from
`ps_gem_init()` after the netif is up.

Verified from the user Windows shell on 2026-05-16:

```text
C:\Users\x213212>curl http://192.168.0.154/
<html><body><h1>VibeOS Ethernet OK</h1><p>lwIP + Zynq GEM0 is running.</p><p>IP: 192.168.0.154</p></body></html>
```

`ETH_DBG4` is used as temporary HTTP/TCP debug:

```text
0x48000001 = HTTP listener created
0x50xxxxxx/0x51xxxxxx/0x52xxxxxx = TCP trace word while debugging RX/TX
```

If one Windows shell cannot connect to port 80 but another can, check local
Windows proxy/firewall/tooling before changing VibeOS. The board-side proof is
the curl response above plus increasing `ETH_DBG5`/`ETH_DBG6`.

### One-Shot Time Sync

Verified on 2026-05-17: SNTP can update Taiwan wall-clock time without killing
HTTP/ping if it is treated as a one-shot network job.

Rules:

```text
Use direct NTP IP 118.163.81.61 for time.stdtime.gov.tw.
Do not use SNTP DNS while debugging this FPGA static-IP path.
Do not call sntp_stop() inside SNTP_SET_SYSTEM_TIME / receive callback.
The callback may only set the OS clock and mark stop_pending.
Call sntp_stop() later from network_task after sys_check_timeouts() returns.
Stop SNTP after the first successful sync; the local timer keeps time running.
If no response arrives within 12 seconds, stop SNTP and keep the build-time clock.
```

The bad signature from the broken version was:

```text
time updates once
then ping/curl stop responding
later UI/OS appears frozen
```

The fixed proof after loading `os.bin` with the stable Ethernet bitstream:

```text
ping -n 6 192.168.0.154 -> 6 received, 0 lost
curl http://192.168.0.154/ -> VibeOS Ethernet OK
wait 30 seconds
ping -n 8 192.168.0.154 -> 8 received, 0 lost
curl http://192.168.0.154/ -> VibeOS Ethernet OK
```

## Rollback

Rollback is always:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\program_riscv_psinit_bit_usb.tcl H:/testproject/backups/vibeos_kb_mouse_accel_hold_20260514_2254/project_2_artifacts/riscv_ps_ddr_wrapper.bit
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\release_vibeos_no_usb_reset.tcl H:/testproject/vibeos/os.bin
```

If rollback restores UI/keyboard, the failed experiment was hardware/bitmap or
Ethernet integration. Do not change keyboard or UI code in response.
