/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Freestanding x86-64 Linux guest: no cross libc/sysroot needed. */
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long s64;
typedef union {
    u32 words[8];
    u64 quads[4];
    u8 bytes[32];
} Vec;

void *memcpy(void *dst, const void *src, unsigned long size)
{
    u8 *d = dst;
    const u8 *s = src;
    unsigned long i;
    for (i = 0; i < size; i++) {
        d[i] = s[i];
    }
    return dst;
}

void *memset(void *dst, int value, unsigned long size)
{
    u8 *d = dst;
    unsigned long i;
    for (i = 0; i < size; i++) {
        d[i] = value;
    }
    return dst;
}

static s64 syscall6(u64 number, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f)
{
    register u64 r10 asm("r10") = d;
    register u64 r8 asm("r8") = e;
    register u64 r9 asm("r9") = f;
    asm volatile("syscall" : "+a" (number)
                 : "D" (a), "S" (b), "d" (c), "r" (r10), "r" (r8), "r" (r9)
                 : "rcx", "r11", "memory");
    return (s64)number;
}

static void text(const char *s)
{
    u64 length = 0;
    while (s[length]) {
        length++;
    }
    syscall6(1, 1, (u64)s, length, 0, 0, 0);
}

static void hex(u64 value, unsigned digits)
{
    char buffer[16];
    const char *chars = "0123456789abcdef";
    unsigned i;
    for (i = 0; i < digits; i++) {
        buffer[digits - i - 1] = chars[value & 15];
        value >>= 4;
    }
    syscall6(1, 1, (u64)buffer, digits, 0, 0, 0);
}

typedef void (*FmaFn)(const Vec *, const Vec *, const void *, Vec *,
                      u32, u32 *, int);

/* AT&T syntax: third source b, VEX source a, destination d. */
#define REG_CASE(OP, SUFFIX, REG, A, B) \
    asm volatile("ldmxcsr %[csr]\n\t" \
                 "vmovdqu (%[d]), %%ymm0\n\t" \
                 "vmovdqu (%[a]), %%ymm1\n\t" \
                 "vmovdqu (%[b]), %%ymm2\n\t" \
                 "vfmadd" #OP #SUFFIX " %%" REG B ", %%" REG A ", %%" REG "0\n\t" \
                 "vmovdqu %%ymm0, (%[out])\n\t" \
                 "stmxcsr %[got]\n\tvzeroupper" \
                 : [got] "=m" (*got) \
                 : [csr] "m" (csr), [d] "r" (d), [a] "r" (a), \
                   [b] "r" (b), [out] "r" (out) \
                 : "ymm0", "ymm1", "ymm2", "memory")

#define DEFINE_FMA(OP, SUFFIX, REG, NAME) \
static void NAME(const Vec *d, const Vec *a, const void *b, Vec *out, \
                  u32 csr, u32 *got, int mode) \
{ \
    switch (mode) { \
    case 0: REG_CASE(OP, SUFFIX, REG, "1", "2"); break; \
    case 2: REG_CASE(OP, SUFFIX, REG, "0", "2"); break; \
    case 3: REG_CASE(OP, SUFFIX, REG, "1", "0"); break; \
    case 4: REG_CASE(OP, SUFFIX, REG, "0", "0"); break; \
    case 5: \
        asm volatile("ldmxcsr %[csr]\n\t" \
                     "vmovdqu (%[d]), %%ymm9\n\t" \
                     "vmovdqu (%[a]), %%ymm10\n\t" \
                     "vmovdqu (%[b]), %%ymm11\n\t" \
                     "vfmadd" #OP #SUFFIX " %%" REG "11, %%" REG "10, %%" REG "9\n\t" \
                     "vmovdqu %%ymm9, (%[out])\n\t" \
                     "stmxcsr %[got]\n\tvzeroupper" \
                     : [got] "=m" (*got) \
                     : [csr] "m" (csr), [d] "r" (d), [a] "r" (a), \
                       [b] "r" (b), [out] "r" (out) \
                     : "ymm9", "ymm10", "ymm11", "memory"); \
        break; \
    default: \
        asm volatile("ldmxcsr %[csr]\n\t" \
                     "vmovdqu (%[d]), %%ymm0\n\t" \
                     "vmovdqu (%[a]), %%ymm1\n\t" \
                     "vfmadd" #OP #SUFFIX " (%[b]), %%" REG "1, %%" REG "0\n\t" \
                     "vmovdqu %%ymm0, (%[out])\n\t" \
                     "stmxcsr %[got]\n\tvzeroupper" \
                     : [got] "=m" (*got) \
                     : [csr] "m" (csr), [d] "r" (d), [a] "r" (a), \
                       [b] "r" (b), [out] "r" (out) \
                     : "ymm0", "ymm1", "memory"); \
        break; \
    } \
}

