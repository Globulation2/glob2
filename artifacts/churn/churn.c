#include <execinfo.h>
#include <dlfcn.h>
#include <malloc/malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { CAP = 32768, DEPTH = 5 };
typedef struct { uintptr_t frames[DEPTH]; unsigned long long calls, bytes; } Entry;
static Entry entries[CAP];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int busy;
static unsigned long long allocs, alloc_bytes, frees, free_bytes, reallocs, realloc_bytes;

static void record(void *p, size_t n) {
    if (!p || busy) return;
    busy = 1;
    void *stack[DEPTH + 4];
    int depth = backtrace(stack, DEPTH + 4);
    uintptr_t f[DEPTH] = {0};
    for (int i = 0; i < DEPTH && i + 2 < depth; ++i) f[i] = (uintptr_t)stack[i + 2];
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < DEPTH; ++i) { h ^= f[i]; h *= 1099511628211ULL; }
    pthread_mutex_lock(&mu);
    ++allocs; alloc_bytes += n;
    for (unsigned k = 0, slot = h % CAP; k < CAP; ++k, slot = (slot + 1) % CAP) {
        Entry *e = &entries[slot];
        if (!e->calls) memcpy(e->frames, f, sizeof f);
        if (memcmp(e->frames, f, sizeof f) == 0) { ++e->calls; e->bytes += n; break; }
    }
    pthread_mutex_unlock(&mu);
    busy = 0;
}
static void *trace_malloc(size_t n) { void *p = malloc(n); record(p, n); return p; }
static void *trace_calloc(size_t a, size_t b) { void *p = calloc(a, b); record(p, a * b); return p; }
static void trace_free(void *p) {
    if (p && !busy) { busy = 1; size_t n = malloc_size(p); pthread_mutex_lock(&mu); ++frees; free_bytes += n; pthread_mutex_unlock(&mu); busy = 0; }
    free(p);
}
static void *trace_realloc(void *p, size_t n) {
    size_t old = p && !busy ? malloc_size(p) : 0;
    void *q = realloc(p, n);
    if (!busy) { busy = 1; pthread_mutex_lock(&mu); ++reallocs; realloc_bytes += old; pthread_mutex_unlock(&mu); busy = 0; }
    record(q, n);
    return q;
}
__attribute__((used, section("__DATA,__interpose")))
static const struct { const void *replacement, *replacee; } hooks[] = {
    {(const void *)trace_malloc, (const void *)malloc},
    {(const void *)trace_calloc, (const void *)calloc},
    {(const void *)trace_free, (const void *)free},
    {(const void *)trace_realloc, (const void *)realloc}
};
static int compare(const void *a, const void *b) {
    const Entry *x = *(const Entry * const *)a, *y = *(const Entry * const *)b;
    return x->bytes < y->bytes ? 1 : x->bytes > y->bytes ? -1 : 0;
}
__attribute__((destructor)) static void dump(void) {
    busy = 1;
    const char *path = getenv("GLOB2_CHURN_OUT");
    FILE *out = path ? fopen(path, "w") : stderr;
    if (!out) return;
    fprintf(out, "TOTAL allocs=%llu requested_bytes=%llu frees=%llu freed_usable_bytes=%llu reallocs=%llu realloc_old_usable_bytes=%llu\n", allocs, alloc_bytes, frees, free_bytes, reallocs, realloc_bytes);
    Entry *sorted[CAP]; unsigned count = 0;
    for (unsigned i = 0; i < CAP; ++i) if (entries[i].calls) sorted[count++] = &entries[i];
    qsort(sorted, count, sizeof sorted[0], compare);
    for (unsigned i = 0; i < count; ++i) {
        Entry *e = sorted[i];
        fprintf(out, "SITE calls=%llu bytes=%llu", e->calls, e->bytes);
        for (int j = 0; j < DEPTH && e->frames[j]; ++j) {
            Dl_info info;
            if (dladdr((void *)e->frames[j], &info)) fprintf(out, " |%s:%p:%p", info.dli_fname, info.dli_fbase, (void *)e->frames[j]);
        }
        fputc('\n', out);
    }
    if (out != stderr) fclose(out);
}
