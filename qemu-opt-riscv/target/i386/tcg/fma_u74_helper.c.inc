/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Packed x86 FMA: LoongArch-style direct RVV fast path. */

/* RC[14:13], FTZ[15], DAZ[6]. */
#define CUSTOM_FMA_MXCSR_NONDEFAULT 0xE040U

#if defined(__riscv) && __riscv_xlen == 64 && \
    defined(__riscv_flen) && __riscv_flen >= 64 && defined(__riscv_vector)
#define CUSTOM_FMA_RISCV_RVV 1
#else
#define CUSTOM_FMA_RISCV_RVV 0
#endif

#if CUSTOM_FMA_RISCV_RVV
/*
 * Build for the full V extension (-march=rv64gcv), not a Zve subset.
 * V guarantees VLEN >= 128: LMUL=2 holds all 8 FP32 or 4 FP64 lanes.
 * The requested VL limits stores to the guest's 128/256-bit operand.
 * Load all three sources before storing, including when d aliases a source.
 *
 * Like the LoongArch fast path, this deliberately skips special-value checks
 * and guest exception-flag updates. NaN payloads and guest FP flags therefore
 * need not match SoftFloat. Nondefault guest controls use SoftFloat below.
 * RVV uses dynamic rounding, so force RNE and restore the host's full fcsr.
 */
static inline __attribute__((always_inline)) void
custom_fma_rvv32(ZMMReg *d, const ZMMReg *x, const ZMMReg *y,
                 const ZMMReg *z, unsigned long n)
{
    unsigned long saved_fcsr;

    asm volatile("csrrw %[saved], fcsr, zero\n\t"
                 "vsetvli zero, %[n], e32, m2, ta, ma\n\t"
                 "vle32.v v8, (%[x])\n\t"
                 "vle32.v v10, (%[y])\n\t"
                 "vle32.v v12, (%[z])\n\t"
                 "vfmacc.vv v12, v8, v10\n\t"
                 "vse32.v v12, (%[d])\n\t"
                 "csrw fcsr, %[saved]"
                 : [saved] "=&r" (saved_fcsr)
                 : [d] "r" (d), [x] "r" (x), [y] "r" (y),
                   [z] "r" (z), [n] "r" (n)
                 : "memory", "v8", "v9", "v10", "v11", "v12", "v13");
}

static inline __attribute__((always_inline)) void
custom_fma_rvv64(ZMMReg *d, const ZMMReg *x, const ZMMReg *y,
                 const ZMMReg *z, unsigned long n)
{
    unsigned long saved_fcsr;

    asm volatile("csrrw %[saved], fcsr, zero\n\t"
                 "vsetvli zero, %[n], e64, m2, ta, ma\n\t"
                 "vle64.v v8, (%[x])\n\t"
                 "vle64.v v10, (%[y])\n\t"
                 "vle64.v v12, (%[z])\n\t"
                 "vfmacc.vv v12, v8, v10\n\t"
                 "vse64.v v12, (%[d])\n\t"
                 "csrw fcsr, %[saved]"
                 : [saved] "=&r" (saved_fcsr)
                 : [d] "r" (d), [x] "r" (x), [y] "r" (y),
                   [z] "r" (z), [n] "r" (n)
                 : "memory", "v8", "v9", "v10", "v11", "v12", "v13");
}
#endif

static inline __attribute__((always_inline)) void
custom_fma_compute(CPUX86State *env, ZMMReg *d, const ZMMReg *a,
                   const ZMMReg *b, uint32_t flags)
{
    uint32_t opcode = flags & 0xff;
    bool is_double = (flags >> 8) & 1;
    bool is_256 = (flags >> 9) & 1;
    int n = is_double ? (is_256 ? 4 : 2) : (is_256 ? 8 : 4);
    const ZMMReg *x, *y, *z;
    int i;

    /* Preserve the original QEMU operand order for the SoftFloat fallback. */
    switch (opcode) {
    case 0xb8:                      /* VFMADD231: a * b + d */
        x = a;
        y = b;
        z = d;
        break;
    case 0xa8:                      /* VFMADD213: a * d + b */
        x = a;
        y = d;
        z = b;
        break;
    case 0x98:                      /* VFMADD132: d * b + a */
        x = d;
        y = b;
        z = a;
        break;
    default:
        g_assert_not_reached();
    }

#if CUSTOM_FMA_RISCV_RVV
    if (likely(!(env->mxcsr & CUSTOM_FMA_MXCSR_NONDEFAULT))) {
        if (is_double) {
            custom_fma_rvv64(d, x, y, z, n);
        } else {
            custom_fma_rvv32(d, x, y, z, n);
        }
        return;
    }
#endif

    /* Each lane depends only on the same source lane; aliases are safe. */
    for (i = 0; i < n; i++) {
        if (is_double) {
            d->ZMM_Q(i) = float64_muladd(x->ZMM_Q(i), y->ZMM_Q(i),
                                        z->ZMM_Q(i), 0, &env->sse_status);
        } else {
            d->ZMM_L(i) = float32_muladd(x->ZMM_L(i), y->ZMM_L(i),
                                        z->ZMM_L(i), 0, &env->sse_status);
        }
    }
}

void helper_custom_fma_vector_reg(CPUX86State *env, uint32_t dofs,
                                  uint32_t aofs, uint32_t bofs, uint32_t flags)
{
    ZMMReg *d = (ZMMReg *)((uint8_t *)env + dofs);
    const ZMMReg *a = (const ZMMReg *)((uint8_t *)env + aofs);
    const ZMMReg *b = (const ZMMReg *)((uint8_t *)env + bofs);

    custom_fma_compute(env, d, a, b, flags);
}

void helper_custom_fma_vector_mem(CPUX86State *env, uint32_t dofs,
                                  uint32_t aofs, target_ulong vaddr,
                                  uint32_t flags)
{
    uintptr_t ra = GETPC();
    ZMMReg mem_buf QEMU_ALIGNED(32);
    ZMMReg *d = (ZMMReg *)((uint8_t *)env + dofs);
    const ZMMReg *a = (const ZMMReg *)((uint8_t *)env + aofs);

    /* Finish all guest reads before changing the destination or host fcsr. */
    mem_buf.ZMM_Q(0) = cpu_ldq_data_ra(env, vaddr, ra);
    mem_buf.ZMM_Q(1) = cpu_ldq_data_ra(env, vaddr + 8, ra);
    if ((flags >> 9) & 1) {
        mem_buf.ZMM_Q(2) = cpu_ldq_data_ra(env, vaddr + 16, ra);
        mem_buf.ZMM_Q(3) = cpu_ldq_data_ra(env, vaddr + 24, ra);
    }
    custom_fma_compute(env, d, a, &mem_buf, flags);
}