#define DEFINE_FORMAT(OP) \
    DEFINE_FMA(OP, ps, "xmm", f##OP##_ps128) \
    DEFINE_FMA(OP, ps, "ymm", f##OP##_ps256) \
    DEFINE_FMA(OP, pd, "xmm", f##OP##_pd128) \
    DEFINE_FMA(OP, pd, "ymm", f##OP##_pd256)
DEFINE_FORMAT(132)
DEFINE_FORMAT(213)
DEFINE_FORMAT(231)

static FmaFn functions[] = {
    f132_ps128, f132_ps256, f132_pd128, f132_pd256,
    f213_ps128, f213_ps256, f213_pd128, f213_pd256,
    f231_ps128, f231_ps256, f231_pd128, f231_pd256,
};

/* d, a, b raw bit patterns: finite, fused cancellation, NaN payloads,
 * infinities, subnormals, overflow, underflow and signed zero. */
static const u32 data32[][3] = {
    {0x3f800000, 0x40000000, 0x40400000},
    {0xbf800000, 0x3f800001, 0x3f7ffffe},
    {0x3f800000, 0x3f800001, 0x3f800001},
    {0x7fc00111, 0x7fc00222, 0x7fc00333},
    {0x7f800111, 0x7f800222, 0x7f800333},
    {0x7f800000, 0x00000000, 0x7fc00333},
    {0x00000001, 0x3f800000, 0x00000000},
    {0x7f7fffff, 0x40000000, 0x7f7fffff},
    {0x00800000, 0x3f000000, 0x00000000},
    {0x80000000, 0x00000000, 0x80000000},
    {0x00000000, 0x80000000, 0x3f800000},
    {0x00800000, 0x3f7fffff, 0x80800000},
};
static const u64 data64[][3] = {
    {0x3ff0000000000000ULL, 0x4000000000000000ULL, 0x4008000000000000ULL},
    {0xbff0000000000000ULL, 0x3ff0000000000001ULL, 0x3feffffffffffffeULL},
    {0x3ff0000000000000ULL, 0x3ff0000000000001ULL, 0x3ff0000000000001ULL},
    {0x7ff8000000000111ULL, 0x7ff8000000000222ULL, 0x7ff8000000000333ULL},
    {0x7ff0000000000111ULL, 0x7ff0000000000222ULL, 0x7ff0000000000333ULL},
    {0x7ff0000000000000ULL, 0, 0x7ff8000000000333ULL},
    {1, 0x3ff0000000000000ULL, 0},
    {0x7fefffffffffffffULL, 0x4000000000000000ULL, 0x7fefffffffffffffULL},
    {0x0010000000000000ULL, 0x3fe0000000000000ULL, 0},
    {0x8000000000000000ULL, 0, 0x8000000000000000ULL},
    {0, 0x8000000000000000ULL, 0x3ff0000000000000ULL},
    {0x0010000000000000ULL, 0x3fefffffffffffffULL, 0x8010000000000000ULL},
};
static const u32 controls[] = {
    0x1f80, 0x3f80, 0x5f80, 0x7f80, 0x1fc0, 0x9f80, 0x9fc0, 0x1fa1,
};

static void inputs(Vec *d, Vec *a, Vec *b, unsigned format, unsigned dataset)
{
    unsigned i;
    for (i = 0; i < (format & 2 ? 4U : 8U); i++) {
        if (format & 2) {
            d->quads[i] = data64[dataset][0];
            a->quads[i] = data64[dataset][1];
            b->quads[i] = data64[dataset][2];
            if (dataset == 0) {
                d->quads[i] += (u64)i << 48;
            }
        } else {
            d->words[i] = data32[dataset][0];
            a->words[i] = data32[dataset][1];
            b->words[i] = data32[dataset][2];
            if (dataset == 0) {
                d->words[i] += i << 20;
            }
        }
    }
}

static void record(unsigned id, const Vec *out, u32 got)
{
    unsigned i;
    hex(id, 8);
    text(" ");
    for (i = 0; i < 4; i++) {
        hex(out->quads[i], 16);
    }
    text(" ");
    hex(got, 8);
    text("\n");
}

int test_main(void)
{
    Vec d, a, b, out;
    u32 got;
    unsigned fn, mode, dataset, control, id = 0;
    u64 no_core[2] = {0, 0};
    u8 *pages;
    s64 mapped;

    syscall6(160, 4, (u64)no_core, 0, 0, 0, 0); /* setrlimit CORE */
    for (fn = 0; fn < 12; fn++) {
        for (mode = 0; mode < 6; mode++) {
            for (dataset = 0; dataset < 12; dataset++) {
                inputs(&d, &a, &b, fn % 4, dataset);
                for (control = 0; control < 8; control++) {
                    functions[fn](&d, &a, &b, &out, controls[control], &got, mode);
                    record(id++, &out, got);
                }
            }
        }
    }

    mapped = syscall6(9, 0, 8192, 3, 0x22, (u64)-1, 0); /* mmap */
    if (mapped < 0 && mapped >= -4095) {
        text("MMAP FAILED\n");
        return 1;
    }
    pages = (u8 *)mapped;
    for (fn = 0; fn < 12; fn++) {
        unsigned size = (fn & 1) ? 32 : 16;
        u32 status = 0;
        s64 child;

        inputs(&d, &a, &b, fn % 4, 0);
        memcpy(pages + 4096 - 3, &b, 32);
        functions[fn](&d, &a, pages + 4096 - 3, &out, 0x1f80, &got, 1);
        record(id++, &out, got);  /* Both pages readable; operand crosses. */
        memcpy(pages + 4096 - size, &b, size);
        if (syscall6(10, (u64)(pages + 4096), 4096, 0, 0, 0, 0) != 0) {
            text("MPROTECT FAILED\n");
            return 1;
        }
        functions[fn](&d, &a, pages + 4096 - size, &out, 0x1f80, &got, 1);
        record(id++, &out, got);  /* Exactly ends at guard page; no overread. */
        child = syscall6(57, 0, 0, 0, 0, 0, 0); /* fork */
        if (child == 0) {
            functions[fn](&d, &a, pages + 4096 - 3, &out, 0x1f80, &got, 1);
            syscall6(60, 99, 0, 0, 0, 0, 0); /* Must die from SIGSEGV first. */
            __builtin_unreachable();
        }
        if (child < 0 || syscall6(61, child, (u64)&status, 0, 0, 0, 0) < 0 ||
            (status & 127) != 11) {
            text("FAULT CHECK FAILED\n");
            return 1;
        }
        text("FAULT ");
        hex(fn, 2);
        text(" SIGSEGV\n");
        if (syscall6(10, (u64)(pages + 4096), 4096, 3, 0, 0, 0) != 0) {
            return 1;
        }
    }
    syscall6(11, (u64)pages, 8192, 0, 0, 0, 0); /* munmap */
    return 0;
}

__attribute__((naked, noreturn)) void _start(void)
{
    asm volatile("xor %ebp, %ebp\n\tand $-16, %rsp\n\tcall test_main\n\t"
                 "mov %eax, %edi\n\tmov $60, %eax\n\tsyscall");
}
