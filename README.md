# IxEngine

A classical (alpha-beta) chess engine written in C++17 — bitboards, PVS, a
transposition table, and a hand-tuned evaluation. It speaks UCI, so any GUI or
script that talks to Stockfish can talk to it.

At 100 ms/move it scores roughly even with Stockfish 18 limited to **~2800
UCI_Elo** (results below).

## Strength

Played through python-chess (`engine.play(Limit(time=0.1))`) against Stockfish 18
with `UCI_LimitStrength`, 100 ms/move:

| Opponent (SF UCI_Elo) | Score (IxEngine) | W–D–L |
|----------------------:|:----------------:|:-----:|
| 2200 | 93% | 28–0–2 |
| 2700 | 78% | 20–7–3 |
| 2800 | 49% | 9–21–10 |
| 2900 | 31% | 2–21–17 |

The ~50% crossover sits right at **~2800** (40 games at 2800: 48.8%). Caveat: Stockfish's
`UCI_Elo` at very fast time controls isn't the same as a CCRL/FIDE rating, and a
classical engine closes the gap on Stockfish more at 100 ms than it would at long
time controls. So read this as "≈2800 at blitz vs Stockfish," not an absolute
rating.

## Features

- Bitboard board with **fancy magic bitboards** for sliders (no BMI2 needed).
- **Zobrist hashing** + a clustered, aging **transposition table**.
- Search: iterative deepening, **PVS**, aspiration windows, **quiescence** (SEE +
  delta pruning), **null-move pruning**, **LMR**, reverse-futility / late-move /
  SEE pruning, check extensions, mate-distance pruning.
- Move ordering: TT move → MVV-LVA + SEE captures → killers → history → quiets.
- **Lazy SMP** multithreading over a shared TT (the `Threads` option).
- Evaluation: tapered PeSTO piece-square tables, mobility, king safety (ring
  attacks + pawn shelter), pawn structure (doubled / isolated / passed), bishop
  pair, rooks on open/semi-open files and the 7th.
- Move generation is perft-verified on the six standard test positions.

## Build (Windows / MSVC)

```bat
build.bat
```

Finds `vcvars64.bat`, runs CMake (NMake, Release), and writes
`bin\ixchess-engine.exe`. Or by hand from a Developer Command Prompt:

```bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

MinGW/Clang also work (the build uses `-O3 -mpopcnt` off MSVC). CMake ≥ 3.15.

## UCI options

| Option | Type | Default | Notes |
|---|---|---|---|
| `Hash` | spin | 64 | TT size in MB. |
| `Threads` | spin | 1 | Search threads (Lazy SMP). 4 threads ≈ +200 Elo. |
| `Move Overhead` | spin | 25 | ms shaved off the clock for safety. |
| `UCI_LimitStrength` | check | false | Weaken the engine. |
| `UCI_Elo` | spin | 2850 | Target when limiting (1320–3000). |

`go` understands `movetime`, `wtime/btime/winc/binc/movestogo`, `depth`, `nodes`,
and `infinite` (+ `stop`).

## Try it

```bat
:: UCI handshake
echo uci | bin\ixchess-engine.exe

:: Move-gen correctness on a FEN
bin\ixchess-engine.exe perft 5 "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

:: Fixed-depth bench
bin\ixchess-engine.exe bench
```

On the UCI prompt you also get `d` (print board), `eval`, and `perft N`.

## Play against it

Browser UI — a clickable board (the front-end was generated with AI):

```bash
pip install flask python-chess
python tools/webui/server.py          # then open http://127.0.0.1:5000
```

Or straight in the terminal:

```bash
python tools/play.py --color white --movetime 1000   # moves as e2e4 or Nf3
```

## Benchmarking vs Stockfish

`tools/match.py` plays IxEngine against Stockfish through python-chess:

```bash
python tools/match.py --elo 2800 --games 40 --movetime 100 --stockfish /path/to/stockfish
```

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
  eval.*         tapered evaluation
  search.*       PVS + iterative deepening + pruning
  main.cpp       UCI front-end
tools/match.py   engine-vs-Stockfish harness
```

See [IMPROVEMENTS.md](IMPROVEMENTS.md) for the review notes and where the next Elo
is hiding.
