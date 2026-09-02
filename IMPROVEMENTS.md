# Notes / todo

Things I want to try, roughly in the order I think they pay off. Everything
goes through `tools/sprt.py` at 100ms/move and 8+0.08 before it stays; the
results of everything tried so far are in TESTING.md.

## Next

- gen3 net: datagen with the current engine (it's a lot stronger than the one
  that labelled gen2), retrain, try HL 768 at the same time. Probably on a
  cheap ARM box left running for a few weeks — the build no longer forces AVX2
  on non-x86, and the scalar path is bit-identical.
- tag a proper 1.1 release with Windows and Linux binaries, then CCRL and/or a
  Lichess bot for a real rating.
- SMP at long TC. The staggered-depth + vote patch was +19 at 100ms with four
  threads but only +3 at 8+0.08 (mostly draws). There should be more there.
- king-bucketed inputs, after gen3.
- go through every heuristic and check its scale against its divisor. The
  history bonus was sitting at depth² = 64 against a 16384 gravity for months
  and fixing that alone was worth +70 in self-play. There may be more like it.

## Tried and dropped

- correction history, twice. First version blew the tree up 3.6x. Second was a
  clean pawn-key EMA clamped at ±96 cp with the raw eval kept in the TT: node
  count fine, -15 in games. Not a third time without a new idea.
- ProbCut (beta+180, depth ≥ 5, d-4 verify): +1 at 4000 games. Null move and
  reverse futility already cover it.
- history-based LMR on its own (`r -= hist/8000`): -25. Over-reduces. The
  clamped ±1 version inside the continuation-history patch is what works.
- LMR +1 at cut-nodes: -33 together with the killer tweak; the killer tweak
  alone was +1. The reductions are already aggressive here.
- capture history: -18, then -11 with the malus limited to capture cutoffs.
  MVV-LVA + SEE is already fine.
- time management "effort" term (less time when the best move ate most of the
  search): -17 on top of the stability scaling, which already covers it.
- LMR statScore clamped ±2 at /12000: +2, flat.
- lazy NNUE accumulator updates: bit-identical, no speed change at HL 512.
  Nearly every node evaluates anyway.
- PGO: +0.7%. Kept because it's free, but not worth talking about.

## Known small things

- SEE treats a promoting pawn as a pawn.
- insufficient material only covers K v K and K+minor v K.
- no pondering, no syzygy.
- `UCI_LimitStrength` is a plain depth cap.
