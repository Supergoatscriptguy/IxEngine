"""Head-to-head match: IxEngine vs Stockfish (UCI_Elo-limited).

Both engines run through python-chess (popen_uci + engine.play(Limit(time=...))),
so a clean match here means IxEngine is a drop-in for any UCI driver.

Usage:
  python tools/match.py --elo 2800 --games 40 --movetime 100 \
      --stockfish /path/to/stockfish
"""

import argparse
import math
import time
from pathlib import Path

import chess
import chess.engine

ROOT = Path(__file__).resolve().parents[1]  # engine/repo root
DEFAULT_ENGINE = ROOT / "bin" / "ixchess-engine.exe"
DEFAULT_STOCKFISH = "stockfish"  # on PATH, or pass --stockfish


def play_game(ours, sf, our_color, movetime, sf_elo, max_moves=200):
    board = chess.Board()
    sf.configure({"UCI_LimitStrength": True, "UCI_Elo": sf_elo})
    limit = chess.engine.Limit(time=movetime / 1000.0)

    while not board.is_game_over(claim_draw=True) and board.fullmove_number <= max_moves:
        engine = ours if board.turn == our_color else sf
        result = engine.play(board, limit)
        if result.move is None:
            break
        board.push(result.move)

    if board.is_game_over(claim_draw=True):
        return board.result(claim_draw=True)
    return "1/2-1/2"


def perf_elo(score, opp_elo):
    if score <= 0.0:
        return opp_elo - 400
    if score >= 1.0:
        return opp_elo + 400
    return opp_elo + 400 * math.log10(score / (1 - score))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=str(DEFAULT_ENGINE))
    ap.add_argument("--stockfish", default=str(DEFAULT_STOCKFISH))
    ap.add_argument("--elo", type=int, default=2200, help="Stockfish UCI_Elo")
    ap.add_argument("--games", type=int, default=20)
    ap.add_argument("--movetime", type=int, default=100, help="ms per move (both sides)")
    args = ap.parse_args()

    ours = chess.engine.SimpleEngine.popen_uci(args.engine)
    sf = chess.engine.SimpleEngine.popen_uci(args.stockfish)
    print(f"Engine: {ours.id.get('name')}")
    print(f"Opponent: Stockfish @ UCI_Elo {args.elo}, {args.movetime} ms/move, {args.games} games\n")

    w = d = l = 0
    t0 = time.time()
    for i in range(args.games):
        our_color = chess.WHITE if i % 2 == 0 else chess.BLACK
        res = play_game(ours, sf, our_color, args.movetime, args.elo)
        if res == "1/2-1/2":
            d += 1
            sym = "="
        elif (res == "1-0" and our_color == chess.WHITE) or (res == "0-1" and our_color == chess.BLACK):
            w += 1
            sym = "+"
        else:
            l += 1
            sym = "-"
        score = (w + 0.5 * d) / (i + 1)
        print(f"  game {i+1:>3}/{args.games} [{'W' if our_color==chess.WHITE else 'B'}] {sym}  "
              f"running: +{w} ={d} -{l}  ({score:.0%})")

    ours.quit()
    sf.quit()

    n = args.games
    score = (w + 0.5 * d) / n
    print(f"\nResult vs SF {args.elo}:  +{w} ={d} -{l}   score {score:.1%}")
    print(f"Estimated performance Elo: ~{perf_elo(score, args.elo):.0f}")
    print(f"({n} games in {time.time()-t0:.1f}s)")


if __name__ == "__main__":
    main()
