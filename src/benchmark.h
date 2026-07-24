/*
 * benchmark.h — shared types, utilities, and benchmark helpers
 *
 * Included by benchmark.c, any_ftoa_benchmark.c, and verifier.c.
 * All functions are static inline to avoid unused-function warnings
 * in translation units that don't call every helper.
 */

#ifndef ZMIJ_PLAYGROUND_H
#define ZMIJ_PLAYGROUND_H

#define _GNU_SOURCE
#include <dlfcn.h>
#include <libgen.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#  include <sched.h>
#elif defined(__APPLE__)
#  include <mach/mach.h>
#  include <mach/thread_policy.h>
#  include <pthread.h>
#endif

/* ---- Constants ---------------------------------------------------------- */

#define WARMUP_ROUNDS 100
#define MAX_LIBS 64

#define DEFAULT_SYM_DOUBLE "zmijcpp_detail_write_double"
#define DEFAULT_SYM_FLOAT "zmijcpp_detail_write_float"
#define DEFAULT_INPUT_PATH "test_input.txt"

/* ---- Types -------------------------------------------------------------- */

typedef char* (*write_float_fn)(float value, char* buffer);
typedef char* (*write_double_fn)(double value, char* buffer);

typedef struct {
  const char* name;
  void* handle;
  write_float_fn wf;
  write_double_fn wd;
} dtoa_lib_t;

typedef struct {
  float* f;
  double* d;
  size_t count;
} values_t;

/* ---- Utility ------------------------------------------------------------ */

/* printf-style progress logger that silently drops output when `f` is NULL.
 * Passing NULL (as JSON mode does) suppresses all progress/info chatter while
 * leaving genuine error/warning fprintf(stderr, ...) calls untouched. */
static inline void log_info(FILE* f, const char* fmt, ...) {
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
}

static inline double timespec_diff_ns(const struct timespec* end,
                                      const struct timespec* start) {
  return (double)(end->tv_sec - start->tv_sec) * 1e9 +
         (double)(end->tv_nsec - start->tv_nsec);
}

/* ---- Input reader ------------------------------------------------------- */

static inline values_t read_values(const char* path) {
  FILE* fp = fopen(path, "r");
  if (!fp) {
    perror(path);
    exit(1);
  }

  size_t cap = 1024;
  float* fv = (float*)malloc(cap * sizeof(float));
  double* dv = (double*)malloc(cap * sizeof(double));
  size_t n = 0;
  char line[256];

  while (fgets(line, sizeof(line), fp)) {
    if (line[0] == '\n' || line[0] == '\0') continue;
    if (n == cap) {
      cap *= 2;
      fv = (float*)realloc(fv, cap * sizeof(float));
      dv = (double*)realloc(dv, cap * sizeof(double));
    }
    dv[n] = strtod(line, NULL);
    fv[n] = strtof(line, NULL);
    n++;
  }
  fclose(fp);

  values_t v;
  v.f = fv;
  v.d = dv;
  v.count = n;
  return v;
}

/* ---- CPU affinity ------------------------------------------------------- */

/** Pin the current thread to one core to reduce scheduling noise. */
static inline void pin_to_core(int core_id) {
#ifdef __linux__
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
    fprintf(stderr,
            "warning: sched_setaffinity failed (try running as root)\n");
#elif defined(__APPLE__)
  thread_affinity_policy_data_t policy = {core_id + 1};
  kern_return_t ret = thread_policy_set(
      pthread_mach_thread_np(pthread_self()), THREAD_AFFINITY_POLICY,
      (thread_policy_t)&policy, THREAD_AFFINITY_POLICY_COUNT);
  if (ret != KERN_SUCCESS)
    fprintf(stderr, "warning: thread_policy_set failed (%d)\n", ret);
#else
  (void)core_id;
  fprintf(stderr, "warning: pin_to_core not supported on this platform\n");
#endif
}

/* ---- Benchmark helpers -------------------------------------------------- */

static int cmp_double(const void* a, const void* b) {
  double da = *(const double*)a, db = *(const double*)b;
  return (da > db) - (da < db);
}

typedef struct {
  double min, p1, med, mean;
  size_t sink;
} bench_stats_t;

