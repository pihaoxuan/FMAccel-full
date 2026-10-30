#include "qemu/osdep.h"
#include "cpu.h"
#include "fma_detector.h"

GHashTable *gemm_meta_cache;
static gsize detector_initialized;

void init_gemm_detector(void)
{
    if (g_once_init_enter(&detector_initialized)) {
        const char *path = getenv("FMA_PROFILE");
        GHashTable *table;
        FILE *fp;
        char line[256];

        if (path == NULL || path[0] == '\0') {
            path = "/path/to/detectorAndQEMU/offline-analysis.txt";
        }
        table = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                      NULL, g_free);
        fp = fopen(path, "r");
        if (fp == NULL) {
            fprintf(stderr, "[FMA Profiler] Cannot open '%s': %s; "
                    "using original QEMU path.\n", path, strerror(errno));
        } else {
            while (fgets(line, sizeof(line), fp) != NULL) {
                uint64_t offset;
                unsigned length;

                if (sscanf(line, "META: %" SCNx64 " %u", &offset,
                           &length) == 2 && length >= 5 && length <= 15) {
                    GEMMBlockMeta *meta = g_new0(GEMMBlockMeta, 1);

                    meta->start_pc = offset;
                    meta->length = length;
                    meta->is_gemm_loop = true;
                    g_hash_table_insert(table, (gpointer)(uintptr_t)offset,
                                        meta);
                }
            }
            fclose(fp);
        }
        /* Publish the fully constructed, subsequently read-only table. */
        gemm_meta_cache = table;
        g_once_init_leave(&detector_initialized, 1);
    }
}

GEMMBlockMeta *getMeta(target_ulong pc)
{
    return gemm_meta_cache ?
        g_hash_table_lookup(gemm_meta_cache, (gpointer)(uintptr_t)pc) : NULL;
}
