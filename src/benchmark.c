/*
 * zmij benchmark runner
 *
 * Loads zmij implementations (C, C++, Rust, xjb, Asm) via dlopen and
 * benchmarks their float/double conversion performance.
 * All libraries are optional — if loading fails, they are skipped.
 *
 * Usage:
 *   zmij_benchmark [--lib-dir <path>] [--rounds <N>] <input.txt>
 */

#include "benchmark.h"

#ifndef DEFAULT_LIB_DIR
#  define DEFAULT_LIB_DIR "build/libs"
#endif

/* ---- Helpers ------------------------------------------------------------ */

/* Try to load a library by dir/filename; returns lib with handle==NULL on
 * failure. */
static dtoa_lib_t try_load_lib(const char* dir, const char* filename,
                               const char* display_name, const char* sym_float,
                               const char* sym_double) {
  char path[4096];
  snprintf(path, sizeof(path), "%s/%s", dir, filename);

  dtoa_lib_t lib;
  lib.name = display_name;
  lib.handle = NULL;
  lib.wf = NULL;
  lib.wd = NULL;

  void* h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!h) return lib;

  dlerror();
  lib.wf = (write_float_fn)dlsym(h, sym_float);
  if (dlerror()) {
    dlclose(h);
    return lib;
  }

  lib.wd = (write_double_fn)dlsym(h, sym_double);
  if (dlerror()) {
    dlclose(h);
    return lib;
  }

  lib.handle = h;
  return lib;
}

/* ---- Verification ------------------------------------------------------ */

static int verify_double(const dtoa_lib_t* libs, int nlibs, const double* vals,
                         size_t count) {
  char bufs[MAX_LIBS][64];
  int mismatches = 0;

  for (size_t i = 0; i < count; i++) {
    for (int k = 0; k < nlibs; k++) {
      char* end = libs[k].wd(vals[i], bufs[k]);
      *end = '\0';
    }

    int mismatch = 0;
    for (int k = 1; k < nlibs; k++) {
      if (strcmp(bufs[0], bufs[k]) != 0) {
        mismatch = 1;
        break;
      }
    }

    if (mismatch) {
      if (mismatches == 0) printf("  *** double mismatches detected ***\n");
      char std_buf[64];
      snprintf(std_buf, sizeof(std_buf), "%.*g", 17, vals[i]);
      printf("  [%zu] stdlib=%-24s", i, std_buf);
      for (int k = 0; k < nlibs; k++)
        printf("  %s=%-24s", libs[k].name, bufs[k]);
      printf("\n");
      mismatches++;
    }
  }
  return mismatches;
}

/* ---- Main --------------------------------------------------------------- */

static void usage(const char* prog) {
  fprintf(stderr,
          "Usage: %s [--lib-dir <path>] [--rounds <N>] [--repeats <M>] "
          "<input.txt>\n"
          "\n"
          "  --lib-dir <path>  Directory containing .so files (default: "
          "compile-time DEFAULT_LIB_DIR)\n"
          "  --rounds <N>      Total benchmark rounds per lib (default: 5000)\n"
          "  --repeats <M>     Shuffled passes; each pass runs N/M rounds "
          "per lib (default: 5)\n",
          prog);
}

