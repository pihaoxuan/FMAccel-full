# FMAccel-full

**FMAccel: 利用原生向量 FMA 加速跨指令集二进制翻译**  
**FMAccel: Accelerating Cross-ISA Binary Translation with Native Vector FMA**

FMAccel 基于 QEMU 用户态翻译，在龙芯 LoongArch、ARMv8 和 RISC-V 平台上加速 x86-64 程序中的指定 FMA 指令。统一使用 `detector/detector.py` 离线检测目标程序，生成指令地址和长度组成的 analysis file；QEMU 在翻译时识别这些位置，并调用对应平台的 FMA 实现。

FMAccel accelerates selected FMA instructions in x86-64 programs through QEMU user-mode translation on LoongArch, ARMv8, and RISC-V hosts. The shared `detector/detector.py` scans the executable offline and records instruction addresses and lengths in a analysiss file. During translation, QEMU identifies these sites and invokes the corresponding host FMA implementation.

## 1. 目录说明 / Repository layout

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

| 目录 / Directory | 用途 / Purpose |
|---|---|
| `detector/` | 三个平台共用的 FMA 检测工具 / Shared FMA detector for all three platforms |
| `qemu-opt-loongarch/` | 基于 LSX/LASX 的龙芯优化版本 / LoongArch implementation using LSX/LASX |
| `qemu-opt-ARMv8/` | 基于 NEON 的 ARMv8 优化版本 / ARMv8 implementation using NEON |
| `qemu-opt-riscv/` | 基于 RVV 的 RISC-V 优化版本 / RISC-V implementation using RVV |
| `qemu-count/` | 在龙芯平台运行的动态指令计数版本 / Dynamic instruction counting build used on LoongArch |
| `test/` | x86-64 性能测试程序 / x86-64 benchmark executables |
| `test/test-recom/` | BLAS 正确性测试程序、源码和输入文件 / BLAS correctness tests, sources, and input files |

### 获取完整项目 / Clone the complete project

本仓库直接包含四个 QEMU 目录的完整源码、检测工具和测试程序，不使用 Git 子模块。使用普通克隆命令即可获取全部内容。

This repository includes the complete source trees of all four QEMU variants, the detector, and the test programs directly, without Git submodules. A standard clone retrieves the full project.

## 2. 编译 / Build

以下命令在对应平台的 Linux 主机上执行。准备好 QEMU 编译依赖、支持对应平台指令扩展的 C 编译器，以及 Python 3 和能够反汇编 x86-64 的 GNU `objdump`。

Run the commands on a Linux host of the selected architecture, with QEMU build dependencies, a C compiler supporting the required host extensions, Python 3, and GNU `objdump` capable of disassembling x86-64.

### 2.1 路径初始化 / Initialize paths

在 FMAccel 根目录执行，后续命令在同一 shell 中使用这些变量。

Run from the FMAccel root; subsequent commands reuse these variables in the same shell.

```sh
FMACCEL_ROOT=$(pwd)
PROFILE="$FMACCEL_ROOT/detector/offline-analysis.txt"
```

### 2.2 配置 profile 路径 / Configure the profile path

编译 LoongArch、ARMv8 或 `qemu-count` 前，将对应目录下 `target/i386/tcg/fma_detector.c` 中的 `fopen` 路径替换为本机 `detector/offline-analysis.txt` 的真实绝对路径，即上面 `PROFILE` 的值。源码中的 `/path/to/...` 是占位路径；修改后需要重新编译。

Before building LoongArch, ARMv8, or `qemu-count`, replace the `fopen` path in that tree's `target/i386/tcg/fma_detector.c` with the actual absolute path to `detector/offline-analysis.txt`, matching `PROFILE` above. The `/path/to/...` strings are placeholders; rebuild after changing them.

RISC-V 支持通过环境变量指定同一个 profile，在运行前执行以下命令即可，无需修改源码路径。其他三个版本仍使用编译时指定的路径。

RISC-V can select the same profile through an environment variable. Run the following before execution; no source-path change is needed. The other three builds use their compiled paths.

```sh
export FMA_PROFILE="$PROFILE"
```

### 2.3 编译优化版本 / Build the optimized version

按当前主机平台选择下面的一组配置，然后执行共用编译命令。

Choose one configuration for the current host, then run the common build commands.

**龙芯 LoongArch / LoongArch with LSX/LASX**

```sh
QEMU_TREE=qemu-opt-loongarch
QEMU_CFLAGS="-mlsx -mlasx"
```

**ARMv8 / AArch64 with NEON**

```sh
QEMU_TREE=qemu-opt-ARMv8
QEMU_CFLAGS="-march=armv8-a"
```

**RISC-V / RISC-V with the full V extension**

```sh
QEMU_TREE=qemu-opt-riscv
QEMU_CFLAGS="-march=rv64gcv -mabi=lp64d"
```

**共用编译命令 / Common build commands**

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

