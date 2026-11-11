# FMAccel-full

**FMAccel: Accelerating Cross-ISA Binary Translation with Native Vector FMA**

FMAccel accelerates selected FMA instructions in x86-64 programs through QEMU user-mode translation on LoongArch, ARMv8, and RISC-V hosts. The shared `detector/detector.py` scans the executable offline and records instruction addresses and lengths in an analysis file. During translation, QEMU identifies these sites and invokes the corresponding host FMA implementation.

## 1. Repository layout

```text
FMAccel/
├── README.md
├── detector/
│   ├── detector.py
│   └── offline-analysis.txt
├── qemu-opt-loongarch/
├── qemu-opt-ARMv8/
├── qemu-opt-riscv/
├── qemu-count/
└── test/
    ├── benchmark-multest
    ├── benchmark-multest-no-fma
    └── test-recom/
```

| Directory | Purpose |
|---|---|
| `detector/` | Shared FMA detector for all three platforms |
| `qemu-opt-loongarch/` | LoongArch implementation using LSX/LASX |
| `qemu-opt-ARMv8/` | ARMv8 implementation using NEON |
| `qemu-opt-riscv/` | RISC-V implementation using RVV |
| `qemu-count/` | Dynamic instruction counting build used on LoongArch |
| `test/` | x86-64 benchmark executables |
| `test/test-recom/` | BLAS correctness tests, sources, and input files |

### Clone the complete project

This repository includes the complete source trees of all four QEMU variants, the detector, and the test programs directly, without Git submodules. A standard clone retrieves the full project.

## 2. Build

Run the commands on a Linux host of the selected architecture, with QEMU build dependencies, a C compiler supporting the required host extensions, Python 3, and GNU `objdump` capable of disassembling x86-64.

### 2.1 Initialize paths

Run from the FMAccel root; subsequent commands reuse these variables in the same shell.

```sh
FMACCEL_ROOT=$(pwd)
PROFILE="$FMACCEL_ROOT/detector/offline-analysis.txt"
```

### 2.2 Configure the profile path

Before building LoongArch, ARMv8, or `qemu-count`, replace the `fopen` path in that tree's `target/i386/tcg/fma_detector.c` with the actual absolute path to `detector/offline-analysis.txt`, matching `PROFILE` above. The `/path/to/...` strings are placeholders; rebuild after changing them.

RISC-V can select the same profile through an environment variable. Run the following before execution; no source-path change is needed. The other three builds use their compiled paths.

```sh
export FMA_PROFILE="$PROFILE"
```

### 2.3 Build the optimized version

Choose one configuration for the current host, then run the common build commands.

**LoongArch with LSX/LASX**

```sh
QEMU_TREE=qemu-opt-loongarch
QEMU_CFLAGS="-mlsx -mlasx"
```

**AArch64 (ARMv8) with NEON**

```sh
QEMU_TREE=qemu-opt-ARMv8
QEMU_CFLAGS="-march=armv8-a"
```

**RISC-V with the full V extension**

```sh
QEMU_TREE=qemu-opt-riscv
QEMU_CFLAGS="-march=rv64gcv -mabi=lp64d"
```

**Common build commands**

```sh
mkdir -p "$FMACCEL_ROOT/$QEMU_TREE/build"
(
    cd "$FMACCEL_ROOT/$QEMU_TREE/build"
    ../configure --target-list=x86_64-linux-user \
        --disable-rust --disable-tools \
        --disable-system --disable-docs \
        --extra-cflags="$QEMU_CFLAGS"
    make -j"$(nproc)"
)
OPT_QEMU="$FMACCEL_ROOT/$QEMU_TREE/build/qemu-x86_64"
```

### 2.4 Build the original QEMU

Prepare unmodified QEMU sources matching the selected optimized version and build the performance baseline with the same options. Replace the path below with the original QEMU source directory.

```sh
BASELINE_SRC=/absolute/path/to/original-qemu
mkdir -p "$BASELINE_SRC/build"
(
    cd "$BASELINE_SRC/build"
    ../configure --target-list=x86_64-linux-user \
        --disable-rust --disable-tools \
        --disable-system --disable-docs
    make -j"$(nproc)"
)
BASELINE_QEMU="$BASELINE_SRC/build/qemu-x86_64"
```

## 3. Detect FMA instructions

All three platforms use `detector/detector.py`. It takes one executable path, invokes `objdump -d -w`, and writes `offline-analysis.txt` in the current working directory. Run it from `detector/` as shown below.

```sh
GUEST="$FMACCEL_ROOT/test/benchmark-multest"
(
    cd "$FMACCEL_ROOT/detector"
    python3 detector.py "$GUEST"
)
```

Each record contains a hexadecimal guest virtual address followed by the instruction length in decimal bytes.

```text
META: 40b5c7 5
META: 40b5cc 5
```

Regenerate the profile whenever the tested executable changes or is rebuilt. The current workflow targets non-PIE x86-64 ELF executables with fixed addresses; the supplied benchmarks are statically linked ELF files. If no supported instructions are found, the detector empties the output file and exits with status `1`.

## 4. Performance evaluation

The benchmarks accept `<op> <N> <iter>`, where `op` is `dgemm`, `sgemm`, `dsyrk`, or `dgemv`, `N` is the problem size, and `iter` is the iteration count. Output includes `Avg_Time` and GFLOPS. Use the same executable, kernel, input size, and iteration count within each comparison.