int main(int argc, char** argv) {
  const char* lib_dir = DEFAULT_LIB_DIR;
  const char* input_path = NULL;
  int rounds = 5000;
  int repeats = 5;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--lib-dir") == 0 && i + 1 < argc) {
      lib_dir = argv[++i];
    } else if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) {
      rounds = atoi(argv[++i]);
      if (rounds <= 0) {
        fprintf(stderr, "Invalid rounds: %s\n", argv[i]);
        return 1;
      }
    } else if (strcmp(argv[i], "--repeats") == 0 && i + 1 < argc) {
      repeats = atoi(argv[++i]);
      if (repeats <= 0) {
        fprintf(stderr, "Invalid repeats: %s\n", argv[i]);
        return 1;
      }
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
      return 0;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      usage(argv[0]);
      return 1;
    } else {
      input_path = argv[i];
    }
  }

  if (!input_path) {
    fprintf(stderr, "Error: no input file specified.\n");
    usage(argv[0]);
    return 1;
  }

  printf("Reading values from: %s\n", input_path);
  values_t vals = read_values(input_path);
  printf("Loaded %zu values.\n\n", vals.count);
  if (vals.count == 0) {
    fprintf(stderr, "Error: input file is empty.\n");
    return 1;
  }

  /* Load libraries — all are optional */
  printf("Loading libraries from: %s\n", lib_dir);
  dtoa_lib_t libs[MAX_LIBS];
  int nlibs = 0;

  struct {
    const char *file, *name, *sym_float, *sym_double;
  } lib_specs[] = {
      {"libzmij_cpp.so", "C++", "zmijcpp_detail_write_float",
       "zmijcpp_detail_write_double"},
      {"libzmij_rust.so", "Rust", "zmijrust_detail_write_float",
       "zmijrust_detail_write_double"},
      {"libxjb.so", "xjb", "xjb32", "xjb64"},
      {"libzmij_asm.so", "Asm", "zmij_detail_write_float",
       "zmij_detail_write_double"},
  };
  int n_specs = (int)(sizeof(lib_specs) / sizeof(lib_specs[0]));

  for (int i = 0; i < n_specs && nlibs < MAX_LIBS; i++) {
    dtoa_lib_t lib =
        try_load_lib(lib_dir, lib_specs[i].file, lib_specs[i].name,
                     lib_specs[i].sym_float, lib_specs[i].sym_double);
    if (lib.handle) {
      libs[nlibs++] = lib;
      printf("  Loaded: %s\n", lib_specs[i].name);
    } else {
      printf("  Not available: %s (skipped)\n", lib_specs[i].name);
    }
  }

  if (nlibs == 0) {
    fprintf(stderr, "Error: no libraries could be loaded.\n");
    return 1;
  }
  printf("\n");

  /* Each repeat runs `block` rounds per lib in rotated order, so every lib
   * is sampled across the same range of thermal/frequency states. */
  int block = rounds / repeats;
  if (block <= 0) block = 1;
  int total_rounds = block * repeats;

  double* d_buf[MAX_LIBS];
  double* f_buf[MAX_LIBS];
  size_t d_sink[MAX_LIBS];
  size_t f_sink[MAX_LIBS];
  for (int i = 0; i < nlibs; i++) {
    d_buf[i] = (double*)malloc((size_t)total_rounds * sizeof(double));
    f_buf[i] = (double*)malloc((size_t)total_rounds * sizeof(double));
    d_sink[i] = 0;
    f_sink[i] = 0;
  }
  int order[MAX_LIBS];

  /* Benchmark: float */
  printf(
      "=== float benchmark (%d rounds × %zu values, %d repeats, %d "
      "warmup) ===\n",
      total_rounds, vals.count, repeats, WARMUP_ROUNDS);
  for (int rep = 0; rep < repeats; rep++) {
    fflush(stdout);
    rotate_order(order, nlibs, rep);
    for (int k = 0; k < nlibs; k++) {
      int i = order[k];
      f_sink[i] ^= warmup_float(libs[i].wf, vals.f, vals.count);
      f_sink[i] = run_float_rounds(libs[i].wf, vals.f, vals.count,
                                   f_buf[i] + rep * block, block, f_sink[i]);
    }
  }
  for (int i = 0; i < nlibs; i++)
    print_stats(libs[i].name, f_buf[i], total_rounds, vals.count, f_sink[i]);
  printf("\n");

  /* Benchmark: double */
  printf(
      "=== double benchmark (%d rounds × %zu values, %d repeats, %d "
      "warmup) ===\n",
      total_rounds, vals.count, repeats, WARMUP_ROUNDS);
  for (int rep = 0; rep < repeats; rep++) {
    fflush(stdout);
    rotate_order(order, nlibs, rep);
    for (int k = 0; k < nlibs; k++) {
      int i = order[k];
      d_sink[i] ^= warmup_double(libs[i].wd, vals.d, vals.count);
      d_sink[i] = run_double_rounds(libs[i].wd, vals.d, vals.count,
                                    d_buf[i] + rep * block, block, d_sink[i]);
    }
  }
  for (int i = 0; i < nlibs; i++)
    print_stats(libs[i].name, d_buf[i], total_rounds, vals.count, d_sink[i]);
  printf("\n");

  /* Verify consistency */
  printf("=== Verifying output consistency ===\n");
  int dmm = verify_double(libs, nlibs, vals.d, vals.count);
  if (dmm == 0)
    printf("  All %zu values: outputs are identical.\n", vals.count);
  else
    printf("  double mismatches: %d\n", dmm);
  printf("\n");

  for (int i = 0; i < nlibs; i++) {
    free(d_buf[i]);
    free(f_buf[i]);
    dlclose(libs[i].handle);
  }
  free(vals.f);
  free(vals.d);
  return 0;
}
