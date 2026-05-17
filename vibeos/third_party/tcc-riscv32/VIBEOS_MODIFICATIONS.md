# vibe-os TCC Modifications

This directory contains Tiny C Compiler (TCC) source code used by vibe-os for
the in-OS JIT compiler integration.

TCC is licensed under the GNU Lesser General Public License. See `COPYING` in
this directory for the full license text.

Local modifications in this vendored copy include:

- `tccelf.c`: adjusted RISC-V32 relocation/GOT handling for OS-added symbols
  registered through `tcc_add_symbol()`.
- `tccpp.c`: added defensive checks and debug logging around predefinition
  injection used by the JIT integration.

The OS integration code that links against TCC lives in:

- `../../tcc_glue.c`

