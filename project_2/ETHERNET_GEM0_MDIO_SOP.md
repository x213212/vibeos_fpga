# VibeOS Zynq GEM0 Ethernet Bring-up Notes

Date: 2026-05-16

For the current integration direction, follow
`project_2/STABLE_UI_ETH_MINIMAL_SOP.md` first. The rule is: keep the
known-good keyboard/mouse/UI bitstream as rollback, then build a separate
Ethernet candidate with only `-enable_eth` changed.

## Current Hardware Result

`probe_gem0_mdio.tcl` confirms the Zynq GEM0 MDIO path can talk to the
Zynq Z7 Lite Ethernet PHY through the PS GEM0 EMIO MDIO pins.

Observed with Ethernet cable connected to a hub:

```text
GEM0_BEFORE NWCTRL=0x00000010 NWCFG=0x00080103 NWSR=0x00000006
PHY01 BMCR=0x3100 BMSR2=0x786d ID1=0x001c ID2=0xc816
```

Interpretation:

- `ID1=0x001c`, `ID2=0xc816`: Realtek RTL8201-class PHY responds.
- `BMSR2=0x786d`: link status bit is set, so the cable/hub link is up.
- MDIO reads return real values at PHY address `1`; unused addresses return
  `0xffff`.

PHY address `0` also mirrors valid values on this board/session, but the
reference Zynq Z7 Lite design uses PHY address `1`. Keep OS code using PHY
address `1` unless a later board-specific probe proves otherwise.

Important: the stable UI rollback bitmap is not enough for Ethernet. Direct
GEM0 probing on that bitmap produced all-zero GEM registers, so `ethstat`
printing `GEM_OFF` or `phy FAIL` is expected there. The Ethernet bitmap must be
verified by checking that its generated wrapper contains `MDIO_ETHERNET`,
`ENET0_GMII_*`, `enet0_gmii_rxd`, `enet0_gmii_txd`, and `ETH_RESET` ports.

## Direct Probe Command

Run after programming the bitstream and PS init:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' H:\testproject\project_2\probe_gem0_mdio.tcl
Get-Content H:\testproject\project_2\probe_gem0_mdio.out
```

## OS Integration Rule

The PHY/MDIO hardware is working. Do not use the temporary low-linked RISC-V
probe path for normal boot.

The stable VibeOS path is:

1. link OS at `0x80000000`
2. load binary to PS DDR physical `0x01000000`
3. keep PL instruction/data DDR remap at `0x80000000 -> 0x01000000`
4. boot the known-good HDMI/USB/SD UI path
5. call `ps_gem_init()` once after `fs_ensure_root()` and before `user_init()`
6. let `network_task()` call `ps_gem_poll()` in the background

`ps_gem_probe()` remains a fallback diagnostic for MDIO/PHY only. The normal
driver is `ps_gem_init()` in `vibeos/drivers/ps_gem.c`; it initializes GEM DMA
RX/TX descriptors, registers an lwIP `eth0` netif, uses static IP
`192.168.0.154`, and starts the existing HTTP service.

When building the Ethernet-capable bitmap, use:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File H:\testproject\project_2\run_eth_build.ps1
```

The Ethernet-capable build currently uses `-fclk_mhz 50`. At 50 MHz the build
Tcl must define `HDMI_FCLK_50`, otherwise the HDMI MMCM VCO is out of range.
The 60 MHz Ethernet build generated a bitstream but failed timing and did not
reach UI/MMIO, while the loader Tcl matched the known-good keyboard/mouse
loader exactly.

Current packet-passing hardware combination:

```text
bitstream:
H:\testproject\project_2\riscv_ps_ddr_hw_eth_clkdomain_probe_50\riscv_ps_ddr_hw.runs\impl_1\riscv_ps_ddr_wrapper.bit

OS:
H:\testproject\vibeos\os.bin

IP:
192.168.0.154
```

The working bitstream uses the Z7-LITE tutorial Ethernet pinmap:

```text
RX_CLK K17, TX_CLK L14, RX_DV K18,
RXD0 J14, RXD1 K14, RXD2 M18, RXD3 M17,
TX_EN N16, TXD0 M14, TXD1 L15, TXD2 M15, TXD3 N15,
MDIO J15, MDC G14, ETH_RESET H20
```

The wrong OCR-derived pinmap `RX_CLK=K18/RX_DV=H16/TXD3=L17` was tested and
rejected: it leaves RX clock nearly flat and ping fails.

The OS must force RTL8201F page 7 register 16 bit 3 to 0, selecting MII mode.
Required debug proof:

```text
PHY01_PAGE7_R16_RMSR=0x1ff2
PHY01_PAGE7_R16_RMII_MODE_BIT3=0
```

Verified ping command/result:

```powershell
ping -S 192.168.0.152 -n 5 192.168.0.154
```

```text
Packets: Sent = 5, Received = 5, Lost = 0 (0% loss)
```

Verified HTTP response:

```text
curl http://192.168.0.154/
<html><body><h1>VibeOS Ethernet OK</h1><p>lwIP + Zynq GEM0 is running.</p><p>IP: 192.168.0.154</p></body></html>
```

This confirms lwIP TCP is connected to the Zynq GEM0 driver, not only raw ARP
or ICMP.

The build script disables BD/IP OOC checkpoints with
`set_property synth_checkpoint_mode None $bd_file` to avoid the Vivado 2019.2
Windows `rundef.js Access denied` hang.

It also runs the implementation directly in one Vivado process instead of using
`launch_runs` / `wait_on_run`, because those project-run wrappers previously
left the parent Vivado waiting even after synthesis had already completed.

Do not add an early `ps_gem_probe(); for (;;) wfi;` block in `os_start()`.
That was only for low-level MDIO debugging and prevents the UI from booting.

From the VibeOS terminal, use:

```text
ethstat
```

Driver mode should print `eth0: DRIVER ip=192.168.0.154`. If it prints only
`phy OK`, the PHY diagnostic path is alive but the full GEM DMA/lwIP driver did
not initialize. If it prints `phy FAIL` on the stable UI rollback bitmap, that
is expected because that bitmap does not expose GEM0.
