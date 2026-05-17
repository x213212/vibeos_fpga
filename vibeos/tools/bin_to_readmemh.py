#!/usr/bin/env python3
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: bin_to_readmemh.py <input.bin> <output.hex>", file=sys.stderr)
        return 2

    with open(sys.argv[1], "rb") as f:
        data = f.read()

    with open(sys.argv[2], "w", encoding="ascii") as f:
        for off in range(0, len(data), 4):
            chunk = data[off:off + 4].ljust(4, b"\x00")
            word = int.from_bytes(chunk, "little")
            f.write(f"{word:08x}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
