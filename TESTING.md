# Test log

Every number the engine has ever been judged by, in one place. Newest at the
bottom of each section. Raw per-run summaries live in `tools/results/`; the full
SPRT logs stay local (`tools/*.log`, gitignored) because they are mostly progress
lines.

## How things get tested

- **Machine:** Intel Core Ultra 7 265F (20 cores, no SMT), Windows 11, MSVC 2022
  with LTO + PGO (`build_pgo.bat`). Test binaries are always built the same way.
- **Self-play SPRT** (`tools/sprt.py`): GSPRT with bounds H0 = 0 / H1 = +5 Elo,
  alpha = beta = 0.05, so the log-likelihood ratio has to reach ±2.94. Colour-
  reversed pairs from a 20-line opening book, 64 MB hash, one thread a side unless
  stated. Short TC is 100 ms/move (or 5+0.05 for time-management changes, since
  fixed movetime hides those); long TC is 8+0.08. A change has to pass both to stay.
  Concurrency 14 for single-thread tests, 5 for four-thread tests.
- **Non-regression bounds** (H0 = -5 / H1 = 0) for bug-fix bundles that are not
  supposed to change strength.
- **Speed-only changes** (prefetch, compiler flags) are gated by identical bench
  node counts plus measured NPS, not SPRT — SPRT cannot resolve a 3 Elo change in
  any sane number of games.
- **Bench fingerprint:** `ixchess-engine.exe bench` prints the node count over five
  positions at depth 12. Two binaries with the same fingerprint search identically;
  it is the first thing checked on any build.
- **Anchor rating** (`tools/anchor/`): 100 games each against four engines held
  fixed at their CCRL 40/15 rating, TC 15+0.15, 64 MB, one thread, no book beyond
  6 plies. Solved for the one unknown rating by bisection, 95% CI by bootstrap over
  games. It is a blitz approximation of the CCRL scale, not a CCRL result.

Self-play Elo overstates cross-engine Elo, usually by 1.5–2×. Treat the SPRT
numbers as "passed / failed and by roughly how much", and the anchor numbers as the
strength.

## Anchor runs

| Date | Build | Pool | Rating | 95% CI | Score |
|---|---|---|---|---|---|
| 2026-06-25 | 1.0, gen2 net | June pool | **3088** | 3058–3116 | 60.6% |
| 2026-07-04 | + prefetch/LTO/PGO, singular ext. | June pool | **3115** | 3086–3146 | 64.2% |
| 2026-07-06 | + time mgmt, countermoves, cont. history | June pool | **3173** | 3144–3204 | 71.2% |
| 2026-09-02 | 1.1 (everything below) | **Sept pool** | **3198** | 3174–3222 | 40.2% |

