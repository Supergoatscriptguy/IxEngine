"""CCRL-anchored performance-rating rig for IxEngine  --  ONE command, re-runnable.

Plays IxEngine (NNUE on, 1 thread, fixed hash) as a gauntlet against a FIXED set
of open-source engines whose published CCRL 40/40 ratings are held as anchors,
then solves for IxEngine's rating on the CCRL scale (fixed-anchor MLE + bootstrap
CI -- see solve.py). Writes a timestamped report + PGN and appends history.csv so
you can compare across future upgrades.

  python tools/anchor/run_anchor.py                       # full run from anchors.json
  python tools/anchor/run_anchor.py --games 120 --tag gen3
  python tools/anchor/run_anchor.py --concurrency 8

This is a BLITZ-ANCHORED APPROXIMATION (default TC 15+0.15), NOT a true CCRL 40/40
number -- engine strength shifts with time control. It is a "where do I sit vs the
CCRL scale" locator, run under conditions kept as close to CCRL as is cheap:
single thread, fixed 64MB hash, ponder off, no tablebases, short generic book.
"""

import argparse
import json
import os
import threading
import time
from datetime import datetime

import chess
import chess.engine
import chess.pgn

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))   # engine/
RUNS = os.path.join(HERE, "runs")


def rpath(p):
    """Resolve a config path relative to the engine root, to absolute."""
    return p if os.path.isabs(p) else os.path.abspath(os.path.join(ROOT, p))


def load_book():
    with open(os.path.join(HERE, "book.json")) as f:
        return json.load(f)["openings"]


def open_engine(spec, common, hash_mb):
    """spec: {cmd, options}. Returns a configured SimpleEngine."""
    cmd = rpath(spec["cmd"])
    eng = chess.engine.SimpleEngine.popen_uci(cmd, cwd=os.path.dirname(cmd))
    opts = {"Hash": hash_mb}
    opts.update(common)
    opts.update(spec.get("options", {}))
    for k, v in opts.items():
        if k == "EvalFile" and isinstance(v, str) and v:
            v = rpath(v)
        try:
            eng.configure({k: v})           # one at a time: skip unsupported, keep the rest
        except Exception:
            pass
    return eng


def play_game(ix, opp, opening, ix_white, base, inc):
    """Returns (result_str, board). result_str is from White's POV ('1-0'/'0-1'/'1/2-1/2')."""
    board = chess.Board()
    for uci in opening.split():
        board.push(chess.Move.from_uci(uci))
    wc = bc = base
    while not board.is_game_over(claim_draw=True) and board.fullmove_number <= 200:
        eng = ix if (board.turn == chess.WHITE) == ix_white else opp
        limit = chess.engine.Limit(white_clock=wc, black_clock=bc,
                                   white_inc=inc, black_inc=inc)
        t0 = time.time()
        try:
            res = eng.play(board, limit)
        except chess.engine.EngineError:
            return ("0-1" if board.turn == chess.WHITE else "1-0"), board
        elapsed = time.time() - t0
        if board.turn == chess.WHITE:
            wc -= elapsed
            if wc < 0:
                return "0-1", board
            wc += inc
        else:
            bc -= elapsed
            if bc < 0:
                return "1-0", board
            bc += inc
        if res.move is None:
            break
        board.push(res.move)
    return board.result(claim_draw=True), board


class OppState:
    def __init__(self, opp):
        self.opp = opp
        self.lock = threading.Lock()
        self.next = 0
        self.w = self.d = self.l = 0     # from IxEngine's POV
        self.games = []                  # (opp_rating, score)
        self.pgns = []


