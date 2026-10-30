#!/usr/bin/env python3
"""Write packed VEX VFMADD virtual addresses for a non-PIE x86-64 ELF."""

import argparse
from pathlib import Path
import re
import struct
import subprocess
import sys


INSN_PATTERN = re.compile(
    r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s)+)\s*(\S+)\s*(.*)",
    re.IGNORECASE,
)
FMA_PATTERN = re.compile(r"vfmadd(?:132|213|231)(?:ps|pd)")


def find_fma(disassembly):
    instructions = []
    for line in disassembly.splitlines():
        match = INSN_PATTERN.match(line)
        if match is None or FMA_PATTERN.fullmatch(match[3].lower()) is None:
            continue
        raw = bytes.fromhex(match[2])
        if (5 <= len(raw) <= 15 and raw[0] == 0xC4 and
                (raw[1] & 0x1F) == 2 and (raw[2] & 3) == 1 and
                raw[3] in (0x98, 0xA8, 0xB8)):
            instructions.append((int(match[1], 16), len(raw)))
    return instructions


def analyze_binary(binary, output, objdump):
    with Path(binary).open("rb") as stream:
        header = stream.read(64)
    if (len(header) < 64 or header[:6] != b"\x7fELF\x02\x01" or
            struct.unpack_from("<HH", header, 16) != (2, 62)):
        raise ValueError("Expected non-PIE x86-64 ELF (ET_EXEC); "
                         "link the guest with -no-pie.")
    result = subprocess.run([objdump, "-d", binary], check=True,
                            capture_output=True, text=True)
    instructions = find_fma(result.stdout)
    if not instructions:
        raise ValueError("No supported packed VEX VFMADD instructions found.")
    Path(output).write_text("".join(
        f"META: {address:x} {length}\n" for address, length in instructions
    ), encoding="ascii")
    print(f"[Detector] Wrote {len(instructions)} instructions to {output}",
          file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("-o", "--output", default="fma_profile.txt")
    parser.add_argument("--objdump", default="llvm-objdump")
    args = parser.parse_args()
    try:
        analyze_binary(args.binary, args.output, args.objdump)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"[Detector] {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
