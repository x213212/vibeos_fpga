# Stable VibeOS SSH/HTTP Snapshot - 2026-05-17

This snapshot is the version confirmed on hardware after the user reported SSH working.

## Verified State

- FPGA IP: `192.168.0.154`
- Host-side ping from `192.168.0.152` succeeds.
- HTTP works:

```text
curl http://192.168.0.154/
<html><body><h1>VibeOS Ethernet OK</h1><p>lwIP + Zynq GEM0 is running.</p><p>IP: 192.168.0.154</p></body></html>
```

- SSH command path was confirmed working by the user after loading the current `vibeos/os.bin`.

## Stable Load Flow

Do not rebuild the bitstream for normal OS testing. Use the stable Ethernet/USB loader:

```powershell
powershell.exe -ExecutionPolicy Bypass -File H:\testproject\project_2\release_vibeos_eth_stable.ps1 H:/testproject/vibeos/os.bin
```

After loading, verify Ethernet:

```powershell
ping -S 192.168.0.152 -n 3 192.168.0.154
curl.exe --max-time 5 http://192.168.0.154/
```

## SSH Notes

The working direction is ECC/Suite-B:

```text
KEX:      ecdh-sha2-nistp256
HOSTKEY:  ecdsa-sha2-nistp256
CIPHER:   aes128-ctr
MAC:      hmac-sha2-256
```

Do not switch back to `diffie-hellman-group14-*`: the bare-metal mbedTLS build is not configured for 2048-bit DH.

Important fixes present in this tree:

- `libssh2_init()` is kept for the OS lifetime instead of calling `libssh2_exit()` after every command.
- TCP transport closes with `altcp_close()` first, and only aborts as fallback.
- SSH diagnostics include inbound packet state and outbound encrypted packet state.
- `SSH_MSG_EXT_INFO` after NEWKEYS is allowed and skipped while waiting for the requested packet.
- AES-CTR backend must not reset the CTR stream for every block.

## Do Not Regress

- Do not reload random/older bitstreams when debugging SSH. Keep the stable Ethernet/USB bitstream unless changing PL hardware intentionally.
- Do not change USB init/reset Tcl while debugging SSH.
- Do not remove the command history entries for SSH probe/set/auth/exec.
- If SSH fails again, first record the full diagnostic line including:

```text
sst sret nb pkt plen mac lseq rseq opkt oseq oplen opad ototal oret oenc tcp_rx ssh_tx ssh_rx seagain reagain
```

## Key Files

- `vibeos/apps/ssh/ssh_client.c`
- `vibeos/third_party/libssh2/src/session.c`
- `vibeos/third_party/libssh2/src/transport.c`
- `vibeos/third_party/libssh2/src/packet.c`
- `vibeos/third_party/libssh2/src/mbedtls.c`
- `project_2/STABLE_UI_ETH_MINIMAL_SOP.md`
- `project_2/release_vibeos_eth_stable.ps1`
