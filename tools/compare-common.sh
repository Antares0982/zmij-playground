#!/usr/bin/env bash
# Shared helpers for the compare-*.sh benchmark drivers.
# Source this from a script living in the repo root; it chdirs to the repo root.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
LIB_DIR="$BUILD_DIR/libs"

if [[ -t 1 ]]; then
    BLUE="$(tput setaf 4)"
    NORMAL="$(tput sgr0)"
else
    BLUE=""
    NORMAL=""
fi

_BUILD_LOG="$(mktemp)"
trap 'rm -f "$_BUILD_LOG"' EXIT

# bench_build [<cmake_args>...] — configure + build quietly, dumping the log
# only if something actually fails.
bench_build() {
    if ! { cmake -B "$BUILD_DIR" . "$@" && cmake --build "$BUILD_DIR"; } \
        >"$_BUILD_LOG" 2>&1; then
        echo "Build failed (cmake args: $*)" >&2
        cat "$_BUILD_LOG" >&2
        exit 1
    fi
}

# bench_run [<extra_benchmark_args>...] — run any_ftoa_benchmark over every
# library that got built. zmij-rust is included only when it is present, since
# the Rust toolchain is optional.
bench_run() {
    local specs=(
        "$LIB_DIR/libxjb.so:xjb64:xjb32"
        "$LIB_DIR/libzmij_cpp.so:zmijcpp_detail_write_double:zmijcpp_detail_write_float"
    )
    if [[ -f "$LIB_DIR/libzmij_rust.so" ]]; then
        specs+=(
            "$LIB_DIR/libzmij_rust.so:zmijrust_detail_write_double:zmijrust_detail_write_float"
        )
    fi
    "$BUILD_DIR/any_ftoa_benchmark" "${specs[@]}" "$@"
}
