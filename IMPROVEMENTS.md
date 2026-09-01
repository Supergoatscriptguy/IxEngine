# Notes / todo

Things I want to try, roughly in the order I think they pay off. Everything
goes through `tools/sprt.py` at 100ms/move and 8+0.08 before it stays.

## Next

- gen3 net: datagen with the current engine (it's ~+90 stronger than the one
  that made gen2), retrain, try HL 768 at the same time.
- embed the net in the exe and make it the default eval. Right now anyone who
  runs the binary without `EvalFile` gets the old hand eval.
- fresh anchor pool; the current four are getting too weak to measure against
  (84% vs Cheng4).
- clang-cl build. Never compared it to MSVC.
- king-bucketed inputs, after gen3.

## Tried and dropped

- history-based LMR on its own (`r -= hist/8000`): -25. Over-reduces. The
  clamped version inside the continuation-history patch is what works.
- capture history: -18, then -11 with the malus limited to capture cutoffs.
  MVV-LVA + SEE is already fine here.
- lazy NNUE accumulator updates: bit-identical, no speed change at HL 512.
  Nearly every node evaluates anyway.
- correction history: tree blew up 3.6x. Probably a missing clamp; not retried.
- PGO: +0.7%. Kept because it's free, but not worth talking about.

## Known small things

- SEE treats a promoting pawn as a pawn.
- insufficient material only covers K v K and K+minor v K.
- no pondering, no syzygy.
- `UCI_LimitStrength` is a plain depth cap.