### 4.1 FMA workload speedup

First generate the profile for `benchmark-multest` as described in Section 3, then run the original and optimized versions.

```sh
GUEST="$FMACCEL_ROOT/test/benchmark-multest"
"$BASELINE_QEMU" "$GUEST" dgemm 128 10
"$OPT_QEMU" "$GUEST" dgemm 128 10
```

### 4.2 Overhead on the no-FMA workload

Empty the loaded profile and run the same `benchmark-multest-no-fma` executable under original and optimized QEMU to measure end-to-end framework overhead. Compute this comparison between the two QEMU versions using that same executable.

```sh
: > "$PROFILE"
NOFMA_GUEST="$FMACCEL_ROOT/test/benchmark-multest-no-fma"
"$BASELINE_QEMU" "$NOFMA_GUEST" dgemm 128 10
"$OPT_QEMU" "$NOFMA_GUEST" dgemm 128 10
```

Before returning to an FMA workload, rerun the detector from Section 3 to restore its profile. Start a new QEMU process after changing profiles.

### 4.3 Metrics

Let `T_original` and `T_opt` denote execution times for identical work under original and optimized QEMU.

| Metric | Formula | Interpretation |
|---|---|---|
| Speedup | `T_original / T_opt` | Above 1 means faster |
| No-FMA time overhead | `(T_opt / T_original - 1) × 100%` | Positive means slower |
| No-FMA performance retention | `(T_original / T_opt) × 100%` | 100% means equal performance |

Prefer execution times when calculating ratios. To summarize multiple cases, take the geometric mean of unrounded speedup ratios and state the included kernels and sizes. Aggregate repeated runs consistently.

## 5. Correctness validation

`test/test-recom/` provides BLAS Level 1, 2, and 3 tests. Generate a profile for each test executable, then run it under original and optimized QEMU. Supply the corresponding `.dat` file where required, as in this `dblat2` example:

```sh
GUEST="$FMACCEL_ROOT/test/test-recom/dblat2"
(
    cd "$FMACCEL_ROOT/detector"
    python3 detector.py "$GUEST"
)
(
    cd "$FMACCEL_ROOT/test/test-recom"
    "$BASELINE_QEMU" ./dblat2 < dblat2.dat
    "$OPT_QEMU" ./dblat2 < dblat2.dat
)
```

Inspect pass/fail and numerical-error reports and compare the two versions' results. If the detector finds no supported FMA instructions in a test, running it with an empty profile does not validate the FMA acceleration path.

## 6. Dynamic instruction counting

The experiments use `qemu-count` on LoongArch to measure dynamic instruction executions and the target FMA share of the translated x86-64 program. This share describes the guest workload's instruction mix, rather than the LoongArch host instruction mix. With the same program, input, and execution path, it can inform workload analysis on the other platforms as well.

### 6.1 Build the counter

Configure the profile path in `qemu-count/target/i386/tcg/fma_detector.c` as described in Section 2.2, then build on the LoongArch host.

```sh
mkdir -p "$FMACCEL_ROOT/qemu-count/build"
(
    cd "$FMACCEL_ROOT/qemu-count/build"
    ../configure --target-list=x86_64-linux-user \
        --disable-rust --disable-tools \
        --disable-system --disable-docs \
        --extra-cflags="-mlsx -mlasx"
    make -j"$(nproc)"
)
COUNT_QEMU="$FMACCEL_ROOT/qemu-count/build/qemu-x86_64"
```

### 6.2 Run and read the counters

Regenerate the profile for the executable being measured, then launch the counter build.

```sh
GUEST="$FMACCEL_ROOT/test/benchmark-multest"
(
    cd "$FMACCEL_ROOT/detector"
    python3 detector.py "$GUEST"
)
"$COUNT_QEMU" "$GUEST" dgemm 128 10
```

When the program exits through `exit_group`, the counter reports these fields.

| Output field | Meaning |
|---|---|
| `FMA exec count` | Dynamic executions intercepted through the profile and target FMA helpers |
| `Total exec count` | Total dynamic guest instruction executions recorded by the counter |
| `FMA exec ratio` | `FMA exec count / Total exec count × 100%` |

The FMA count depends on detector coverage and the active profile. Instrumentation adds execution cost, so counter-build timings are not used to calculate optimization speedups.

## 7. Supported scope

The current detection and acceleration workflow covers VEX-encoded packed `vfmadd132ps/pd`, `vfmadd213ps/pd`, and `vfmadd231ps/pd`, with register and memory operands. Scalar FMA and EVEX/AVX-512 FMA are outside this workflow.

| Host | Implementation |
|---|---|
| LoongArch | LSX for 128-bit operands; LASX for 256-bit operands |
| ARMv8 | NEON, with two 128-bit blocks for 256-bit operands |
| RISC-V | RVV, with a SoftFloat fallback when the required compile-time features are absent |

Profiles use guest virtual addresses; PIE and shared-library relocation handling is not provided. Native FMA fast paths do not guarantee identical behavior to original QEMU for all special floating-point values and exception flags. Validate the intended workloads with the correctness tests above.

## 8. Licensing

Refer to `COPYING`, `COPYING.LIB`, and the applicable licensing files in each QEMU tree. Bundled dependencies and BLAS test material retain their respective licenses.
