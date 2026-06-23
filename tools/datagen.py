"""Run self-play data generation across many cores and concatenate the output.

Each worker is an `ixchess-engine datagen` process with its own seed; the text
shards are merged into one file (bullet's converter ingests `FEN | score | wdl`).

  python tools/datagen.py --out data/gen1.txt --games 200000 --nodes 5000 --workers 18
"""

import argparse
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ENGINE = ROOT / "bin" / "ixchess-engine.exe"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=str(DEFAULT_ENGINE))
    ap.add_argument("--out", required=True)
    ap.add_argument("--games", type=int, default=100000, help="total games across workers")
    ap.add_argument("--nodes", type=int, default=5000, help="search nodes per move")
    ap.add_argument("--workers", type=int, default=16)
    args = ap.parse_args()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    per = max(1, args.games // args.workers)

    print(f"datagen: {args.workers} workers x {per} games @ {args.nodes} nodes/move")
    t0 = time.time()
    procs = []
    shards = []
    for w in range(args.workers):
        shard = out.with_suffix(f".part{w}")
        shards.append(shard)
        procs.append(subprocess.Popen(
            [args.engine, "datagen", str(shard), str(per), str(args.nodes), str(w + 1)]))
    for p in procs:
        p.wait()

    # Merge shards.
    total = 0
    with open(out, "w") as dst:
        for shard in shards:
            if shard.exists():
                with open(shard) as src:
                    for line in src:
                        dst.write(line)
                        total += 1
                shard.unlink()
    print(f"\nmerged {total} positions -> {out}  ({time.time()-t0:.0f}s)")


if __name__ == "__main__":
    main()
