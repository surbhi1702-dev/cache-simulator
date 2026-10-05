#!/usr/bin/env python3
"""Emit memory traces of C = A x B (row-major doubles) for the simulator.

    python3 scripts/gen_matmul_trace.py --n 64 --variant naive > traces/matmul_naive_64.trace
    python3 scripts/gen_matmul_trace.py --n 64 --variant tiled --tile 16 > traces/matmul_tiled_64.trace

The naive i-j-k loop walks B column-wise (stride n*8 bytes), so for large n
every B access misses. Loop tiling keeps a tile of B resident and reuses it.
"""
import argparse
import sys

A, B, C = 0x10000000, 0x20000000, 0x30000000
ELEM = 8


def naive(n, out):
    for i in range(n):
        for j in range(n):
            out(f"R 0x{C + (i * n + j) * ELEM:x}")
            for k in range(n):
                out(f"R 0x{A + (i * n + k) * ELEM:x}")
                out(f"R 0x{B + (k * n + j) * ELEM:x}")
            out(f"W 0x{C + (i * n + j) * ELEM:x}")


def tiled(n, t, out):
    for ii in range(0, n, t):
        for jj in range(0, n, t):
            for kk in range(0, n, t):
                for i in range(ii, min(ii + t, n)):
                    for j in range(jj, min(jj + t, n)):
                        out(f"R 0x{C + (i * n + j) * ELEM:x}")
                        for k in range(kk, min(kk + t, n)):
                            out(f"R 0x{A + (i * n + k) * ELEM:x}")
                            out(f"R 0x{B + (k * n + j) * ELEM:x}")
                        out(f"W 0x{C + (i * n + j) * ELEM:x}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--n", type=int, default=64)
    ap.add_argument("--variant", choices=["naive", "tiled"], default="naive")
    ap.add_argument("--tile", type=int, default=16)
    args = ap.parse_args()
    lines = [f"# matmul {args.variant} n={args.n}" + (f" tile={args.tile}" if args.variant == "tiled" else "")
             + f", doubles, A@0x{A:x} B@0x{B:x} C@0x{C:x}"]
    out = lines.append
    naive(args.n, out) if args.variant == "naive" else tiled(args.n, args.tile, out)
    sys.stdout.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
