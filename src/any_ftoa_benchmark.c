/*
 * any_ftoa_benchmark — generic dtoa benchmark runner
 *
 * Loads arbitrary shared libraries via dlopen and benchmarks their
 * float/double conversion performance.
 *
 * Usage:
 *   any_ftoa_benchmark [--test-input <path>] [--rounds <N>] <lib_spec> ...
 *
 * Each <lib_spec> has the format:
 *   path
 *   path:entrance_for_double
 *   path:entrance_for_double:entrance_for_float
 *
 * Defaults: --test-input test_input.txt
 *           sym_double = zmijcpp_detail_write_double
 *           sym_float  = zmijcpp_detail_write_float
 */

#include "benchmark.h"

/* ---- Main --------------------------------------------------------------- */

static void usage(const char* prog) {
  fprintf(stderr,
          "Usage: %s [--test-input <path>] [--rounds <N>] [--repeats <M>] "
          "<lib_spec> [<lib_spec> ...]\n"
          "\n"
          "Each <lib_spec> is: path[:sym_double[:sym_float]]\n"
          "  Defaults: sym_double=%s  sym_float=%s\n"
          "  --test-input <path>  Input file (default: %s)\n"
          "  --rounds <N>         Total benchmark rounds per lib (default: "
          "5000)\n"
          "  --repeats <M>        Shuffled passes; each pass runs N/M rounds "
          "per lib (default: 5)\n",
          prog, DEFAULT_SYM_DOUBLE, DEFAULT_SYM_FLOAT, DEFAULT_INPUT_PATH);
}

int main(int argc, char** argv) {
  int rounds = 5000;
  int repeats = 5;
  const char* input_path = DEFAULT_INPUT_PATH;
  const char* lib_specs[MAX_LIBS];
  int n_specs = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) {
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
    } else if (strcmp(argv[i], "--test-input") == 0 && i + 1 < argc) {
      input_path = argv[++i];
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      if (n_specs >= MAX_LIBS) {
        fprintf(stderr, "Too many libraries (max %d)\n", MAX_LIBS);
        return 1;
      }
      lib_specs[n_specs++] = argv[i];
    }
  }

  if (n_specs == 0) {
    fprintf(stderr, "Error: need at least one lib spec.\n");
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

  /* Load libraries */
  dtoa_lib_t libs[MAX_LIBS];
  int nlibs = 0;

  for (int i = 0; i < n_specs; i++) {
    dtoa_lib_t lib = parse_and_load(lib_specs[i]);
    if (lib.handle) {
      libs[nlibs++] = lib;
      printf("  Loaded: %s\n", lib.name);
    } else {
      fprintf(stderr, "  Failed to load: %s\n", lib_specs[i]);
    }
  }

  if (nlibs == 0) {
    fprintf(stderr, "Error: no libraries could be loaded.\n");
    return 1;
  }
  printf("\n");

  /* Pin to a single core to reduce scheduling noise */
  pin_to_core(1);

  /* Check if any lib has float support */
  int has_float = 0;
  for (int i = 0; i < nlibs; i++) {
    if (libs[i].wf) {
      has_float = 1;
      break;
    }
  }

  /* Each repeat runs `block` rounds per lib in shuffled order, so every lib
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
    f_buf[i] = libs[i].wf
                   ? (double*)malloc((size_t)total_rounds * sizeof(double))
                   : NULL;
    d_sink[i] = 0;
    f_sink[i] = 0;
  }
  int order[MAX_LIBS];

  /* Benchmark: float */
  if (has_float) {
    printf(
        "=== float benchmark (%d rounds × %zu values, %d repeats, %d "
        "warmup) ===\n",
        total_rounds, vals.count, repeats, WARMUP_ROUNDS);
    for (int rep = 0; rep < repeats; rep++) {
      fflush(stdout);
      rotate_order(order, nlibs, rep);
      for (int k = 0; k < nlibs; k++) {
        int i = order[k];
        if (!libs[i].wf) continue;
        f_sink[i] ^= warmup_float(libs[i].wf, vals.f, vals.count);
        f_sink[i] = run_float_rounds(libs[i].wf, vals.f, vals.count,
                                     f_buf[i] + rep * block, block, f_sink[i]);
      }
    }
    for (int i = 0; i < nlibs; i++) {
      if (libs[i].wf)
        print_stats(libs[i].name, f_buf[i], total_rounds, vals.count,
                    f_sink[i]);
    }
    printf("\n");
  }

  /* Benchmark: double */
  printf(
      "=== double benchmark (%d rounds × %zu values, %d repeats, %d warmup) "
      "===\n",
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

  /* Cleanup */
  for (int i = 0; i < nlibs; i++) {
    free(d_buf[i]);
    free(f_buf[i]);
    dlclose(libs[i].handle);
    free((void*)libs[i].name);
  }
  free(vals.f);
  free(vals.d);
  return 0;
}