def run_opponent(state, ixspec, common, hash_mb, book, n_games, base, inc, concurrency, ix_name):
    opp = state.opp

    def worker():
        ix = open_engine(ixspec, common, hash_mb)
        og = open_engine(opp, common, hash_mb)
        try:
            while True:
                with state.lock:
                    g = state.next
                    if g >= n_games:
                        break
                    state.next += 1
                ix_white = (g % 2 == 0)
                opening = book[(g // 2) % len(book)]
                result, board = play_game(ix, og, opening, ix_white, base, inc)
                if result == "1/2-1/2":
                    score = 0.5
                elif (result == "1-0") == ix_white:
                    score = 1.0
                else:
                    score = 0.0
                game = chess.pgn.Game.from_board(board)
                game.headers["Event"] = "IxEngine CCRL-anchor gauntlet"
                game.headers["White"] = ix_name if ix_white else opp["name"]
                game.headers["Black"] = opp["name"] if ix_white else ix_name
                game.headers["Result"] = result
                with state.lock:
                    if score == 1.0:
                        state.w += 1
                    elif score == 0.0:
                        state.l += 1
                    else:
                        state.d += 1
                    state.games.append((opp["ccrl_4040"], score))
                    state.pgns.append(str(game))
                    done = state.w + state.d + state.l
                    print(f"  {opp['name']:<22} +{state.w} ={state.d} -{state.l}"
                          f"  ({done}/{n_games})", end="\r")
        finally:
            ix.quit()
            og.quit()

    threads = [threading.Thread(target=worker) for _ in range(max(1, concurrency))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=os.path.join(HERE, "anchors.json"))
    ap.add_argument("--games", type=int, default=0, help="override games_total")
    ap.add_argument("--concurrency", type=int, default=0)
    ap.add_argument("--tc", default="", help="override, e.g. 15+0.15")
    ap.add_argument("--tag", default="", help="label for this run in history.csv")
    args = ap.parse_args()

    with open(args.config) as f:
        cfg = json.load(f)

    tc = args.tc or cfg.get("tc", "15+0.15")
    base = float(tc.split("+")[0])
    inc = float(tc.split("+")[1]) if "+" in tc else 0.0
    games_total = args.games or cfg.get("games_total", 400)
    concurrency = args.concurrency or cfg.get("concurrency", 6)
    hash_mb = cfg.get("hash", 64)
    common = cfg.get("common_options", {})
    ixspec = cfg["ixengine"]
    ix_name = ixspec.get("name", "IxEngine")
    opponents = cfg["opponents"]
    book = load_book()

    k = len(opponents)
    if k == 0:
        raise SystemExit("anchors.json has no opponents -- fill in the verified engines first.")
    per_opp = max(2, (games_total // k) // 2 * 2)   # even split, colour-balanced

    print(f"IxEngine CCRL-anchor gauntlet  ({ix_name})")
    print(f"TC {tc}  hash {hash_mb}MB  concurrency {concurrency}")
    print(f"{k} opponents x {per_opp} games = {k*per_opp} total\n")

    all_games = []
    states = []
    t0 = time.time()
    for opp in opponents:
        print(f"vs {opp['name']} (CCRL 40/40 {opp['ccrl_4040']})")
        st = OppState(opp)
        run_opponent(st, ixspec, common, hash_mb, book, per_opp, base, inc, concurrency, ix_name)
        states.append(st)
        all_games.extend(st.games)

    # ---- solve (import locally so solve.py stays standalone) ----
    import importlib.util
    spec = importlib.util.spec_from_file_location("solve", os.path.join(HERE, "solve.py"))
    solve = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(solve)
    s = solve.summary(all_games)
    gap = 3300 - s["rating"]
    dur = time.time() - t0

    os.makedirs(RUNS, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    tag = args.tag or "run"

    # ---- report ----
    lines = []
    lines.append(f"IxEngine CCRL-anchored performance rating  --  {stamp}  [{tag}]")
    lines.append("=" * 64)
    lines.append("BLITZ-ANCHORED APPROXIMATION (TC %s) -- NOT a true CCRL 40/40 number." % tc)
    lines.append("Conditions: NNUE on, 1 thread, %dMB hash, ponder off, no TB, <=6-ply book." % hash_mb)
    lines.append("")
    lines.append(f"  IxEngine:  {s['rating']}   95%% CI [{s['ci_lo']}, {s['ci_hi']}]  (+/-{s['ci_half']})")
    lines.append(f"  overall score: {s['score_pct']}%%  over {s['games']} games  ({dur/60:.0f} min)")
    lines.append(f"  gap to ~3300 (CCRL 40/40 top-100 territory): {gap:+d}")
    if s["saturated_frac"] > 0.02:
        lines.append(f"  WARNING: {s['saturated_frac']*100:.0f}%% of bootstrap samples hit a rating bound "
                     f"(bracket too one-sided -- add a closer opponent).")
    lines.append("")
    lines.append("Per-opponent (IxEngine W-D-L), anchors held FIXED:")
    for st in states:
        o = st.opp
        n = st.w + st.d + st.l
        sc = (st.w + 0.5 * st.d) / n * 100 if n else 0
        lines.append(f"  {o['name']:<24} CCRL {o['ccrl_4040']:>4}   "
                     f"+{st.w} ={st.d} -{st.l}   ({sc:.0f}%%)")
    report = "\n".join(lines).replace("%%", "%")

    with open(os.path.join(RUNS, f"{stamp}_{tag}.txt"), "w") as f:
        f.write(report + "\n")
    with open(os.path.join(RUNS, f"{stamp}_{tag}.pgn"), "w") as f:
        for st in states:
            for p in st.pgns:
                f.write(p + "\n\n")
    hist = os.path.join(HERE, "history.csv")
    new = not os.path.exists(hist)
    with open(hist, "a") as f:
        if new:
            f.write("timestamp,tag,rating,ci_lo,ci_hi,games,score_pct,tc\n")
        f.write(f"{stamp},{tag},{s['rating']},{s['ci_lo']},{s['ci_hi']},"
                f"{s['games']},{s['score_pct']},{tc}\n")

    print("\n" + report)
    print(f"\nwrote runs/{stamp}_{tag}.txt , .pgn  and appended history.csv")


if __name__ == "__main__":
    main()
