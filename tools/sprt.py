"""Local SPRT tester — decide whether engine A is stronger than engine B.

Plays colour-reversed pairs from a small opening book, accumulates W/D/L, and
runs a sequential probability ratio test (GSPRT, trinomial). Stops as soon as
the log-likelihood ratio crosses a bound, or at --maxgames.

  # is the change an improvement? (H0: <=0 Elo, H1: >=5 Elo)
  python tools/sprt.py --a bin/new.exe --b bin/base.exe --tc 5+0.05 --concurrency 4

  # fixed nodes per move (low variance, reproducible)
  python tools/sprt.py --a bin/new.exe --b bin/base.exe --nodes 200000

Bounds default to [0, 5] Elo at alpha=beta=0.05.
"""

import argparse
import math
import threading
import time

import chess
import chess.engine

# Short, roughly balanced openings (UCI) for game variety.
BOOK = [
    "e2e4 e7e5", "e2e4 c7c5", "e2e4 e7e6", "e2e4 c7c6", "e2e4 d7d5",
    "d2d4 d7d5", "d2d4 g8f6", "d2d4 d7d6", "c2c4 e7e5", "c2c4 g8f6",
    "g1f3 d7d5", "g1f3 g8f6", "e2e4 e7e5 g1f3 b8c6", "e2e4 e7e5 g1f3 b8c6 f1b5",
    "d2d4 d7d5 c2c4 e7e6", "d2d4 d7d5 c2c4 c7c6", "d2d4 g8f6 c2c4 g7g6",
    "e2e4 c7c5 g1f3 d7d6", "e2e4 e7e6 d2d4 d7d5", "c2c4 e7e6 g1f3 d7d5",
]


def expected_score(elo):
    return 1.0 / (1.0 + 10 ** (-elo / 400.0))


def gsprt_llr(W, D, L, elo0, elo1):
    n = W + D + L
    if n == 0 or W + L == 0:
        return 0.0
    score = (W + 0.5 * D) / n
    if score in (0.0, 1.0):
        return 0.0
    var = (W * (1 - score) ** 2 + D * (0.5 - score) ** 2 + L * score ** 2) / n
    if var <= 0:
        return 0.0
    s0, s1 = expected_score(elo0), expected_score(elo1)
    # GSPRT LLR for a (near-)normal score model: (mu1-mu0)/var * (sum_x - n*mid).
    # `var` is the per-game variance, so there is no extra factor of n here.
    return (s1 - s0) * ((W + 0.5 * D) - n * (s0 + s1) / 2.0) / var


def elo_with_ci(W, D, L):
    n = W + D + L
    if n == 0:
        return 0.0, 0.0
    score = (W + 0.5 * D) / n
    if score <= 0 or score >= 1:
        return (800.0 if score >= 1 else -800.0), 0.0
    var = (W * (1 - score) ** 2 + D * (0.5 - score) ** 2 + L * score ** 2) / n
    se = math.sqrt(var / n)
    elo = -400 * math.log10(1 / score - 1)
    # delta-method CI on Elo
    d = (400 / math.log(10)) / (score * (1 - score))
    return elo, 1.96 * se * d


def build_limit(args, wc, bc):
    if args.nodes:
        return chess.engine.Limit(nodes=args.nodes)
    if args.movetime:
        return chess.engine.Limit(time=args.movetime / 1000.0)
    return chess.engine.Limit(white_clock=wc, black_clock=bc,
                              white_inc=args.inc, black_inc=args.inc)


def play_game(a, b, opening, a_white, args):
    board = chess.Board()
    for uci in opening.split():
        board.push(chess.Move.from_uci(uci))
    wc = bc = args.base
    while not board.is_game_over(claim_draw=True) and board.fullmove_number <= 200:
        eng = a if (board.turn == chess.WHITE) == a_white else b
        t0 = time.time()
        try:
            res = eng.play(board, build_limit(args, wc, bc))
        except chess.engine.EngineError:
            # treat engine failure as a loss for that side
            return "0-1" if board.turn == chess.WHITE else "1-0"
        elapsed = time.time() - t0
        if not args.nodes and not args.movetime:
            if board.turn == chess.WHITE:
                wc -= elapsed
                if wc < 0: return "0-1"
                wc += args.inc
            else:
                bc -= elapsed
                if bc < 0: return "1-0"
                bc += args.inc
        if res.move is None:
            break
        board.push(res.move)
    if board.is_game_over(claim_draw=True):
        return board.result(claim_draw=True)
    return "1/2-1/2"


