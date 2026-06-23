"""Measure all three engine modes vs Stockfish at one TC, for a comparable table.

  python tools/modes_elo.py --elo 2800 --games 24 --movetime 100 --stockfish <path>

Baseline = 1 thread + HCE,  Upgraded = SMP + HCE,  Maxxed = SMP + NNUE.
"""

import argparse
import math
import time
from pathlib import Path

import chess
import chess.engine

ROOT = Path(__file__).resolve().parents[1]


def perf_elo(score, opp):
    if score <= 0:
        return opp - 400
    if score >= 1:
        return opp + 400
    return opp + 400 * math.log10(score / (1 - score))


def play(ours, sf, our_white, mt, max_moves=200):
    board = chess.Board()
    lim = chess.engine.Limit(time=mt / 1000)
    while not board.is_game_over(claim_draw=True) and board.fullmove_number <= max_moves:
        eng = ours if (board.turn == chess.WHITE) == our_white else sf
        m = eng.play(board, lim).move
        if m is None:
            break
        board.push(m)
    return board.result(claim_draw=True) if board.is_game_over(claim_draw=True) else "1/2-1/2"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=str(ROOT / "bin" / "ixchess-engine.exe"))
    ap.add_argument("--net", default=str(ROOT / "bin" / "ix.nnue"))
    ap.add_argument("--stockfish", required=True)
    ap.add_argument("--elo", type=int, default=2800)
    ap.add_argument("--games", type=int, default=24)
    ap.add_argument("--movetime", type=int, default=100)
    ap.add_argument("--threads", type=int, default=4)
    args = ap.parse_args()

    net = args.net if Path(args.net).exists() else "<empty>"
    modes = [
        ("Baseline", {"Threads": 1, "EvalFile": "<empty>"}),
        ("Upgraded", {"Threads": args.threads, "EvalFile": "<empty>"}),
        ("Maxxed",   {"Threads": args.threads, "EvalFile": net}),
    ]

    ours = chess.engine.SimpleEngine.popen_uci(args.engine)
    sf = chess.engine.SimpleEngine.popen_uci(args.stockfish)
    sf.configure({"UCI_LimitStrength": True, "UCI_Elo": args.elo})

    print(f"vs Stockfish {args.elo}, {args.movetime} ms/move, {args.games} games each\n")
    results = []
    for name, cfg in modes:
        try:
            ours.configure(cfg)
        except Exception:
            pass
        w = d = l = 0
        for i in range(args.games):
            our_white = i % 2 == 0
            r = play(ours, sf, our_white, args.movetime)
            if r == "1/2-1/2":
                d += 1
            elif (r == "1-0") == our_white:
                w += 1
            else:
                l += 1
            score = (w + 0.5 * d) / (i + 1)
            print(f"  {name:9s} {i+1:>2}/{args.games}  +{w} ={d} -{l}  ({score:.0%})   ", end="\r")
        score = (w + 0.5 * d) / args.games
        elo = perf_elo(score, args.elo)
        results.append((name, w, d, l, score, elo))
        print(f"  {name:9s}  +{w} ={d} -{l}   {score:5.1%}   perf ~{elo:.0f} Elo            ")

    ours.quit()
    sf.quit()
    print("\n=== summary (vs SF {} @ {} ms) ===".format(args.elo, args.movetime))
    for name, w, d, l, score, elo in results:
        print(f"  {name:9s}  {score:5.1%}   ~{elo:.0f} Elo")


if __name__ == "__main__":
    main()
