# Third-Party License Inventory

This repository includes original VibeOS FPGA work plus third-party and
vendored source. The top-level `LICENSE` applies to original project code only.
Third-party code keeps its own license.

If this inventory and a component-local license file disagree, the
component-local license file controls.

## Summary

| Component | Path | License / notice source |
| --- | --- | --- |
| VibeOS FPGA original code | repository files without another notice | The Unlicense; see `LICENSE` |
| VibeOS / mini-riscv-os derived code | `vibeos/` | Source repository: `https://github.com/x213212/vibeos.git`; Unlicense for local VibeOS contributions; mini-riscv-os portions under BSD-2-Clause; see `vibeos/LICENSE` |
| ultraembedded RISC-V core | `ultraembedded_riscv/` | BSD-3-Clause-style license; see `ultraembedded_riscv/LICENSE` and full text below |
| PicoRV32 | `picorv32/` | ISC license; see `picorv32/COPYING` |
| PicoRV32 test material | `picorv32/tests/` | BSD-3-Clause-style license; see `picorv32/tests/LICENSE` |
| lwIP | `vibeos/third_party/lwip/` | BSD-3-Clause-style license; see `vibeos/third_party/lwip/COPYING` |
| libssh2 | `vibeos/third_party/libssh2/` | BSD-style license; see `vibeos/third_party/libssh2/COPYING` |
| Mbed TLS / TF-PSA crypto | `vibeos/third_party/mbedtls/` | Apache-2.0 OR GPL-2.0-or-later; see `vibeos/third_party/mbedtls/LICENSE` and nested license files |
| Tiny C Compiler | `vibeos/third_party/tcc-riscv32/` | LGPL-2.1-or-later; see `vibeos/third_party/tcc-riscv32/COPYING` |
| WRP | `vibeos/third_party/wrp/` | Apache-2.0; see `vibeos/third_party/wrp/LICENSE` |
| libhubbub | `vibeos/third_party/libhubbub/` | MIT-style license; see `vibeos/third_party/libhubbub/COPYING` |
| libnsfb | `vibeos/third_party/libnsfb/` | MIT-style license; see `vibeos/third_party/libnsfb/COPYING` |
| libparserutils | `vibeos/third_party/libparserutils/` | MIT-style license; see `vibeos/third_party/libparserutils/COPYING` |
| libwapcaplet | `vibeos/third_party/libwapcaplet/` | MIT-style license; see `vibeos/third_party/libwapcaplet/COPYING` |
| gbemu vendored code | `vibeos/third_party/gbemu/` | No separate `LICENSE`, `LICENCE`, or `COPYING` file was found in the local vendored copy. Verify upstream licensing before redistributing this component independently. |
| zynq_z7lite_training reference copy | `project_2/zynq_z7lite_training/` | No explicit license file was found in the local copy. Verify upstream licensing before redistributing this reference material independently. |

Generated Vivado outputs, bitstreams, build directories, logs, and local backup
archives are not intended to be distributed as source license authorities.

## ultraembedded RISC-V License

The following text is copied from `ultraembedded_riscv/LICENSE`:

```text
Copyright (c) 2014, ultraembedded
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the {organization} nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Notes For Future Imports

When adding another third-party directory, keep the upstream license file in
that directory and add one row to the summary table above. Do not replace
third-party license text with the top-level Unlicense.
