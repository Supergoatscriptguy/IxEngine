"""Run self-play data generation across many cores with a live status bar.

Each worker is an `ixchess-engine datagen` process (own seed); their text shards
(`FEN | score | wdl`) are merged at the end. Worker output is hidden — you just
get one updating status line you can tab in and glance at.

  python tools/datagen.py --out data/gen1.txt --games 200000 --nodes 5000 --workers 18
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE_BIN = "ixchess-engine.exe" if os.name == "nt" else "ixchess-engine"
DEFAULT_ENGINE = ROOT / "bin" / ENGINE_BIN


def fmt_time(s):
    s = max(0, int(s))
    return f"{s // 3600}:{(s % 3600) // 60:02d}:{s % 60:02d}" if s >= 3600 else f"{s // 60:02d}:{s % 60:02d}"


def human(n):
    if n >= 1_000_000:
        return f"{n / 1e6:.2f}M"
    if n >= 1_000:
        return f"{n / 1e3:.0f}k"
    return str(int(n))


def avg_line_bytes(shards):
    for s in shards:
        if s.exists() and s.stat().st_size > 4096:
            with open(s, "rb") as f:
                data = f.read(65536)
            lines = data.count(b"\n")
            if lines > 20:
                return len(data) / lines
    return 70.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=str(DEFAULT_ENGINE))
    ap.add_argument("--out", required=True)
    ap.add_argument("--games", type=int, default=100000, help="total games across workers")
    ap.add_argument("--nodes", type=int, default=5000, help="search nodes per move")
    ap.add_argument("--workers", type=int, default=16)
    ap.add_argument("--net", default="", help="NNUE net for bootstrap datagen (better labels)")
    args = ap.parse_args()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    per = max(1, args.games // args.workers)
    target = args.games * 70          # rough positions estimate, for % and ETA

    shards = [out.with_suffix(f".part{w}") for w in range(args.workers)]
    procs = []
    for w, shard in enumerate(shards):
        if shard.exists():
            shard.unlink()
        cmd = [args.engine, "datagen", str(shard), str(per), str(args.nodes), str(w + 1)]
        if args.net:
            cmd.append(args.net)
        procs.append(subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))

    print(f"datagen: {args.workers} workers x {per} games @ {args.nodes} nodes/move "
          f"(~{human(target)} positions target)")

    start = time.time()
    last_t, last_pos, rate, alb = start, 0, 0.0, 70.0
    BAR = 26
    while True:
        alive = sum(p.poll() is None for p in procs)
        nbytes = sum(s.stat().st_size for s in shards if s.exists())
        if nbytes > 1_000_000:
            alb = avg_line_bytes(shards)
        pos = int(nbytes / alb)

        now = time.time()
        dt = now - last_t
        if dt >= 0.5:
            inst = (pos - last_pos) / dt
            rate = inst if rate == 0 else 0.6 * rate + 0.4 * inst
            last_t, last_pos = now, pos

        frac = min(pos / target, 0.999) if target else 0
        filled = int(frac * BAR)
        bar = "#" * filled + "-" * (BAR - filled)
        eta = (target - pos) / rate if rate > 1 and pos < target else 0
        sys.stdout.write(
            f"\r[{bar}] ~{frac*100:4.1f}%  {human(pos)} pos  {human(rate)}/s  "
            f"elapsed {fmt_time(now-start)}  ETA {fmt_time(eta)}  {alive}/{args.workers} live   ")
        sys.stdout.flush()

        if alive == 0:
            break
        time.sleep(1.0)

    # Merge shards.
    print("\nmerging shards...")
    total = 0
    with open(out, "w") as dst:
        for shard in shards:
            if shard.exists():
                with open(shard) as src:
                    for line in src:
                        dst.write(line)
                        total += 1
                shard.unlink()
    print(f"done: {total} positions -> {out}  ({fmt_time(time.time()-start)})")


if __name__ == "__main__":
    main()