Per opponent (W-D-L, IxEngine's point of view):

| Run | Cheng4 0.38 (2906) | Senpai 1.0 (2985) | Inanis 1.6.0 (3048) | Bit-Genie 9 (3098) |
|---|---|---|---|---|
| 2026-06-25 | +67 =23 -10 (78%) | +30 =32 -38 (46%) | +38 =37 -25 (56%) | +45 =33 -22 (62%) |
| 2026-07-04 | +60 =28 -12 (74%) | +41 =30 -29 (56%) | +44 =38 -18 (63%) | +47 =34 -19 (64%) |
| 2026-07-06 | +73 =22 -5 (84%) | +52 =37 -11 (70%) | +45 =38 -17 (64%) | +52 =29 -19 (66%) |

| Run | Halogen 10 (3194) | Weiss 2.0 (3265) | Zahak 10.0 (3292) | Alexandria 3.5 (3321) |
|---|---|---|---|---|
| 2026-09-02 | +33 =47 -20 (56%) | +21 =44 -35 (43%) | +11 =52 -37 (37%) | +6 =37 -57 (24%) |

The June pool was replaced because 84% against its weakest member is past the
point where the logistic model is reliable; its 3173 flattered the engine. The two
pools are not comparable. The September pool's scores fall off monotonically with
opponent strength, which is what you want to see.

## NNUE

Architecture: 768 → 512 perspective, SCReLU, 8 piece-count output buckets, int16
weights, AVX2 forward pass. Trainer: `tools/train_nnue.py`, 20 epochs, cosine LR
from 1e-3, quantised export. Data: `tools/datagen.py` self-play, quiet positions
only, `fen | cp | wdl`.

| Generation | Data | Positions | Labelled by | Final loss |
|---|---|---|---|---|
| gen1 | gen1.txt (1.49 GB) | 23,430,372 | hand eval, 3000–5000 nodes/move | 0.00682 |
| gen2 | gen1 + gen2.txt (4.52 GB) | 93,721,649 | gen1 net | 0.00816 (different mix, not comparable) |

Datagen ran at ~455 positions/s per worker with the hand eval, 18 workers.

| Test | TC | Games | W-D-L | Elo | Verdict |
|---|---|---|---|---|---|
| gen1 vs hand eval | 100 ms | 395 | +210 =110 -75 | **+124 ±30** | pass |
| gen1 vs hand eval | 8+0.08 | 277 | +153 =82 -42 | **+148 ±36** | pass |
| gen2 vs gen1 | 100 ms | 230 | +134 =70 -26 | **+177 ±40** | pass |
| gen2 vs hand eval | 100 ms | 167 | +117 =32 -18 | **+237 ±55** | pass |
| gen2 vs hand eval | 8+0.08 | 93 | +75 =12 -6 | **+332 ±89** | pass |

gen2 has been the shipped net since 2026-06-24 and is compiled into the binary
since 2026-09-01.

## Early rough matches (June 2026, hand eval era)

Small samples against a strength-limited Stockfish, kept for the record only:

- vs Stockfish `UCI_Elo 3000`, 40 games at 100 ms: +0 =21 -19, 26% (~2820 performance).
- vs Stockfish `UCI_Elo 2800`, 20 games each at 100 ms: Baseline 50%, Upgraded
  (Lazy SMP) 45%, Maxxed (gen1 net + SMP) 50%.
- Lazy SMP vs one thread, self-play: about +20 at blitz, ~+200 at 3+0.03 (the
  "+207" figure); those logs were not kept.

## Speed changes (bit-identical, gated by NPS)

Measured with `go depth N` from the start position, 256 MB hash, gen2 net, 3 runs.

| Change | Config | Before | After | Δ |
|---|---|---|---|---|
| TT prefetch in do_move | NNUE, depth 18 | 1,103,501 nps | 1,223,414 | **+10.9%** |
| TT prefetch in do_move | NNUE, depth 22 | 1,036,436 | 1,141,566 | +10.1% |
| TT prefetch in do_move | hand eval, depth 18 | 2,048,641 | 2,048,734 | 0% |
| LTO (/GL /LTCG) | NNUE, depth 18 | 1,223,414 | 1,242,840 | +1.6% |
| PGO | NNUE, depth 18 | 1,242,840 | 1,251,899 | +0.7% |
| lazy accumulator updates | NNUE, depth 18 | 1,114,975 | 1,111,710 | -0.3% → dropped |

The three kept together were also run through SPRT once: +17.8 ±10 at 100 ms
(1934 games), which is consistent with a ~13% speed-up.

## SPRT history

Each row is one test. "Kept" means it is in the engine today.

### July 2026

| Date | Change | Against | TC | Games | W-D-L | Elo | LLR | Verdict | Kept |
|---|---|---|---|---|---|---|---|---|---|
| 07-03 | prefetch + LTO + PGO | 1.0 gen2 | 100 ms | 1934 | +468 =1097 -369 | +17.8 ±10 | +2.85 | pass | yes |
| 07-04 | singular extensions + multicut | speed build | 100 ms | 2278 | +546 =1290 -442 | +15.9 ±9 | +2.92 | pass | yes |
| 07-04 | singular extensions + multicut | speed build | 8+0.08 | 2014 | +420 =1264 -330 | +15.5 ±9 | +2.93 | pass | yes |
| 07-05 | time mgmt: stability + score-drop scaling | + SE | 5+0.05 | 3573 | +721 =2234 -618 | +10.0 ±7 | +2.97 | pass | yes |
| 07-05 | time mgmt: stability + score-drop scaling | + SE | 8+0.08 | 2952 | +589 =1868 -495 | +11.1 ±8 | +2.86 | pass | yes |
| 07-05 | countermove heuristic | + TM | 100 ms | 1038 | +281 =576 -181 | +33.6 ±14 | +3.06 | pass | yes |
| 07-05 | countermove heuristic | + TM | 8+0.08 | 1288 | +278 =820 -190 | +23.8 ±11 | +3.16 | pass | yes |
| 07-05 | history-based LMR, r -= hist/8000 | + CM | 100 ms | 1111 | +198 =635 -278 | -25.1 ±13 | -2.99 | fail | no |
| 07-05 | continuation history + LMR statScore clamped ±1 | + CM | 100 ms | 1682 | +408 =967 -307 | +20.9 ±11 | +3.04 | pass | yes |
| 07-05 | continuation history + LMR statScore clamped ±1 | + CM | 8+0.08 | 1134 | +240 =735 -159 | +24.9 ±12 | +3.02 | pass | yes |
| 07-05 | capture history (malus on every cutoff, /16) | + CH | 100 ms | 1563 | +307 =868 -388 | -18.0 ±11 | -3.00 | fail | no |
| 07-05 | capture history (malus only on capture cutoffs, /32) | + CH | 100 ms | 2417 | +505 =1328 -584 | -11.4 ±9 | -3.09 | fail | no |

A first version of the countermove test read 0 wins in 63 games: the binaries were
stale (copied source files kept old timestamps and NMake skipped the recompile).
Every stage binary has been checked by bench fingerprint since.

### September 2026

| Date | Change | Against | TC | Games | W-D-L | Elo | LLR | Verdict | Kept |
|---|---|---|---|---|---|---|---|---|---|
| 09-01 | bug fixes (check-ext SEE, null-move repetition, 50-move mate) | 3173 build | 100 ms | 4013 | +820 =2384 -809 | +1.0 ±7 | +1.41 | inconclusive, non-regression bounds | yes |
| 09-01 | correction history (pawn key, clamped ±96 cp) | embedded net | 100 ms | 1883 | +384 =1036 -463 | -14.6 ±11 | -2.97 | fail | no |
| 09-01 | ProbCut (beta+180, depth ≥ 5) | embedded net | 100 ms | 4013 | +860 =2303 -850 | +0.9 ±7 | -0.64 | inconclusive | no |
| 09-01 | LMR: +1 at cut-nodes, -1 for killers/counter | embedded net | 100 ms | 819 | +140 =461 -218 | -33.2 ±16 | -2.82 | fail | no |
| 09-01 | singular ext. from depth 6 + double extension | embedded net | 100 ms | 1341 | +332 =771 -238 | +24.4 ±12 | +2.89 | pass | yes |
| 09-01 | singular ext. from depth 6 + double extension | embedded net | 8+0.08 | 903 | +187 =603 -113 | +28.5 ±13 | +2.98 | pass | yes |
| 09-01 | time mgmt: effort (node-fraction) term | + SE tune | 5+0.05 | 1279 | +196 =825 -258 | -16.9 ±11 | -2.91 | fail | no |
| 09-01 | Lazy SMP: staggered helper depths + vote, **4 threads** | + SE tune | 100 ms | 1631 | +342 =1035 -254 | +18.8 ±10 | +3.03 | pass | yes |
| 09-01 | history bonus 16d²+32d cap 1600 (was d² cap 1200) | + SE tune | 100 ms | 393 | +122 =231 -40 | **+73.6 ±22** | +3.09 | pass | yes |
| 09-01 | history bonus 16d²+32d cap 1600 | + SE tune | 8+0.08 | 450 | +113 =291 -46 | **+52.1 ±19** | +2.77 | pass | yes |
| 09-01 | LMR -1 for killers/counter (alone) | + history | 100 ms | 2696 | +546 =1611 -539 | +1 ±8 | -0.44 | stopped, neutral | no |
| 09-01 | Lazy SMP patch, **4 threads** | + history | 8+0.08 | 1239 | +147 =955 -137 | +3 ±9 | +0.07 | stopped, neutral (77% draws) | yes, on the STC result |
| 09-02 | LMR statScore /12000 clamped ±2 | + history | 100 ms | 2790 | +567 =1676 -547 | +2 ±8 | 0.00 | stopped, neutral | no |

The history-bonus result is the one to remember: with `bonus = depth²` the tables
were getting 64 points at depth 8 against a 16384 gravity, so history and
continuation history had been running almost empty. Scale checks come before new
heuristics from now on.

## Bench fingerprints

`bench` node counts for each build, so any of them can be reproduced.

| Build | Eval | Nodes |
|---|---|---|
| 1.0 (2026-06) | hand | 369,616 |
| + singular extensions | hand | 534,507 |
| + countermoves | hand | 565,439 |
| + continuation history | hand | 559,197 |
| + Sept bug fixes | hand | 474,231 |
| + embedded net (bench now runs NNUE) | NNUE | 308,369 |
| + SE tuning | NNUE | 473,174 |
| + history bonus (1.1, current) | NNUE | 530,521 |

## Perft

All six standard positions are checked after any change to move generation or
make/unmake: startpos d5 4,865,609 · Kiwipete d5 193,690,690 · position 3 d6
11,030,083 · position 4 d5 15,833,292 · position 5 d5 89,941,194 · position 6 d5
164,075,551.
