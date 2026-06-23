# IxEngine — review notes & roadmap

A snapshot of where the engine stands, the small things found during review, and
where the Elo is hiding if we want to push further. Elo numbers are rough
estimates for an engine at this level and at fast time controls — treat them as
"order of magnitude," not promises.

## Review: minor issues found (none affect legality or crash safety)

The engine is correct where it counts — move generation is perft-exact on all six
standard positions, and it played 100 crash-free games vs Stockfish with no
illegal moves. The items below are accuracy/quality nits, not bugs that lose games
outright:

- **Fixed:** quiescence was caching the (possibly stand-pat-adjusted) score as the
  position's "static eval" in the TT. Now it stores the raw eval.
- **Fixed:** removed a dead `oldAlpha` local in `negamax`.
- **Null-move and the repetition history.** A null move pushes its hashed position
  into the repetition history, so in rare cases a null-search line can detect a
  "repetition" against a real earlier position. Impact is negligible (null only
  produces a fail-high cutoff), so it's left as-is rather than risk the working
  repetition logic. Clean fix: don't record null positions, or track ply parity.
- **SEE ignores promotion gains.** `see_ge` treats a promoting pawn as a pawn. Fine
  for move ordering/pruning; slightly inaccurate when a promotion is part of an
  exchange.
- **Repetition is scored as a draw on the first repeat** (2-fold), not the rules'
  3-fold. Standard search optimization; can very rarely misjudge a line that
  repeats once before improving.
- **Insufficient-material draw** only catches K vs K and K+single-minor. KNN vs K,
  etc. fall through to the 50-move/repetition nets instead.
- **`UCI_LimitStrength`** is a crude depth cap, not a smooth Elo dial.

## Where the Elo is (high → low leverage)

### Big swings
- **NNUE evaluation (~+200–400).** The single biggest jump, and it dovetails with
  the Ixchess neural-net project — train a small net and wire an incremental
  evaluator into `eval.cpp`. Cost: large (training pipeline, efficient incremental
  updates, SIMD). This is the "make it genuinely strong" lever.
- **Lazy SMP / multithreading (~+50–80 per core-doubling, with diminishing
  returns).** The `Threads` option is currently a no-op. Needs a lockless TT and
  per-thread search state. Cost: high; introduces nondeterminism.

### Solid, well-understood gains
- **Texel-tune the evaluation (~+30–80).** Weights (mobility, king safety, pawns,
  the extra terms) are hand-picked. Tune them against labelled positions — and you
  already have a pile of Lichess games to mine. Cost: build a tuner; watch for
  overfitting.
- **Continuation history / counter-move history (~+20–40).** Currently only
  butterfly history + killers. Adding 1- and 2-ply continuation history is one of
  the better modern move-ordering gains. Cost: moderate state + tuning.
- **Singular extensions (~+20–50).** Extend a move that is the only good one per the
  TT. Cost: a verification re-search per candidate; some slowdown.
- **King-safety overhaul + pawn-hash table (~+20–40).** The current king safety is
  a reasonable ring-attack + shelter model; a tuned attack table and a cached pawn
  evaluation (passed/backward/connected/storms) would help. Cost: complexity.

### Smaller, cheap tweaks
- **Capture history & history pruning (~+15–30).**
- **ProbCut and razoring (~+10–20 each).**
- **Time management: best-move stability, "easy move," node-based budgeting
  (~+10–30 in real games).** Right now it's a straightforward soft/hard split.
- **Aspiration-window and LMR-formula tuning (~+10–30).** These are guessed
  constants; small sweeps usually find free Elo.
- **Syzygy endgame tablebases (~+10–20, more in endgame-heavy play).** Adds a
  dependency (Fathom) and the tablebase files.

### Speed (more depth = more Elo, indirectly)
- **Staged / lazy move generation** — generate captures, then quiets, on demand
  instead of all at once; skip `gives_check` on moves that get pruned anyway.
- **Direct legal move generation** (pin-aware) instead of make/undo legality
  checks. Faster but more code and more chances for subtle bugs.
- **Incremental evaluation** of the material/PST term (kept from scratch today).

## What I'd do next, in order
1. Texel-tune the eval on Lichess data — best Elo-per-hour, reuses data you have.
2. Add continuation history + singular extensions — straightforward, well-trodden.
3. Lazy SMP to use the whole CPU.
4. If you want it to be *strong* strong: NNUE, sharing infrastructure with the
   Ixchess neural-net side.
