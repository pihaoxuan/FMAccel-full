#!/bin/sh
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "Usage: sh validate.sh ORIGINAL_QEMU OPTIMIZED_QEMU [X86_ELF]" >&2
    exit 2
fi
original=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
optimized=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output=${FMA_TEST_OUTPUT:-"$PWD/fma-u74-results"}
mkdir -p "$output"
output=$(cd "$output" && pwd)

if [ "$#" -eq 3 ]; then
    guest=$(cd "$(dirname "$3")" && pwd)/$(basename "$3")
else
    guest="$output/fma-regression"
    # Clang 14 + ld.lld can emit x86-64 ELF on U74, without cross libc.
    "${CLANG:-clang-14}" --target=x86_64-linux-gnu -fuse-ld=lld \
        -O2 -mavx -mfma -ffreestanding -fno-builtin -fno-stack-protector \
        -fno-pie -no-pie -nostdlib -fno-vectorize -fno-slp-vectorize \
        -Wl,-e,_start -Wl,--build-id=none \
        "$script_dir/fma-regression.c" -o "$guest"
fi

python3 "$script_dir/detector.py" "$guest" \
    --objdump "${LLVM_OBJDUMP:-llvm-objdump-14}" -o "$output/fma_profile.txt"
FMA_PROFILE=/dev/null "$original" -cpu max "$guest" \
    > "$output/original.txt" 2> "$output/original.stderr"
FMA_PROFILE="$output/fma_profile.txt" "$optimized" -cpu max \
    -d op -D "$output/optimized-tcg.log" "$guest" \
    > "$output/optimized.txt" 2> "$output/optimized.stderr"
cmp "$output/original.txt" "$output/optimized.txt"
grep -q 'custom_fma_vector_reg' "$output/optimized-tcg.log"
grep -q 'custom_fma_vector_mem' "$output/optimized-tcg.log"
echo "PASS: guest outputs/MXCSR match; both optimized helpers were translated."
echo "Results: $output"
echo "Hardware fflags preservation and speed still require separate U74 checks."
