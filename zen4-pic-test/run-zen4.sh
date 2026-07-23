#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Zen4 -fPIC regression A/B/C test for xjb64 (double path).
#
# Builds three variants of xjb from the SAME pristine xjb/ftoa.cpp:
#   base : unmodified
#   varD : patch-1 (buf+sign collapse — removes clang PIC spill)
#   varE : patch-1 + patch-2 (also fold scaled-index pow10/exp_result
#          table loads into base-only loads)
# plus zmij-cpp as the reference, and runs them side by side in a single
# shuffled benchmark pass (apples-to-apples) for the full
# {SSE2, SSE4.1} x {clang, gcc} matrix.
#
# Run from the repo root on the Zen4 box:
#     bash zen4-pic-test/run-zen4.sh                # matrix, default rounds
#     ROUNDS=20000 REPEATS=9 bash zen4-pic-test/run-zen4.sh
#     CCS="clang gcc" SSES="sse41" bash zen4-pic-test/run-zen4.sh
#     PERF=1 bash zen4-pic-test/run-zen4.sh         # + perf stat base vs varE
#
# Requires: clang++/g++, the repo's src/ + test_input.txt, python3.
# ---------------------------------------------------------------------------
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"
WORK="$REPO/zen4-pic-test/build"
mkdir -p "$WORK"

ROUNDS="${ROUNDS:-20000}"
REPEATS="${REPEATS:-9}"
CCS="${CCS:-clang gcc}"          # add "icpx" if you have it
SSES="${SSES:-sse2 sse41}"
PERF="${PERF:-0}"

BASE_FLAGS="-std=c++17 -O3 -DNDEBUG -fPIC -fvisibility=hidden -fno-stack-protector -fomit-frame-pointer"

# ---- 1. generate the three source variants from pristine xjb/ftoa.cpp ------
python3 - "$REPO" "$WORK" <<'PY'
import sys, pathlib
repo, work = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
src = (repo / "xjb" / "ftoa.cpp").read_text()

# base
(work / "ftoa_base.cpp").write_text(src)

# patch 1: collapse (orig_buf, sign) into one live pointer
p1_old = "    memcpy(&vi, &v, sizeof(v));\n    *buf = '-';\n    buf += vi >> 63;\n"
p1_new = p1_old + ('#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))\n'
                   '    asm("" : "+r"(buf));\n#endif\n')
assert src.count(p1_old) == 1, "patch-1 anchor not found (source changed?)"
d = src.replace(p1_old, p1_new, 1)
(work / "ftoa_varD.cpp").write_text(d)

# patch 2 (stacks on patch 1): fold scaled-index table loads to base-only
gp_old = ("    const u64* pow10_ptr = t->pow10_double + 323 * 2 + 2;\n"
          "    *pow10_hi = pow10_ptr[k * 2 + 0];\n"
          "    *pow10_lo = pow10_ptr[k * 2 + 1];")
gp_new = ("    const u64* pow10_ptr = t->pow10_double + 323 * 2 + 2 + k * 2;\n"
          '#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))\n'
          '    asm("" : "+r"(pow10_ptr));\n#endif\n'
          "    *pow10_hi = pow10_ptr[0];\n"
          "    *pow10_lo = pow10_ptr[1];")
er_old = "    u64 exp_result = t->exp_result_double[e10 + 324];"
er_new = ("    const u64* exp_result_ptr = &t->exp_result_double[e10 + 324];\n"
          '#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))\n'
          '    asm("" : "+r"(exp_result_ptr));\n#endif\n'
          "    u64 exp_result = *exp_result_ptr;")
assert d.count(gp_old) == 1 and d.count(er_old) == 1, "patch-2 anchor not found"
e = d.replace(gp_old, gp_new, 1).replace(er_old, er_new, 1)
(work / "ftoa_varE.cpp").write_text(e)
print("generated: ftoa_base.cpp ftoa_varD.cpp ftoa_varE.cpp")
PY

# ---- 2. build the benchmark harness once ----------------------------------
BENCH="$WORK/any_ftoa_benchmark"
${CC:-cc} -O2 -Isrc src/any_ftoa_benchmark.c -o "$BENCH" -ldl -lm

echo "CPU: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//')"
echo "rounds=$ROUNDS repeats=$REPEATS"
echo

# ---- 3. matrix loop -------------------------------------------------------
for cc in $CCS; do
  case "$cc" in
    clang) CXX=clang++ ;;
    gcc)   CXX=g++ ;;
    icpx)  CXX=icpx ;;
    *)     CXX="$cc" ;;
  esac
  command -v "$CXX" >/dev/null 2>&1 || { echo "skip: $CXX not found"; continue; }

  for sse in $SSES; do
    if [[ "$sse" == "sse41" ]]; then EXTRA="-msse4.1 -DZMIJ_USE_SSE4_1=1"; else EXTRA=""; fi
    tag="${cc}_${sse}"
    echo "############################################################"
    echo "# ${CXX}   ${sse}    ($($CXX --version | head -1))"
    echo "############################################################"

    # xjb variants
    for v in base varD varE; do
      "$CXX" $BASE_FLAGS $EXTRA -Ixjb -shared \
             -o "$WORK/libxjb_${v}_${tag}.so" "$WORK/ftoa_${v}.cpp"
    done
    # zmij reference
    "$CXX" $BASE_FLAGS $EXTRA -Izmij-cpp -shared \
           -o "$WORK/libzmij_${tag}.so" zmij-cpp/zmij.cc

    "$BENCH" --test-input test_input.txt --rounds "$ROUNDS" --repeats "$REPEATS" \
      "$WORK/libxjb_base_${tag}.so:xjb64:xjb32" \
      "$WORK/libxjb_varD_${tag}.so:xjb64:xjb32" \
      "$WORK/libxjb_varE_${tag}.so:xjb64:xjb32" \
      "$WORK/libzmij_${tag}.so:zmijcpp_detail_write_double:zmijcpp_detail_write_float"
    echo
  done
done

# ---- 4. optional perf attribution (single-lib runs, clang+SSE4.1) ---------
if [[ "$PERF" == "1" ]] && command -v perf >/dev/null 2>&1; then
  echo "############################################################"
  echo "# perf stat: base vs varE (clang, SSE4.1, double-heavy)"
  echo "############################################################"
  EV="cycles,instructions,L1-dcache-loads,L1-dcache-load-misses,stalled-cycles-backend,stalled-cycles-frontend"
  for v in base varE; do
    echo "----- $v -----"
    perf stat -e "$EV" -- \
      "$BENCH" --test-input test_input.txt --rounds "$ROUNDS" --repeats "$REPEATS" \
      "$WORK/libxjb_${v}_clang_sse41.so:xjb64:xjb32" >/dev/null || true
    echo
  done
fi