### 2.4 编译原始 QEMU / Build the original QEMU

准备与所选优化版本对应的未修改 QEMU 源码，采用相同编译选项构建性能基线。将下面路径替换为原始 QEMU 的源码目录。

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

## 3. 检测 FMA 指令 / Detect FMA instructions

三个平台均使用 `detector/detector.py`。它接收一个可执行文件路径，调用 `objdump -d -w`，并将结果写入当前工作目录中的 `offline-analysis.txt`。因此，检测时进入 `detector/` 目录。

All three platforms use `detector/detector.py`. It takes one executable path, invokes `objdump -d -w`, and writes `offline-analysis.txt` in the current working directory. Run it from `detector/` as shown below.

```sh
GUEST="$FMACCEL_ROOT/test/benchmark-multest"
(
    cd "$FMACCEL_ROOT/detector"
    python3 detector.py "$GUEST"
)
```

输出格式如下：第一项为十六进制的客户程序虚拟地址，第二项为十进制的指令字节长度。

Each record contains a hexadecimal guest virtual address followed by the instruction length in decimal bytes.

```text
META: 40b5c7 5
META: 40b5cc 5
```

更换或重新编译被测程序后，需要重新生成 profile。当前流程面向地址固定的非 PIE x86-64 ELF 程序；随附性能测试程序为静态链接 ELF。若未检测到支持的指令，脚本会清空输出文件并以状态码 `1` 退出。

Regenerate the profile whenever the tested executable changes or is rebuilt. The current workflow targets non-PIE x86-64 ELF executables with fixed addresses; the supplied benchmarks are statically linked ELF files. If no supported instructions are found, the detector empties the output file and exits with status `1`.

## 4. 性能测试 / Performance evaluation

测试程序接收 `<op> <N> <iter>`：`op` 可选 `dgemm`、`sgemm`、`dsyrk`、`dgemv`，`N` 为问题规模，`iter` 为迭代次数。输出包含 `Avg_Time` 和 GFLOPS。同组对比使用相同程序、kernel、输入规模和迭代次数。

The benchmarks accept `<op> <N> <iter>`, where `op` is `dgemm`, `sgemm`, `dsyrk`, or `dgemv`, `N` is the problem size, and `iter` is the iteration count. Output includes `Avg_Time` and GFLOPS. Use the same executable, kernel, input size, and iteration count within each comparison.

### 4.1 FMA 程序加速效果 / FMA workload speedup

先按第 3 节为 `benchmark-multest` 生成 profile，再分别运行原始和优化版本。

First generate the profile for `benchmark-multest` as described in Section 3, then run the original and optimized versions.

```sh
GUEST="$FMACCEL_ROOT/test/benchmark-multest"
"$BASELINE_QEMU" "$GUEST" dgemm 128 10
"$OPT_QEMU" "$GUEST" dgemm 128 10
```

### 4.2 无 FMA 程序开销 / Overhead on the no-FMA workload

将加载的 profile 清空，在原始和优化 QEMU 下运行同一个 `benchmark-multest-no-fma`，评估框架引入的端到端开销。该对比不使用两个不同的测试程序互相比值。

Empty the loaded profile and run the same `benchmark-multest-no-fma` executable under original and optimized QEMU to measure end-to-end framework overhead. Compute this comparison between the two QEMU versions using that same executable.

```sh
: > "$PROFILE"
NOFMA_GUEST="$FMACCEL_ROOT/test/benchmark-multest-no-fma"
"$BASELINE_QEMU" "$NOFMA_GUEST" dgemm 128 10
"$OPT_QEMU" "$NOFMA_GUEST" dgemm 128 10
```

返回 FMA 测试前，重新执行第 3 节的检测命令，恢复对应程序的 profile。每次切换 profile 后启动新的 QEMU 进程。

Before returning to an FMA workload, rerun the detector from Section 3 to restore its profile. Start a new QEMU process after changing profiles.

### 4.3 指标计算 / Metrics

设 `T_original` 和 `T_opt` 分别为相同工作量在原始和优化 QEMU 下的执行时间。

Let `T_original` and `T_opt` denote execution times for identical work under original and optimized QEMU.

| 指标 / Metric | 计算方式 / Formula | 含义 / Interpretation |
|---|---|---|
| 加速比 / Speedup | `T_original / T_opt` | 大于 1 表示加速 / Above 1 means faster |
| 无 FMA 时间开销 / No-FMA time overhead | `(T_opt / T_original - 1) × 100%` | 正值表示变慢 / Positive means slower |
| 无 FMA 性能保留率 / No-FMA performance retention | `(T_original / T_opt) × 100%` | 100% 表示性能相同 / 100% means equal performance |

优先使用执行时间计算比值；汇总多个测试点时，对未舍入的加速比计算几何平均，并注明包含的 kernel 和规模。重复运行时采用一致的汇总方式。

