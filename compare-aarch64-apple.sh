#!/usr/bin/env bash
# Benchmark all built ftoa libraries on aarch64.
# NEON is part of the baseline ABI, so unlike x86-64 there is only one
# configuration to measure.
# Any extra arguments are forwarded to any_ftoa_benchmark (e.g. --rounds 1000).

source "$(dirname "$0")/tools/compare-common.sh"

bench_build
echo "${BLUE}NEON (aarch64 baseline):${NORMAL}"
bench_run "$@"
