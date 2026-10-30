# x86 packed FMA on SiFive U74-MC

Based on the supplied QEMU 11.0.1 tree. The U74 has F/D scalar floating
point, but no RVV. Selected packed VFMADD instructions call helpers that
use one `fmadd.s` or `fmadd.d` per lane, with a SoftFloat fallback.

The full implementation record is in `docs/opt-info-riscv-u74.md`.

On the RISC-V Linux host:

```sh
mkdir -p build-u74
cd build-u74
../configure --target-list=x86_64-linux-user \
  --extra-cflags="-march=rv64gc -mabi=lp64d"
make -j2
```

The guest must be a non-PIE x86-64 Linux ELF. Its FMA sites must be in the
profile, so regenerate the profile whenever the guest binary changes:

```sh
python3 contrib/fma-u74/detector.py /absolute/path/to/guest \
  --objdump llvm-objdump-14 -o /absolute/path/to/fma_profile.txt
FMA_PROFILE=/absolute/path/to/fma_profile.txt \
  ./build-u74/qemu-x86_64 -cpu max /absolute/path/to/guest
```

Clang 14 and ld.lld can build the included freestanding x86 test directly
on U74, without an x86 cross libc. From the project root:

```sh
CLANG=clang-14 LLVM_OBJDUMP=llvm-objdump-14 \
  sh contrib/fma-u74/validate.sh \
  /absolute/path/to/original/qemu-x86_64 ./build-u74/qemu-x86_64
```

The script compares result bit patterns and MXCSR, tests readable and
faulting page boundaries, and verifies that both optimized helper calls
appear in the translated TCG log. It is a correctness test, not a speed
benchmark. The target-host full build and runtime tests have not been
run on this Mac. The existing handwritten x86 address decoder is retained;
its limits are listed in the full implementation record.
