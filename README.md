# ftoa-benchmark

A benchmark framework for comparing float-to-string implementations. Any library that conforms to the uniform C ABI can be plugged in and benchmarked.

Each implementation is compiled as a shared library (`.so`), loaded at runtime via `dlopen`, and benchmarked through a unified interface.

## Latest Result

06/21, 2026. Benchmark shows better performance on [xjb](https://github.com/xjb714/xjb).

x64, SSE2 (Intel, i13700k):

```
=== float benchmark (5000 rounds × 91932 values, 5 repeats, 100 warmup) ===
  xjb                       min    7.22  P1    7.22  med    7.26  mean    7.25 ns/call  (sink=4042899836)
  zmij_cpp                  min    9.65  P1    9.65  med    9.68  mean    9.68 ns/call  (sink=4043068260)

=== double benchmark (5000 rounds × 91932 values, 5 repeats, 100 warmup) ===
  xjb                       min    8.24  P1    8.27  med    8.34  mean    8.34 ns/call  (sink=7868610120)
  zmij_cpp                  min    9.76  P1    9.76  med    9.80  mean    9.80 ns/call  (sink=7867761512)
```

x64, SSE4.1 (Intel, i13700k):

```
=== float benchmark (5000 rounds × 91932 values, 5 repeats, 100 warmup) ===
  xjb                       min    7.25  P1    7.26  med    7.30  mean    7.30 ns/call  (sink=4042899836)
  zmij_cpp                  min    9.65  P1    9.65  med    9.69  mean    9.70 ns/call  (sink=4043068260)
  
=== double benchmark (5000 rounds × 91932 values, 5 repeats, 100 warmup) ===
  xjb                       min    7.49  P1    7.51  med    7.56  mean    7.56 ns/call  (sink=7868610120)
  zmij_cpp                  min    8.29  P1    8.31  med    8.36  mean    8.37 ns/call  (sink=7867761512)
```



Apple M4:

```
=== float benchmark (5000 rounds × 91932 values, 5 repeats, 100 warmup) ===
  xjb                       min    3.26  P1    3.53  med    3.63  mean    3.64 ns/call  (sink=4042899836)
  zmij_cpp                  min    3.89  P1    4.17  med    4.34  mean    4.34 ns/call  (sink=4043068260)

=== double benchmark (5000 rounds × 91932 values, 5 repeats, 100 warmup) ===
  xjb                       min    4.16  P1    4.48  med    4.67  mean    4.67 ns/call  (sink=7868610120)
  zmij_cpp                  min    4.17  P1    4.49  med    4.65  mean    4.67 ns/call  (sink=7867761512)
```



## Prerequisites

- **Nix** (recommended) — provides all build tools in a reproducible environment
- Or install manually: `clang`, `clang++`, `cmake`, `python3`
- Optional: `cargo` (Rust). When it is missing, the Rust implementation is simply
  skipped — everything else still builds.

## Quick Start

```bash
# Enter development shell (provides clang, cmake, python3)
nix develop

# Build everything (libraries + benchmark executables)
cmake -B build
cmake --build build

# Compile additional libraries to compare (optional)
./tools/compile_cpp.sh ./zmij-cpp-old/zmij.cc zmij-7afa3c
./tools/compile_cpp.sh ./zmij-cpp-old2/zmij.cc zmij-4baf80
./tools/compile_cpp.sh ./zmij-cpp-pr/zmij.cc zmij-pr120

# Run benchmark against all libraries
./build/any_ftoa_benchmark \
    ./build/libs/libxjb.so:xjb64:xjb32 \
    ./build/libs/libzmij_cpp.so:zmijcpp_detail_write_double:zmijcpp_detail_write_float \
    ./build/libs/libzmij_rust.so:zmijrust_detail_write_double:zmijrust_detail_write_float
```

For a full example that builds and benchmarks all included libraries, pick the driver
for your platform:

- [`compare-x64.sh`](compare-x64.sh) — x86-64 Linux, the `{SSE2, SSE4.1} × {clang, gcc}` matrix (4 variants)
- [`compare-aarch64.sh`](compare-aarch64.sh) — aarch64 Linux, `{clang, gcc}` (2 variants)
- [`compare-aarch64-apple.sh`](compare-aarch64-apple.sh) — Apple silicon, single toolchain

Each forwards extra arguments to `any_ftoa_benchmark` (e.g. `./compare-x64.sh --rounds 1000`).

## Running the Benchmark

```bash
./build/any_ftoa_benchmark [--test-input <path>] [--rounds <N>] <lib_spec> [<lib_spec> ...]
```

Each `<lib_spec>` has the format `path[:sym_double[:sym_float]]`. If symbol names are omitted, they default to `zmijcpp_detail_write_double` / `zmijcpp_detail_write_float`.

### Examples

```bash
# Benchmark a library with default symbol names
./build/any_ftoa_benchmark build/libs/libzmij_cpp.so

# Custom symbols and input file
./build/any_ftoa_benchmark --test-input my_data.txt build/libs/libxjb.so:xjb64:xjb32

# Custom rounds
./build/any_ftoa_benchmark --rounds 1000 build/libs/libzmij_rust.so:zmijrust_detail_write_double:zmijrust_detail_write_float
```

### Output Format

```
=== float benchmark (1000 rounds × 91932 values, 100 warmup) ===
  C++             91932000 calls        749.55 ms total      8.15 ± 0.08 ns/call  (sink=897356900)
  Rust            91932000 calls        798.53 ms total      8.69 ± 0.10 ns/call  (sink=897438300)
```

Each line reports: total calls, total time, **mean ± stddev** ns/call, and a sink value (to prevent dead-code elimination). A 100-round warmup runs before measurement begins.

## Preparing Test Input

The benchmark reads one floating-point number per line from a text file. Example:

```
-62.136664999999937
65.851379000000009
3.14159
1.0e-10
```

## Uniform C ABI

Any library can be benchmarked as long as it exports symbols with this signature:

```c
char *<sym_double>(double value, char *buffer);
char *<sym_float>(float value, char *buffer);
```

- `buffer` must be at least 16 bytes (float) or 25 bytes (double)
- Returns a pointer past the last written byte
- Libraries are loaded with `RTLD_LOCAL`, so identical symbol names across libraries don't conflict

## Adding Your Own Library

1. Compile your implementation as a shared library (`.so`)
2. Export functions matching the C ABI above
3. Pass the library path and symbol names as a `<lib_spec>` to `any_ftoa_benchmark`

See `tools/compile_cpp.sh` for an example of how to compile a C++ source file into a conforming shared library.

### Note on Inlining

For best results (no extra `jmp`/`call` overhead in the exported wrapper):

- **C++ (zmij-style)**: comment out explicit template instantiations and add `ZMIJ_INLINE` to the `write` function
- **xjb**: add `static inline` to `xjb64` and `xjb32`
- **Rust**: add `#[inline]` before `write_to_zmij_buffer`

## Building

### Full CMake Build

```bash
cmake -B build
cmake --build build
```

This will:
1. Invoke `build_libs.py` to compile the built-in shared libraries
2. Generate assembly files in `build/libs/` for inspection
3. Build the `any_ftoa_benchmark` and `verifier` executables

### Compile Additional Libraries

```bash
./tools/compile_cpp.sh <source.cc> <output-name>
```

### Libraries Only

```bash
python3 build_libs.py [--output-dir <dir>] [--sse41] [--cc clang] [--no-rust]
```

### Rust (optional)

The Rust toolchain is not required. CMake probes for `cargo` and disables the Rust
library when it is absent; pass `-DWITH_RUST=OFF` to skip it even when `cargo` is
installed. `build_libs.py` does the same probing and accepts `--no-rust`.

### SSE4.1 / AVX2 Mode

```bash
# SSE4.1 via CMake
cmake -B build -DCMAKE_C_COMPILER=clang -DSSE41=ON
cmake --build build
```

## Verifier

Verify that a dtoa library reproduces the exact text representation of each input value (double only):

```bash
./build/verifier [--test-input <path>] <lib_spec> [<lib_spec> ...]
```

Each `<lib_spec>` is `path[:sym_double]`. Exits with code 1 if any mismatches are found.

```bash
./build/verifier build/libs/libzmij_cpp.so
./build/verifier --test-input my_data.txt build/libs/libxjb.so:xjb64
```

## Design Notes

- **Same translation unit**: FFI exports are appended directly to the original source files, not in separate wrapper files. This ensures the compiler naturally inlines the algorithm without requiring LTO.
- Default compiler is clang, which tends to generate faster code than gcc for these workloads.

## License

See [LICENSE](LICENSE). Third-party library licenses are placed in each source directory.
