#!/usr/bin/env python3
"""Extract a flat flash image from an RP2040/RP2350 UF2 file.

The resulting .bin is the flash contents as mapped at 0x10000000 on the
RP2040 (or wherever the UF2 target addresses point). This is only a helper
for analysis; it never writes to hardware.
"""
import argparse
import struct
from pathlib import Path

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30


def parse_uf2(path: Path):
    data = path.read_bytes()
    blocks = []
    for offset in range(0, len(data) - 511, 512):
        block = data[offset : offset + 512]
        magic0, magic1, flags, addr, size, block_no, num_blocks, family = struct.unpack_from(
            "<IIIIIIII", block, 0
        )
        if magic0 != UF2_MAGIC0 or magic1 != UF2_MAGIC1:
            continue
        if struct.unpack_from("<I", block, 508)[0] != UF2_MAGIC_END:
            continue
        payload = block[32 : 32 + size]
        blocks.append((addr, payload))

    if not blocks:
        raise ValueError(f"no UF2 blocks found in {path}")

    base = min(addr for addr, _ in blocks)
    end = max(addr + len(payload) for addr, payload in blocks)
    image = bytearray(end - base)
    for addr, payload in blocks:
        image[addr - base : addr - base + len(payload)] = payload
    return base, bytes(image), len(blocks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("uf2", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()

    base, image, blocks = parse_uf2(args.uf2)
    output = args.output or args.uf2.with_suffix(".bin")
    output.write_bytes(image)
    print(f"{args.uf2}: base=0x{base:08X} size=0x{len(image):X} blocks={blocks} -> {output}")


if __name__ == "__main__":
    main()