Prefer execution times when calculating ratios. To summarize multiple cases, take the geometric mean of unrounded speedup ratios and state the included kernels and sizes. Aggregate repeated runs consistently.

## 5. 正确性验证 / Correctness validation

`test/test-recom/` 提供 BLAS Level 1、2、3 测试。对每个测试可执行文件单独生成 profile，再分别运行原始和优化 QEMU。需要输入文件的测试应使用对应 `.dat` 文件，例如 `dblat2`：

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

检查测试输出中的通过、失败和数值误差信息，并比较两个版本的结果。若该测试没有检测到支持的 FMA 指令，空 profile 下的运行不会验证 FMA 加速路径。

Inspect pass/fail and numerical-error reports and compare the two versions' results. If the detector finds no supported FMA instructions in a test, running it with an empty profile does not validate the FMA acceleration path.

## 6. 动态指令统计 / Dynamic instruction counting

本实验选择在龙芯 LoongArch 平台上使用 `qemu-count`，统计被翻译 x86-64 程序的动态指令执行次数和目标 FMA 占比。该占比描述的是被测程序在给定工作负载下的指令构成，不是龙芯宿主指令构成；在程序、输入和执行路径相同的条件下，可用于其他平台的工作负载分析。

The experiments use `qemu-count` on LoongArch to measure dynamic instruction executions and the target FMA share of the translated x86-64 program. This share describes the guest workload's instruction mix, rather than the LoongArch host instruction mix. With the same program, input, and execution path, it can inform workload analysis on the other platforms as well.

### 6.1 编译计数版本 / Build the counter

先按第 2.2 节配置 `qemu-count/target/i386/tcg/fma_detector.c` 的 profile 路径，然后在龙芯主机上编译。

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

### 6.2 运行并读取统计 / Run and read the counters

为当前被测程序重新生成 profile，然后启动计数版本。

Regenerate the profile for the executable being measured, then launch the counter build.

```sh
GUEST="$FMACCEL_ROOT/test/benchmark-multest"
(
    cd "$FMACCEL_ROOT/detector"
    python3 detector.py "$GUEST"
)
"$COUNT_QEMU" "$GUEST" dgemm 128 10
```

程序经 `exit_group` 退出时，会输出以下统计字段。

When the program exits through `exit_group`, the counter reports these fields.

| 输出字段 / Output field | 含义 / Meaning |
|---|---|
| `FMA exec count` | profile 命中并进入目标 FMA helper 的动态执行次数 / Dynamic executions intercepted through the profile and target FMA helpers |
| `Total exec count` | 计数器记录的客户程序动态指令执行次数 / Total dynamic guest instruction executions recorded by the counter |
| `FMA exec ratio` | `FMA exec count / Total exec count × 100%` |

FMA 计数受检测范围和当前 profile 影响。计数版本包含额外插桩，其执行时间不用于优化加速比计算。

The FMA count depends on detector coverage and the active profile. Instrumentation adds execution cost, so counter-build timings are not used to calculate optimization speedups.

## 7. 支持范围 / Supported scope

当前检测与加速流程覆盖 VEX 编码的 packed `vfmadd132ps/pd`、`vfmadd213ps/pd` 和 `vfmadd231ps/pd`，包括寄存器和内存操作数；不包含标量 FMA 和 EVEX/AVX-512 FMA。

The current detection and acceleration workflow covers VEX-encoded packed `vfmadd132ps/pd`, `vfmadd213ps/pd`, and `vfmadd231ps/pd`, with register and memory operands. Scalar FMA and EVEX/AVX-512 FMA are outside this workflow.

| 平台 / Host | 实现 / Implementation |
|---|---|
| LoongArch | 128 位使用 LSX，256 位使用 LASX / LSX for 128-bit operands; LASX for 256-bit operands |
| ARMv8 | 使用 NEON；256 位操作数分为两个 128 位块 / NEON, with two 128-bit blocks for 256-bit operands |
| RISC-V | 使用 RVV；未启用相应编译特性时回退到 SoftFloat / RVV, with a SoftFloat fallback when the required compile-time features are absent |

profile 使用客户程序虚拟地址，当前未提供 PIE 或共享库的重定位处理。原生 FMA 快速路径也不保证所有特殊浮点值和异常标志行为与原始 QEMU 完全一致；具体工作负载应通过上述正确性测试进行验证。

Profiles use guest virtual addresses; PIE and shared-library relocation handling is not provided. Native FMA fast paths do not guarantee identical behavior to original QEMU for all special floating-point values and exception flags. Validate the intended workloads with the correctness tests above.

## 8. 许可证 / Licensing

各 QEMU 目录中的 `COPYING`、`COPYING.LIB` 及相关文件规定其许可条款。随附依赖和 BLAS 测试材料保留各自的许可证。

Refer to `COPYING`, `COPYING.LIB`, and the applicable licensing files in each QEMU tree. Bundled dependencies and BLAS test material retain their respective licenses.
