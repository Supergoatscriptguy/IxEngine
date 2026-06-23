"""Play a game against IxEngine in your terminal.

  python tools/play.py                         # you're White, 1s/move
  python tools/play.py --color black --movetime 3000
  python tools/play.py --depth 14              # fixed depth instead of time

Enter moves as UCI (e2e4) or SAN (Nf3). Commands: undo, board, quit.
"""

import argparse
from pathlib import Path

import chess
import chess.engine

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ENGINE = ROOT / "bin" / "ixchess-engine.exe"


def show(board, last=None):
    print()
    try:
        print(board.unicode(borders=True))
    except UnicodeEncodeError:        # some Windows terminals can't do the glyphs
        print(board)
    if last is not None:
        print(f"last move: {last.uci()}")
    line = "White" if board.turn else "Black"
    print(f"{line} to move" + ("  — check!" if board.is_check() else ""))


def parse_move(board, text):
    for fn in (chess.Move.from_uci, board.parse_san):
        try:
            m = fn(text)
            if m in board.legal_moves:
                return m
        except (ValueError, chess.InvalidMoveError,
                chess.IllegalMoveError, chess.AmbiguousMoveError):
            pass
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=str(DEFAULT_ENGINE))
    ap.add_argument("--color", choices=["white", "black"], default="white")
    ap.add_argument("--movetime", type=int, default=1000, help="engine ms/move")
    ap.add_argument("--depth", type=int, default=0, help="fixed depth (overrides movetime)")
    ap.add_argument("--hash", type=int, default=128)
    args = ap.parse_args()

    you = chess.WHITE if args.color == "white" else chess.BLACK
    eng = chess.engine.SimpleEngine.popen_uci(args.engine)
    eng.configure({"Hash": args.hash})
    limit = (chess.engine.Limit(depth=args.depth) if args.depth
             else chess.engine.Limit(time=args.movetime / 1000))

    print(f"You are {args.color}. UCI (e2e4) or SAN (Nf3). Commands: undo, board, quit.")
    board, last = chess.Board(), None

    while not board.is_game_over(claim_draw=True):
        show(board, last)
        if board.turn == you:
            try:
                cmd = input("your move: ").strip()
            except (EOFError, KeyboardInterrupt):
                print(); break
            low = cmd.lower()
            if low in ("quit", "q", "resign"):
                print("you resigned."); break
            if low == "board":
                continue
            if low == "undo":
                if len(board.move_stack) >= 2:
                    board.pop(); board.pop()
                    last = board.peek() if board.move_stack else None
                else:
                    print("nothing to undo.")
                continue
            m = parse_move(board, cmd)
            if m is None:
                print("illegal or unreadable move, try again."); continue
            board.push(m); last = m
        else:
            print("IxEngine thinking...")
            res = eng.play(board, limit, info=chess.engine.INFO_SCORE)
            san = board.san(res.move)
            sc = res.info.get("score")
            tag = ""
            if sc is not None:
                cp = sc.white().score(mate_score=100000)
                tag = f"   (eval {cp/100:+.2f} for White)"
            print(f"IxEngine plays {san}{tag}")
            board.push(res.move); last = res.move

    show(board, last)
    print("result:", board.result(claim_draw=True))
    eng.quit()


if __name__ == "__main__":
    main()
