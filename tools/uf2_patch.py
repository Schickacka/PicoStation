#!/usr/bin/env python3
"""Apply byte patches to the flash payload of an RP2040/RP2350 UF2 file.

Patches are expressed as flash offsets (relative to the UF2 base address) and
hex byte strings, e.g.:

    tools/uf2_patch.py in.uf2 out.uf2 0x4D7=10 0x34572=00bf

The UF2 block structure is preserved; only the payload bytes change. UF2 has
no checksum, so no other fields need updating. This only rewrites the output
file; it never touches hardware.
"""
import argparse
import struct
from pathlib import Path

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30


def load_blocks(data: bytes):
    blocks = []
    for offset in range(0, len(data) - 511, 512):
        block = bytearray(data[offset : offset + 512])
        magic0, magic1, flags, addr, size, block_no, num_blocks, family = struct.unpack_from(
            "<IIIIIIII", block, 0
        )
        if magic0 != UF2_MAGIC0 or magic1 != UF2_MAGIC1:
            raise ValueError(f"bad UF2 magic at file offset 0x{offset:X}")
        if struct.unpack_from("<I", block, 508)[0] != UF2_MAGIC_END:
            raise ValueError(f"bad UF2 end magic at file offset 0x{offset:X}")
        blocks.append((addr, size, block))
    if not blocks:
        raise ValueError("no UF2 blocks")
    return blocks


def apply_patch(blocks, flash_offset: int, payload: bytes):
    target = min(addr for addr, _, _ in blocks) + flash_offset
    for addr, size, block in blocks:
        if addr <= target and target + len(payload) <= addr + size:
            start = 32 + (target - addr)
            block[start : start + len(payload)] = payload
            return
    raise ValueError(f"flash offset 0x{flash_offset:X} is not inside a UF2 payload")


def parse_patch(text: str):
    offset_s, hex_s = text.split("=", 1)
    return int(offset_s, 0), bytes.fromhex(hex_s)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("patches", nargs="+", help="OFFSET=HEXBYTES, e.g. 0x4D7=10")
    args = parser.parse_args()

    original = args.input.read_bytes()
    blocks = load_blocks(original)

    for text in args.patches:
        offset, payload = parse_patch(text)
        apply_patch(blocks, offset, payload)
        print(f"patched flash 0x{offset:06X}: {payload.hex()}")

    output = bytearray()
    for _, _, block in blocks:
        output += block
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"wrote {args.output} ({len(output)} bytes)")


if __name__ == "__main__":
    main()
