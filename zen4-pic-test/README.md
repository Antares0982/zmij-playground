# Zen4 -fPIC xjb64 regression test

Root cause (verified by asm analysis): under `-fPIC`, xjb's **double** path is
one GPR short — the `double_table` base must be pinned in a register, which
(a) forces a stack spill on clang and (b) turns ~10 table accesses into
`base+index` loads (incl. 5 scaled-index `(,8)` loads). The **float** path and
zmij's double path are not register-starved, so PIC costs them nothing. This
matches the observed "only xjb64 regresses, ~2x on Zen4, fine on Intel".

## Variants under test

| variant | change | verified effect (asm) |
|---------|--------|-----------------------|
| `base`  | pristine | clang: 2 spill refs, 12 base+index loads; gcc: 0 spill, 10 |
| `varD`  | patch-1: collapse `(orig_buf, sign)` into one live pointer | clang spill 2→0; loads unchanged. Neutral on gcc/Intel, tiny win. **Low risk.** |
| `varE`  | patch-1 + patch-2: also fold scaled-index `pow10`/`exp_result` loads to base-only | base+index: clang 12→9, gcc 10→7. **Costs ~4% on Intel** — Zen4-only bet. |

Both variants are **bit-identical** to base over 30M random doubles + edge cases.

## Run

From the repo root on the Zen4 box:

```bash
bash zen4-pic-test/run-zen4.sh                 # clang+gcc, SSE2+SSE4.1
ROUNDS=20000 REPEATS=9 bash zen4-pic-test/run-zen4.sh
PERF=1 bash zen4-pic-test/run-zen4.sh          # + perf stat base vs varE
```

## Data to send back

1. The full stdout (both `float` and `double` benchmark blocks, all 4 matrix
   combos). The number that matters: `xjb_base` vs `xjb_varD` vs `xjb_varE` vs
   `zmij` **median ns/call in the double block**.
2. The printed `CPU:` line + `clang --version` / `g++ --version`.
3. If you ran `PERF=1`: the two `perf stat` blocks (base vs varE).

The question the data answers: on Zen4, does varD (spill removal) and/or varE
(base-only loads) close the gap between `xjb_base` and its Intel-relative
potential / zmij? varD helping ⇒ spill-dominated; varE helping beyond varD ⇒
scaled-index-addressing-dominated.

## Adopting

`patch-1-buf-collapse.patch` and `patch-2-baseonly-loads.patch` apply to
`xjb/ftoa.cpp` (stack: 1 then 2). Adopt patch-1 only if varD wins with no
regression elsewhere; adopt patch-2 only if varE wins on Zen4 (it is guarded to
x86-64 gcc/clang, but still costs a little on Intel, so gate it on `__znver4__`
if you want zero Intel impact).
