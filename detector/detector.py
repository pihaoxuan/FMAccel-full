import subprocess
import sys
import re

def analyze_binary(binary_path):
    print(f"[Detector] Scanning binary for ALL vfmadd instructions: {binary_path}...", file=sys.stderr)

    result = subprocess.run(
        ['objdump', '-d', '-w',  binary_path],
        capture_output=True,
        text=True,
        check=True
    )

    insn_pattern = re.compile(
        r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s)+)\s*(\S+)\s*(.*)',
        re.IGNORECASE
    )

    fma_instructions = []

    # ：
    for line in result.stdout.splitlines():
        match = insn_pattern.match(line)
        if not match:
            continue

        addr = int(match.group(1), 16)
        bytes_str = match.group(2).strip()
        insn_len = len(bytes_str.split())
        mnemonic = match.group(3).lower()

        # ： vfmadd
        # if 'vfmadd' in mnemonic:
        if re.fullmatch(r'vfmadd(?:132|213|231)(?:ps|pd)',  mnemonic):
            first_byte = bytes_str.split()[0].lower()
            if first_byte == 'c4':
                fma_instructions.append({
                    'offset': addr,
                    'len': insn_len,  #  ( 5)
                    'mnemonic': mnemonic
                })

    # ： Profile
    if fma_instructions:
        with open("./offline-analysis.txt", "w") as f:
            for insn in fma_instructions:
                # ：META: <16> <10>
                f.write(f"META: {insn['offset']:x} {insn['len']}\n")
                
        print(f"\n[Success] Wrote {len(fma_instructions)} FMA instructions to offline-analysis.txt\n")
    else:
        open("./offline-analysis.txt","w").close()
        print("Error: No vfmadd instructions found in the binary.")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 detector.py <binary>", file=sys.stderr)
        sys.exit(1)
    analyze_binary(sys.argv[1])