/* Sort `round_ns` in place and reduce it to summary statistics. */
static inline bench_stats_t compute_stats(double* round_ns, int rounds,
                                          size_t count, size_t sink_val) {
  qsort(round_ns, (size_t)rounds, sizeof(double), cmp_double);

  bench_stats_t s;
  s.min = round_ns[0] / (double)count;
  s.p1 = round_ns[rounds / 100] / (double)count; /* P1  */
  s.med = round_ns[rounds / 2] / (double)count;  /* P50 */

  double total_ns = 0;
  for (int r = 0; r < rounds; r++) total_ns += round_ns[r];
  s.mean = total_ns / ((double)rounds * (double)count);
  s.sink = sink_val;
  return s;
}

static inline void print_stats_line(const char* name, bench_stats_t s) {
  printf(
      "  %-24s  min %7.2f  P1 %7.2f  med %7.2f  mean %7.2f ns/call  "
      "(sink=%zu)\n",
      name, s.min, s.p1, s.med, s.mean, s.sink);
}

/* Sort, summarize, and print in one step (human-readable output). */
static inline void print_stats(const char* name, double* round_ns, int rounds,
                               size_t count, size_t sink_val) {
  print_stats_line(name, compute_stats(round_ns, rounds, count, sink_val));
}

/* ---- JSON output -------------------------------------------------------- */
/*
 * Emit an indent-4 JSON document, nested by lib:
 *   {
 *       "<name>": {
 *           "float":  { "min": .., "P1": .., "med": .., "mean": .., "sink": ..
 * }, "double": { ... }
 *       },
 *       ...
 *   }
 * The "float" member is omitted for libs whose has_float[] entry is 0.
 */

static inline void json_emit_stats(const bench_stats_t* s, int indent) {
  printf("{\n");
  printf("%*s\"min\": %.2f,\n", indent + 4, "", s->min);
  printf("%*s\"P1\": %.2f,\n", indent + 4, "", s->p1);
  printf("%*s\"med\": %.2f,\n", indent + 4, "", s->med);
  printf("%*s\"mean\": %.2f,\n", indent + 4, "", s->mean);
  printf("%*s\"sink\": %zu\n", indent + 4, "", s->sink);
  printf("%*s}", indent, "");
}

static inline void json_emit_document(const char** names,
                                      const bench_stats_t* fstats,
                                      const int* has_float,
                                      const bench_stats_t* dstats, int nlibs) {
  printf("{\n");
  for (int i = 0; i < nlibs; i++) {
    printf("    \"%s\": {\n", names[i]);
    if (has_float[i]) {
      printf("        \"float\": ");
      json_emit_stats(&fstats[i], 8);
      printf(",\n");
    }
    printf("        \"double\": ");
    json_emit_stats(&dstats[i], 8);
    printf("\n");
    printf("    }%s\n", i + 1 < nlibs ? "," : "");
  }
  printf("}\n");
}

/* ---- Benchmark ---------------------------------------------------------- */
/*
 * Each lib's benchmark runs in two phases per (repeat, lib) pair:
 *   1) warmup_*  — refill icache/dcache after the previous lib evicted them
 *   2) run_*_rounds — measure `rounds` rounds, append into caller buffer
 *
 * The caller drives the outer repeat loop, shuffling lib order each repeat,
 * so every lib is sampled across the full range of thermal/frequency states
 * instead of the first lib monopolising the cold-CPU window.
 */

static inline size_t warmup_float(write_float_fn fn, const float* vals,
                                  size_t count) {
  char buf[64];
  volatile size_t sink = 0;
  for (int r = 0; r < WARMUP_ROUNDS; r++)
    for (size_t i = 0; i < count; i++) {
      char* end = fn(vals[i], buf);
      sink += (size_t)(end - buf);
    }
  return (size_t)sink;
}

static inline size_t warmup_double(write_double_fn fn, const double* vals,
                                   size_t count) {
  char buf[64];
  volatile size_t sink = 0;
  for (int r = 0; r < WARMUP_ROUNDS; r++)
    for (size_t i = 0; i < count; i++) {
      char* end = fn(vals[i], buf);
      sink += (size_t)(end - buf);
    }
  return (size_t)sink;
}

