"""Play two UCI engines head-to-head — for measuring whether a change helps.

  python tools/selfplay.py --a bin/ixchess-engine.exe --b bin/ixchess-base.exe \
      --games 60 --movetime 100

Alternates colours every game and reports A's score + the implied Elo gap.
"""

import argparse
import math
import time

import chess
import chess.engine


def play(a, b, a_is_white, movetime):
    board = chess.Board()
    limit = chess.engine.Limit(time=movetime / 1000)
    while not board.is_game_over(claim_draw=True) and board.fullmove_number <= 200:
        eng = a if (board.turn == chess.WHITE) == a_is_white else b
        res = eng.play(board, limit)
        if res.move is None:
            break
        board.push(res.move)
    if board.is_game_over(claim_draw=True):
        return board.result(claim_draw=True)
    return "1/2-1/2"


def elo_diff(score):
    if score <= 0.0:
        return -800.0
    if score >= 1.0:
        return 800.0
    return -400.0 * math.log10(1.0 / score - 1.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True, help="engine A (the change under test)")
    ap.add_argument("--b", required=True, help="engine B (the baseline)")
    ap.add_argument("--games", type=int, default=60)
    ap.add_argument("--movetime", type=int, default=100)
    args = ap.parse_args()

    a = chess.engine.SimpleEngine.popen_uci(args.a)
    b = chess.engine.SimpleEngine.popen_uci(args.b)
    print(f"A = {a.id.get('name')}  ({args.a})")
    print(f"B = {b.id.get('name')}  ({args.b})")
    print(f"{args.games} games, {args.movetime} ms/move\n")

    w = d = l = 0
    t0 = time.time()
    for i in range(args.games):
        res = play(a, b, i % 2 == 0, args.movetime)
        a_white = i % 2 == 0
        if res == "1/2-1/2":
            d += 1
        elif (res == "1-0") == a_white:
            w += 1
        else:
            l += 1
        score = (w + 0.5 * d) / (i + 1)
        print(f"  {i+1:>3}/{args.games}  A: +{w} ={d} -{l}  ({score:.1%})  "
              f"[{elo_diff(score):+.0f} Elo]", end="\r")

    a.quit()
    b.quit()
    n = args.games
    score = (w + 0.5 * d) / n
    print(f"\n\nA vs B: +{w} ={d} -{l}   score {score:.1%}   "
          f"Elo diff {elo_diff(score):+.0f}   ({time.time()-t0:.0f}s)")


if __name__ == "__main__":
    main()
