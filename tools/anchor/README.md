# CCRL-anchor rig

A permanent measuring rig that places IxEngine on the **CCRL 40/40 rating scale**
by playing it against open-source engines whose published CCRL 40/40 ratings are
held as **fixed anchors**, then solving for the single unknown (IxEngine).

## Run it (one command)

```bat
run_anchor.bat
```
or
```bash
python tools/anchor/run_anchor.py            # uses anchors.json
python tools/anchor/run_anchor.py --tag gen3 # label the run in history.csv
python tools/anchor/run_anchor.py --games 200 --concurrency 8
```

Each run writes `runs/<timestamp>_<tag>.txt` (the report), `runs/<timestamp>_<tag>.pgn`
(every game, for re-scoring with Ordo/Bayeselo if you ever want a second opinion),
and appends one line to `history.csv` so you can watch the number move across
upgrades.

## What it measures (and what it does NOT)

This is a **blitz-anchored approximation** at TC **15+0.15**, not a true CCRL 40/40
result. Engine strength shifts with time control, so treat the number as a
"where do I sit on the CCRL scale" locator, not a verdict. Conditions are kept as
close to CCRL as is cheap:

- IxEngine: **NNUE on** (`EvalFile`), **1 thread**, fixed **64 MB** hash, ponder off.
- Every opponent: same 64 MB hash, 1 thread, no tablebases.
- Short, balanced, generic opening book (`book.json`, <=6 plies), colour-reversed pairs.

## How the rating is computed

`solve.py` holds each opponent at its published CCRL 40/40 rating and finds the one
IxEngine rating `r` where, under the logistic (Elo) model, expected score equals
actual score (draws = half a point). Error bars are a **percentile bootstrap over
games** (95% CI). This is the same fixed-anchor idea as Ordo's multi-anchor mode,
implemented in pure Python so the rig has no external binary to maintain. The PGN
is emitted so you can cross-check with Ordo/Bayeselo independently.

## The anchors

See `anchors.json`. Each opponent records its exact version and the exact CCRL
40/40 rating it is anchored to. The set is chosen to **bracket** IxEngine (at least
one weaker, one stronger) so the gauntlet produces both wins and losses -- a
one-sided bracket makes the solve unreliable (the rig warns when that happens).

## Adding an upgrade comparison

Don't change the opponents or anchors -- that's what keeps runs comparable. Just
rebuild IxEngine (or point `ixengine.options.EvalFile` at a new net) and re-run
with a fresh `--tag`. Compare the new line in `history.csv` to the old.