static inline size_t run_float_rounds(write_float_fn fn, const float* vals,
                                      size_t count, double* round_ns_out,
                                      int rounds, size_t sink_in) {
  char buf[64];
  volatile size_t sink = sink_in;
  struct timespec t0, t1;
  for (int r = 0; r < rounds; r++) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &t0);
    for (size_t i = 0; i < count; i++) {
      char* end = fn(vals[i], buf);
      sink += (size_t)(end - buf);
    }
    clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
    round_ns_out[r] = timespec_diff_ns(&t1, &t0);
  }
  return (size_t)sink;
}

static inline size_t run_double_rounds(write_double_fn fn, const double* vals,
                                       size_t count, double* round_ns_out,
                                       int rounds, size_t sink_in) {
  char buf[64];
  volatile size_t sink = sink_in;
  struct timespec t0, t1;
  for (int r = 0; r < rounds; r++) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &t0);
    for (size_t i = 0; i < count; i++) {
      char* end = fn(vals[i], buf);
      sink += (size_t)(end - buf);
    }
    clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
    round_ns_out[r] = timespec_diff_ns(&t1, &t0);
  }
  return (size_t)sink;
}

/* Fill `order` with a cyclic rotation: order[i] = (rep + i) % n.
 *
 * Cycling instead of random shuffling guarantees that across M repeats each
 * lib appears in each slot position the same number of times (exactly when
 * M is a multiple of n; otherwise off by one). That removes the P1 variance
 * that plain Fisher-Yates exhibits with few libs and few repeats — a single
 * unlucky draw could put one lib in slot 0 four out of five times. */
static inline void rotate_order(int* order, int n, int rep) {
  for (int i = 0; i < n; i++) order[i] = (rep + i) % n;
}

/* ---- Lib spec parsing --------------------------------------------------- */
/*
 * Parse a spec string of the form:
 *   path
 *   path:sym_double
 *   path:sym_double:sym_float
 *
 * Defaults: sym_double = DEFAULT_SYM_DOUBLE, sym_float = DEFAULT_SYM_FLOAT.
 * Returns a dtoa_lib_t with handle==NULL on failure.
 * Float symbol lookup failure is non-fatal (wf set to NULL).
 */
static inline dtoa_lib_t parse_and_load(const char* spec) {
  dtoa_lib_t lib = {NULL, NULL, NULL, NULL};

  char* buf = strdup(spec);
  if (!buf) {
    perror("strdup");
    exit(1);
  }

  char* path = buf;
  char* sym_double = NULL;
  char* sym_float = NULL;

  char* colon1 = strchr(path, ':');
  if (colon1) {
    *colon1 = '\0';
    sym_double = colon1 + 1;
    char* colon2 = strchr(sym_double, ':');
    if (colon2) {
      *colon2 = '\0';
      sym_float = colon2 + 1;
    }
  }

  if (!sym_double || *sym_double == '\0') sym_double = DEFAULT_SYM_DOUBLE;
  if (!sym_float || *sym_float == '\0') sym_float = DEFAULT_SYM_FLOAT;

  /* Derive display name from basename */
  char* path_copy = strdup(path);
  const char* bname = basename(path_copy);
  const char* display = bname;
  if (strncmp(display, "lib", 3) == 0) display += 3;
  char* name = strdup(display);
  char* dot = strstr(name, ".so");
  if (dot) *dot = '\0';
  free(path_copy);

  void* h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    fprintf(stderr, "dlopen(%s): %s\n", path, dlerror());
    free(buf);
    free(name);
    return lib;
  }

  dlerror();
  write_double_fn wd = (write_double_fn)dlsym(h, sym_double);
  const char* err = dlerror();
  if (err) {
    fprintf(stderr, "dlsym(%s, %s): %s\n", path, sym_double, err);
    dlclose(h);
    free(buf);
    free(name);
    return lib;
  }

  dlerror();
  write_float_fn wf = (write_float_fn)dlsym(h, sym_float);
  err = dlerror();
  if (err) {
    /* float is allowed to fail — just set NULL */
    wf = NULL;
  }

  lib.name = name;
  lib.handle = h;
  lib.wd = wd;
  lib.wf = wf;

  free(buf);
  return lib;
}

#endif /* ZMIJ_PLAYGROUND_H */
