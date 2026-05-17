# Zynq-7020 Type-C UART Path

For the MicroPhase Zynq-7020 board, the onboard Type-C USB-UART bridge appears as `USB-SERIAL CH340 (COM10)` on this Windows host. The board manual maps that bridge to Zynq PS MIO pins:

- UART RX: `PS_MIO14_500`, package pin `C5`
- UART TX: `PS_MIO15_500`, package pin `C8`

These are PS MIO pins, not PL pins. A PL RTL port such as `uart_tx` cannot be constrained to these pins with XDC. To print over the onboard Type-C port, use PS UART0 on MIO14/15 or route UART through EMIO in a Zynq PS block design.

Preferred live test:

```powershell
& 'H:\Xilinx\vivado\Vivado\2019.2\bin\vivado.bat' -mode batch -source H:\testproject\project_2\build_ps_uart0_hw.tcl
powershell -ExecutionPolicy Bypass -File H:\testproject\project_2\read_ps_uart_with_init.ps1 -PortName COM9 -BaudRate 115200 -Seconds 10
```

Expected serial output:

```text
PS_UART0_OK
```

This path uses a Vivado-generated Zynq PS7 configuration for UART0 on MIO14/15, then sends a byte stream through PS UART0 over JTAG/XSDB while reading the Windows CH340 COM port.

Older direct-register test:

```powershell
powershell -ExecutionPolicy Bypass -File H:\testproject\project_2\read_ps_uart0.ps1 -PortName COM10 -BaudRate 115200 -Seconds 5
```

Expected serial output:

```text
PS_UART0_OK
```

The PL-only `uart_smoke.v` remains useful only when `uart_tx` is wired to an external USB-UART RX pin through a known PL package pin.
