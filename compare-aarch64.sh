#!/usr/bin/env bash
# Benchmark all built ftoa libraries on aarch64 Linux, once per toolchain.
# NEON is part of the baseline ABI, so unlike x86-64 there is only one SIMD
# configuration; we still sweep {clang, gcc} (2 variants).
# Any extra arguments are forwarded to any_ftoa_benchmark (e.g. --rounds 1000).

source "$(dirname "$0")/tools/compare-common.sh"

for cc in "clang:OFF" "gcc:ON"; do
    cc_label="${cc%%:*}"; use_gcc="${cc#*:}"

    bench_build -DUSE_GCC="$use_gcc"
    echo "${BLUE}NEON / ${cc_label} (aarch64 baseline):${NORMAL}"
    bench_run "$@"
done
