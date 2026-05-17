`vibe-os` is an integrated RISC-V 32-bit QEMU browser OS.

Lineage:

- Original project: [cccriscv/mini-riscv-os](https://github.com/cccriscv/mini-riscv-os)
- Referenced fork/base: [x213212/mini-riscv-os](https://github.com/x213212/mini-riscv-os)
- Bundled JIT compiler: Tiny C Compiler (TCC), LGPL-2.1, with modified source in `third_party/tcc-riscv32/`
- This integration: `vibe-os`, based on `08-BlockDeviceDriver`, adding GUI networking, WRP, and browser support.

<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/b1d2adb4-ee97-4041-b7a0-49c18a2152e6" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/35e27e5e-672e-47f2-9cae-d54827d4c796" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/432cee2d-ce12-4fb7-b3ac-943d71c7d955" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/99882167-bca3-445b-89dd-8fc1f0313f61" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/f7412eda-fdba-4f1e-bbc2-883a4fbdd3bb" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/864735b9-0ed5-476b-9a48-92150bcae609" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/7fdbed70-67d8-4762-9c15-f270bb0475e9" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/b147b2a3-4851-4113-bdce-13555412b1e9" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/38a5fde9-0bc5-473b-a229-82ecbe3d79b5" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/a2ab664b-2bbb-4de3-aa47-c0a0255a4059" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/6712ed96-689a-4d38-b809-7d2b2cd451a9" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/69e17ad8-be37-49eb-8a0f-a307290ed469" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/bb8603cb-ff2b-4c99-afd9-ae6c53804b5c" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/8faaef63-fe46-4651-9fed-629ccebea928" />
<img width="1100" height="890" alt="image" src="https://github.com/user-attachments/assets/08549ae4-73d0-4d2a-a5cf-520494adcb60" />



This project extends from the `08-BlockDeviceDriver` stage of `mini-riscv-os`, adding a GUI, networking, NetSurf-style browser frontend, WRP server integration, and modern web browsing capabilities.

## Quick Start

The fastest way is to download, enter the project, and start QEMU from the host shell:

```sh
# Host shell
git clone https://github.com/x213212/vibeos.git
cd vibeos
make ENABLE_AUDIO=1 qemu
```

If you want to use the WRP browser included in this version, you also need to start tap0 and the WRP server on the host first; detailed instructions are below.

## Commands

### Host Shell

These commands are executed in the Linux shell outside of QEMU.

Configure QEMU tap network:

```sh
# Host shell
sudo ip tuntap add dev tap0 mode tap user "$USER" 2>/dev/null || true
sudo ip addr flush dev tap0
sudo ip addr add 192.168.123.100/24 dev tap0
sudo ip link set tap0 up
```

Compile mbedTLS static libs (only needed for the first build or when mbedTLS source updates):

```sh
# Host shell
rm -rf /tmp/mbedtls-rv-build

/usr/bin/cmake -S ./third_party/mbedtls \
  -B /tmp/mbedtls-rv-build \
  -G "Unix Makefiles" \
  -DCMAKE_SYSTEM_NAME=Generic \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DCMAKE_C_COMPILER=/usr/bin/riscv64-unknown-elf-gcc \
  -DCMAKE_AR=/usr/bin/riscv64-unknown-elf-ar \
  -DCMAKE_RANLIB=/usr/bin/riscv64-unknown-elf-ranlib \
  -DCMAKE_C_FLAGS="-march=rv32ima -mabi=ilp32 -I$(pwd)" \
  -DENABLE_PROGRAMS=OFF \
  -DENABLE_TESTING=OFF \
  -DGEN_FILES=ON \
  -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
  -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
  -DUSE_STATIC_TF_PSA_CRYPTO_LIBRARY=ON \
  -DUSE_SHARED_TF_PSA_CRYPTO_LIBRARY=OFF \
  -DMBEDTLS_CONFIG_FILE=$(pwd)/third_party/mbedtls/configs/config-suite-b.h \
  -DMBEDTLS_USER_CONFIG_FILE=$(pwd)/ports/mbedtls/mbedtls_os_config.h \
  -DTF_PSA_CRYPTO_CONFIG_FILE=$(pwd)/third_party/mbedtls/configs/crypto-config-suite-b.h \
  -DTF_PSA_CRYPTO_USER_CONFIG_FILE=$(pwd)/ports/mbedtls/tf_psa_crypto_os_config.h \
  -DMBEDTLS_FATAL_WARNINGS=OFF \
  -DMBEDTLS_AS_SUBPROJECT=OFF \
  -DMBEDTLS_TARGET_PREFIX=rv_

cmake --build /tmp/mbedtls-rv-build -j2
```

Compile the OS:

```sh
# Host shell
make -j2 os.elf
```

Forced rebuild (recommended after modifying headers or third-party source):

```sh
# Host shell
make -B -j2 os.elf
```

Compile WRP server:

```sh
# Host shell
cd third_party/wrp
GOPATH=/tmp/gopath GOMODCACHE=/tmp/gopath/pkg/mod GOCACHE=/tmp/go-build go build -o wrp ./...
```

Start WRP server (keep this shell open):

```sh
# Host shell, terminal 1
cd third_party/wrp
./wrp -l :9999 -b /opt/google/chrome/chrome -ua "" -s 1s
```

Start QEMU (in another host shell):

```sh
# Host shell, terminal 2
make ENABLE_AUDIO=1 qemu
```

### QEMU Guest Shell

These commands are executed in the OS terminal inside the QEMU window, not in the host Linux shell.

View WRP server URL:

```text
# QEMU guest shell
wrp status
```

Set WRP server URL:

```text
# QEMU guest shell
wrp set http://192.168.123.100:9999
```

Open WRP browser:

```text
# QEMU guest shell
wrp open
```

Open a website directly:

```text
# QEMU guest shell
netsurf https://news.ycombinator.com
netsurf https://duckduckgo.com
netsurf http://68k.news
```

Download files:

```text
# QEMU guest shell
wget http://192.168.123.100:9999/ index.html
```

SSH:

```text
# QEMU guest shell
ssh status
ssh set root@192.168.123.100:2221
ssh auth <password>
ssh exec <command>
```

## Protocol / Network Stack

`vibe-os` does not implement a full protocol stack from scratch; instead, it integrates existing open-source stacks:

- TCP/IP: Uses [lwIP](https://github.com/lwip-tcpip/lwip) as the TCP/IP stack within the guest OS.
- Network device: QEMU `virtio-net-device`, connected to `192.168.123.100/24` via host `tap0`.
- HTTP client: Built-in `apps/net/wget/user_wget.c`, running on lwIP TCP sockets, used to download WRP HTML, GIP/GIF images, and general HTTP files.
- SSH protocol: Uses [libssh2](https://github.com/libssh2/libssh2) for the SSH-2 client protocol.
- SSH crypto backend: libssh2 uses the [mbedTLS](https://github.com/Mbed-TLS/mbedtls) backend via `third_party/libssh2/src/mbedtls.c`.
- TLS / crypto primitives: mbedTLS provides RSA, AES, SHA, HMAC, CTR-DRBG, X.509, PK parsing, etc.
- Browser remoting: Modern websites are rendered by [WRP](https://github.com/tenox7/wrp) + Chrome on the host. The guest OS only handles HTML, ISMAP, and GIP/GIF images returned by WRP.

Important boundaries:

- `vibe-os` does not execute modern JavaScript/CSS directly in the guest.
- The WRP server runs Chrome on the host; the guest interacts with it via HTTP/ISMAP.
- SSH commands are sent from the guest OS via lwIP TCP connections to the remote SSH server, processed by libssh2.

## Upstream Projects

Core sources:

- Original OS: [cccriscv/mini-riscv-os](https://github.com/cccriscv/mini-riscv-os)
- Referenced OS fork/base: [x213212/mini-riscv-os](https://github.com/x213212/mini-riscv-os)
- WRP Web Rendering Proxy: [tenox7/wrp](https://github.com/tenox7/wrp)
- GIP fast GIF encoder: [tenox7/gip](https://github.com/tenox7/gip)
- lwIP TCP/IP stack: [lwip-tcpip/lwip](https://github.com/lwip-tcpip/lwip)
- mbedTLS: [Mbed-TLS/mbedtls](https://github.com/Mbed-TLS/mbedtls)
- libssh2: [libssh2/libssh2](https://github.com/libssh2/libssh2)
- RISC-V GNU toolchain: [riscv-collab/riscv-gnu-toolchain](https://github.com/riscv-collab/riscv-gnu-toolchain)
- QEMU: [qemu-project/qemu](https://gitlab.com/qemu-project/qemu)
- Chromium / Chrome rendering engine: [chromium/src](https://chromium.googlesource.com/chromium/src)

NetSurf libraries:

- libnsfb: [git.netsurf-browser.org/libnsfb.git](https://git.netsurf-browser.org/libnsfb.git)
- libwapcaplet: [git.netsurf-browser.org/libwapcaplet.git](https://git.netsurf-browser.org/libwapcaplet.git)
- libparserutils: [git.netsurf-browser.org/libparserutils.git](https://git.netsurf-browser.org/libparserutils.git)
- libhubbub: [git.netsurf-browser.org/libhubbub.git](https://git.netsurf-browser.org/libhubbub.git)

Integrated features:

- VGA GUI / window manager
- virtio block / net / keyboard / tablet
- lwIP TCP/IP
- Simple shell, file system, text editor
- NetSurf-style browser frontend
- WRP integration using host Chrome for modern website rendering
- WRP GIP fast GIF path
- ISMAP click / keyboard / wheel queue synced with WRP `.map` IDs

## Directory Structure

```text
vibe-os repo root/
  Makefile              OS build / QEMU startup settings
  os.elf                Compiled RISC-V kernel / userspace image
  hdd.dsk               QEMU virtio block disk image
  user.c                User/session bootstrap and shared state
  user_terminal.c/.h    Terminal input, history, env, aliases, worker loop
  user_cmd.c/.h         Shell command dispatch and command handlers
  user_gui.c/.h         GUI window manager, taskbar, resize/focus loop
  user_graphics.c/.h    Image opening and clipped drawing helpers
  user_fs_shell.c/.h    Shell-facing FS, path, copy, SFTP path helpers
  user_editor.c/.h      Editor window opening / lazy load glue
  runtime/jit/          TCC/JIT runtime and debugger UI glue
  ports/mbedtls/        OS mbedTLS/TF-PSA compatibility layer
  apps/netsurf/         NetSurf/WRP frontend
  apps/net/wget/        HTTP client / WGET queue
  apps/ssh/             SSH settings, SSH exec, SFTP helpers
  apps/editor/          Text editor and asm tool implementation
  apps/demo3d/          Demo 3D / FPS drawing tools
  apps/gbemu/           Game Boy app wrapper and audio shim
  drivers/virtio/       virtio block, net, keyboard/tablet
  drivers/audio/        AC97 and virtio sound drivers
  third_party/lwip/     lwIP source tree
  third_party/gbemu/    Game Boy emulator C core
  third_party/wrp/      Modified WRP host-side server
```

## Host / Guest Network Configuration

QEMU uses the tap device:

```make
-netdev tap,id=net0,ifname=tap0,script=no,downscript=no
```

IP Planning:

| Endpoint | IP / port | Description |
| --- | --- | --- |
| QEMU guest OS | `192.168.123.1/24` | Defined in `drivers/virtio/virtio_net.c` |
| Host tap0 | `192.168.123.100/24` | Host running WRP server |
| WRP default URL | `http://192.168.123.100:9999` | Defined in `apps/ssh/ssh_client.c` |

## Build Instructions

Requires:

- `riscv64-unknown-elf-gcc`
- `qemu-system-riscv32`
- GNU make
- Pre-built mbedTLS RISC-V static libs (default at `/tmp/mbedtls-rv-build`)

To create the disk image (first time only):

```sh
dd if=/dev/urandom of=hdd.dsk bs=1M count=32
```

To build and run:

```sh
make -j2 os.elf
make qemu
```

## Development Notes

- After modifying `.h` files, a forced rebuild might be necessary: `make -B -j2 os.elf`.
- After modifying the WRP server, rebuild with `go build` and restart the server.
- The OS does not support direct modern JS/CSS; interaction is handled via WRP's ISMAP path.
- Input synchronization (clicks, scrolls) is managed via an OS-side action queue to avoid ID race conditions.

## License

This project integrates several open-source components:

- **vibe-os (this fork)**: Released into the public domain under **The Unlicense**.
- **mini-riscv-os**: Distributed under the **Two-clause BSD License**.
- **WRP**: Distributed under the **Apache License 2.0**.

For complete license texts, please see the [LICENSE](LICENSE) file.
