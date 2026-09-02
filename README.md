# IxEngine

A chess engine written from scratch in C++17. Bitboards, alpha-beta with the usual
modern pruning, a transposition table, Lazy SMP, and an NNUE evaluation trained
entirely on its own self-play games. It talks UCI, so anything that can run
Stockfish can run this.

Current version: **1.1**. Roughly **3200 on the CCRL blitz scale** (details below).

## How strong

The engine plays a 400-game gauntlet against four open-source engines with known
CCRL ratings. Their ratings are held fixed and IxEngine's is solved for
(`tools/anchor/`). One thread, 64 MB hash, 15+0.15, September 2026:

| Opponent | CCRL 40/15 | Score |
|---|---|---|
| Halogen 10 | 3194 | 56% |
| Weiss 2.0 | 3265 | 43% |
| Zahak 10.0 | 3292 | 37% |
| Alexandria 3.5 | 3321 | 24% |

**IxEngine ≈ 3198, 95% CI 3174–3222.**

That is a blitz approximation of the CCRL scale, not an actual CCRL listing.
Strength shifts with the time control, and this pool only covers one region of the
list. Earlier runs used a weaker pool (Cheng4, Senpai, Inanis, Bit-Genie) and read
3088 → 3115 → 3173 as the engine improved through the summer, but that pool had
saturated by the end and those numbers are not comparable to this one.

The net is worth about +240 over the hand-written eval at 100 ms and more at
longer time controls. Everything, including every failed experiment, is in
[TESTING.md](TESTING.md).

## What's in it

**Board:** bitboards with fancy magic sliders (no BMI2 needed), Zobrist hashing,
perft-verified move generation.

**Search:** iterative deepening, principal variation search, aspiration windows,
quiescence with SEE and delta pruning, null move, reverse futility, late move
pruning, SEE pruning, late move reductions, singular extensions with multicut and
double extensions, check extensions, mate-distance pruning. Move ordering is
TT move, captures by MVV-LVA and SEE, killers, countermove, then butterfly plus
one- and two-ply continuation history.

**Time management:** the soft limit scales with how stable the best move has been
and whether the score just dropped.

**Threads:** Lazy SMP over a shared table. Helpers stagger their depths and the
final move is a depth- and score-weighted vote across threads.

**Evaluation:** a 768→512 perspective NNUE (SCReLU, eight piece-count buckets,
int16, AVX2) compiled into the binary. It was bootstrapped over two self-play
generations: gen1 was labelled by the hand eval, gen2 by the gen1 net. The
hand-written eval (tapered PeSTO tables, mobility, king safety, pawn structure) is
still there behind `EvalFile <empty>`.

## Building

Windows, MSVC 2022, CMake 3.15+, Python (used at build time to embed the net):

```bat
build.bat        # normal build
build_pgo.bat    # profile-guided build, what the numbers above were measured with
```

Both find `vcvars64.bat` on their own and leave `bin\ixchess-engine.exe`. Other
compilers work too — the CMake file only adds AVX2 flags on x86:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## UCI options

| Option | Default | |
|---|---|---|
| `Hash` | 64 | table size in MB |
| `Threads` | 1 | search threads |
| `Move Overhead` | 25 | ms kept back from the clock |
| `EvalFile` | `<embedded>` | `<embedded>` for the built-in net, `<empty>` for the hand eval, or a path to another `.nnue` of the same shape |
| `UCI_LimitStrength` / `UCI_Elo` | off / 2850 | a plain depth cap, fine for a sparring partner |

`go` understands `movetime`, `wtime/btime/winc/binc/movestogo`, `depth`, `nodes`
and `infinite`. On the prompt, `d` prints the board, `bench` and `perft N` do what
you'd expect.

## Playing it

The browser UI has a clickable board, eval bar and move list (the front-end was
generated with AI):

```
pip install flask python-chess
python tools/webui/server.py      # http://127.0.0.1:5000
```

Or in a terminal: `python tools/play.py --color white --movetime 1000`.

## Testing

Nothing goes in without a self-play SPRT at both 100 ms/move and 8+0.08 (see
`tools/sprt.py`), and the anchor gauntlet is re-run after a batch of changes.
Speed-only changes are checked by identical `bench` node counts and measured NPS
instead. The full record — every pass, every fail, the anchor runs, the training
runs — is in [TESTING.md](TESTING.md), with per-run summaries in
`tools/results/`. Ideas that are queued or already tried are in
[IMPROVEMENTS.md](IMPROVEMENTS.md).

```
python tools/sprt.py --a bin/new.exe --b bin/base.exe --movetime 100 --concurrency 14
python tools/sprt.py --a bin/new.exe --b bin/base.exe --tc 8+0.08 --concurrency 14
python tools/anchor/run_anchor.py --tag whatever
```

## Layout

```
src/            the engine (types, bitboard, zobrist, position, movegen, tt, eval, nnue, search, main)
nets/           trained nets; ix-gen2.nnue is the one that gets embedded
tools/
  sprt.py       A/B testing
  anchor/       CCRL-anchored rating gauntlet
  results/      one summary file per test run
  datagen.py    self-play data generation
  train_nnue.py PyTorch trainer, quantised export
  embed_net.py  turns a .nnue into a C++ array at build time
  webui/, play.py, match.py, selfplay.py, modes_elo.py
```
