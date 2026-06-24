# IxEngine

A classical (alpha-beta) chess engine in C++17 — bitboards, PVS, a transposition
table, Lazy SMP, a hand-tuned evaluation, and an optional NNUE net. It speaks UCI, so
any GUI or script that drives Stockfish drives it too.

## Strength

Two evaluations: the hand-crafted one (default) and an **optional NNUE net**
(`EvalFile`). The NNUE, trained on 23M self-play positions, beats the hand eval by
a wide, SPRT-confirmed margin (self-play, 1 thread):

| Test | Result |
|---|---|
| NNUE vs hand eval, 100 ms | **+124 ±30 Elo** |
| NNUE vs hand eval, 8+0.08 | **+148 ±36 Elo** |

Vs Stockfish 18 (`UCI_LimitStrength`, 100 ms): the hand eval (Baseline) crosses 50%
at **~2800**; NNUE (Maxxed) adds ~+124 on top of that. **Lazy SMP** adds ~+200 more
at long time control (much less at blitz, where the search is too short for the
helper threads to matter). Numbers are on the "vs Stockfish at this TC" scale —
*not* CCRL/FIDE.

## Features

- Bitboard board with **fancy magic bitboards** for sliders (no BMI2 needed).
- **Zobrist hashing** + a clustered, aging **transposition table**.
- Search: iterative deepening, **PVS**, aspiration windows, **quiescence** (SEE +
  delta pruning), **null-move pruning**, **LMR**, reverse-futility / late-move /
  SEE pruning, check extensions, mate-distance pruning.
- Move ordering: TT move → MVV-LVA + SEE captures → killers → history → quiets.
- **Lazy SMP** multithreading over a shared TT (the `Threads` option).
- Hand-tuned eval: tapered PeSTO piece-square tables, mobility, king safety (ring
  attacks + pawn shelter), pawn structure (doubled / isolated / passed), bishop
  pair, rooks on open/semi-open files and the 7th.
- Move generation is perft-verified on the six standard positions.
- Optional **NNUE evaluation** (`768→512` perspective, SCReLU, 8 buckets) with
  incremental accumulators + AVX2 — trained by the included PyTorch pipeline.

## Modes

All three are the *same binary*, selected by UCI options (and by a dropdown in the
web UI):

| Mode | Settings |
|---|---|
| **Baseline** | `Threads 1`, hand-crafted eval |
| **Upgraded** | `Threads N`, hand-crafted eval (Lazy SMP) |
| **Maxxed** | NNUE eval (`EvalFile`) + `Threads N` |

## Build (Windows / MSVC)

```bat
build.bat
```

Finds `vcvars64.bat`, runs CMake (NMake, Release), writes `bin\ixchess-engine.exe`.
By hand from a Developer Command Prompt:

```bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

MinGW/Clang also work (`-O3 -mpopcnt` off MSVC). CMake ≥ 3.15.

## UCI options

| Option | Type | Default | Notes |
|---|---|---|---|
| `Hash` | spin | 64 | TT size in MB. |
| `Threads` | spin | 1 | Search threads (Lazy SMP). Big gains at longer TC. |
| `Move Overhead` | spin | 25 | ms shaved off the clock for safety. |
| `UCI_LimitStrength` | check | false | Weaken the engine. |
| `UCI_Elo` | spin | 2850 | Target when limiting (1320–3000). |

`go` understands `movetime`, `wtime/btime/winc/binc/movestogo`, `depth`, `nodes`,
and `infinite` (+ `stop`).

## Play against it

Browser UI — clickable board, eval bar, move list, last-move arrows (front-end is
AI-generated):

```bash
pip install flask python-chess
python tools/webui/server.py          # then open http://127.0.0.1:5000
```

Terminal:

```bash
python tools/play.py --color white --movetime 1000   # moves as e2e4 or Nf3
```

Quick CLI checks:

```bat
echo uci | bin\ixchess-engine.exe
bin\ixchess-engine.exe perft 5 "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
bin\ixchess-engine.exe bench
```

On the UCI prompt: `d` (print board), `eval`, `perft N`.

## Development & testing

Every engine change is validated by self-play SPRT before it's kept — that's how
SMP was confirmed (+207 at 3+0.03) and how two eval/ordering experiments were
correctly rejected.

```bash
# A vs B with sequential probability ratio test (GSPRT, opening book, concurrency)
python tools/sprt.py --a bin/new.exe --b bin/base.exe --tc 5+0.05 --concurrency 12

# straight head-to-head Elo between two builds
python tools/selfplay.py --a bin/new.exe --b bin/base.exe --games 100 --movetime 100

# vs Stockfish at a target Elo
python tools/match.py --elo 2800 --games 40 --movetime 100 --threads 4 --stockfish <path>
```

Test new changes at **both 100 ms and ~3 s**, since some gains (e.g. SMP) only show
at longer TC.

## Layout

```
src/
  types.h        enums, move encoding, bit ops
  bitboard.*     masks, leaper tables, magic sliders
  zobrist.*      hash keys
  position.*     board state, FEN, make/unmake, attacks, SEE
  movegen.*      pseudo-legal generation
  perft.*        move-gen validation
  tt.*           transposition table
  eval.*         hand-tuned evaluation
  search.*       PVS + iterative deepening + Lazy SMP
  main.cpp       UCI front-end
tools/
  play.py        terminal human-vs-engine
  webui/         browser UI (server.py + index.html)
  match.py       vs Stockfish
  sprt.py        SPRT A/B tester
  selfplay.py    head-to-head Elo between two builds
```

See [IMPROVEMENTS.md](IMPROVEMENTS.md) for the roadmap (NNUE next) and review notes.
