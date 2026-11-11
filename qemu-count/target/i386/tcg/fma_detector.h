#ifndef FMA_DETECTOR_H
#define FMA_DETECTOR_H

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/translator.h" // ！
#include <glib.h>

extern uint64_t g_total_insn_exec;
extern uint64_t g_fma_exec_count;

void print_fma_ratio(void);

typedef struct {
    target_ulong start_pc;   
    uint32_t length;         
    bool is_gemm_loop;       
    uint8_t reg_mapping_hint;
} GEMMBlockMeta;

extern GHashTable *gemm_meta_cache;

void init_gemm_detector(void);

GEMMBlockMeta* getMeta(target_ulong pc);

#endif