class Shared:
    def __init__(self):
        self.lock = threading.Lock()
        self.W = self.D = self.L = 0
        self.gameno = 0
        self.done = False
        self.verdict = None


def worker(args, shared, la, lb):
    a = chess.engine.SimpleEngine.popen_uci(args.a)
    b = chess.engine.SimpleEngine.popen_uci(args.b)
    ta = args.threadsa or args.threads
    tb = args.threadsb or args.threads
    for e, tc, ev in ((a, ta, args.neta), (b, tb, args.netb)):
        cfg = {"Hash": args.hash}
        if tc:
            cfg["Threads"] = tc
        if ev:
            cfg["EvalFile"] = ev
        try:
            e.configure(cfg)
        except Exception:
            pass
    try:
        while True:
            with shared.lock:
                if shared.done:
                    break
                g = shared.gameno
                shared.gameno += 1
            opening = BOOK[(g // 2) % len(BOOK)]
            a_white = (g % 2 == 0)
            res = play_game(a, b, opening, a_white, args)

            with shared.lock:
                if res == "1/2-1/2":
                    shared.D += 1
                elif (res == "1-0") == a_white:
                    shared.W += 1
                else:
                    shared.L += 1
                W, D, L = shared.W, shared.D, shared.L
                n = W + D + L
                llr = gsprt_llr(W, D, L, args.elo0, args.elo1)
                elo, ci = elo_with_ci(W, D, L)
                print(f"  g{n:>4}  +{W} ={D} -{L}  LLR {llr:+.2f} "
                      f"[{la:.2f},{lb:.2f}]  Elo {elo:+.0f} +/-{ci:.0f}", end="\r")
                if llr >= lb and not shared.done:
                    shared.done, shared.verdict = True, "H1 accepted: A is stronger"
                elif llr <= la and not shared.done:
                    shared.done, shared.verdict = True, "H0 accepted: not an improvement"
                elif n >= args.maxgames and not shared.done:
                    shared.done, shared.verdict = True, "max games reached (inconclusive)"
                if shared.done:
                    break
    finally:
        a.quit()
        b.quit()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--elo0", type=float, default=0.0)
    ap.add_argument("--elo1", type=float, default=5.0)
    ap.add_argument("--alpha", type=float, default=0.05)
    ap.add_argument("--beta", type=float, default=0.05)
    ap.add_argument("--tc", default="5+0.05", help="base+inc seconds, e.g. 8+0.08")
    ap.add_argument("--movetime", type=int, default=0, help="ms/move (overrides --tc)")
    ap.add_argument("--nodes", type=int, default=0, help="nodes/move (overrides --tc)")
    ap.add_argument("--concurrency", type=int, default=1)
    ap.add_argument("--maxgames", type=int, default=4000)
    ap.add_argument("--hash", type=int, default=64)
    ap.add_argument("--threads", type=int, default=0, help="engine Threads (both)")
    ap.add_argument("--threadsa", type=int, default=0, help="engine A Threads (overrides --threads)")
    ap.add_argument("--threadsb", type=int, default=0, help="engine B Threads (overrides --threads)")
    ap.add_argument("--neta", default="", help="EvalFile (NNUE) for engine A")
    ap.add_argument("--netb", default="", help="EvalFile (NNUE) for engine B")
    args = ap.parse_args()
    args.base = float(args.tc.split("+")[0])
    args.inc = float(args.tc.split("+")[1]) if "+" in args.tc else 0.0

    la = math.log(args.beta / (1 - args.alpha))
    lb = math.log((1 - args.beta) / args.alpha)

    tcdesc = (f"{args.nodes} nodes/move" if args.nodes else
              f"{args.movetime}ms/move" if args.movetime else f"{args.tc}s")
    print(f"SPRT  H0:{args.elo0} vs H1:{args.elo1} Elo  (a={args.alpha}, b={args.beta})")
    print(f"A={args.a}\nB={args.b}\nTC={tcdesc}  concurrency={args.concurrency}\n")

    shared = Shared()
    t0 = time.time()
    threads = [threading.Thread(target=worker, args=(args, shared, la, lb))
               for _ in range(max(1, args.concurrency))]
    for t in threads: t.start()
    for t in threads: t.join()

    W, D, L = shared.W, shared.D, shared.L
    elo, ci = elo_with_ci(W, D, L)
    print(f"\n\n{shared.verdict}")
    print(f"+{W} ={D} -{L}   Elo {elo:+.1f} +/-{ci:.0f}   "
          f"LLR {gsprt_llr(W, D, L, args.elo0, args.elo1):+.2f}   ({time.time()-t0:.0f}s)")


if __name__ == "__main__":
    main()
