#!/usr/bin/env bash
# Benchmark all built ftoa libraries on x86-64 across the full
# {SSE2, SSE4.1} × {clang, gcc} matrix (4 variants).
# Any extra arguments are forwarded to any_ftoa_benchmark (e.g. --rounds 1000).

source "$(dirname "$0")/tools/compare-common.sh"

for sse in "SSE2:0" "SSE4.1:1"; do
    for cc in "clang:OFF" "gcc:ON"; do
        sse_label="${sse%%:*}"; sse41="${sse#*:}"
        cc_label="${cc%%:*}";   use_gcc="${cc#*:}"

        bench_build -DSSE41="$sse41" -DUSE_GCC="$use_gcc"
        echo "${BLUE}${sse_label} / ${cc_label}:${NORMAL}"
        bench_run "$@"
    done
done
