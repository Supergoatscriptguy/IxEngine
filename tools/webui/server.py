"""Browser UI to play against IxEngine.

The web front-end (index.html) was generated with AI.

Setup:
    pip install flask python-chess
Run:
    python tools/webui/server.py            # then open http://127.0.0.1:5000
    python tools/webui/server.py --engine /path/to/ixchess-engine.exe --port 5000
"""

import argparse
import atexit
import threading
from pathlib import Path

import chess
import chess.engine
from flask import Flask, jsonify, request, send_from_directory

ROOT = Path(__file__).resolve().parents[2]          # engine / repo root
DEFAULT_ENGINE = ROOT / "bin" / "ixchess-engine.exe"
HERE = Path(__file__).resolve().parent

app = Flask(__name__)
lock = threading.Lock()
state = {
    "board": chess.Board(),
    "engine": None,
    "human": chess.WHITE,
    "movetime": 1000,
    "last": None,
}


def engine():
    if state["engine"] is None:
        state["engine"] = chess.engine.SimpleEngine.popen_uci(str(app.config["ENGINE_PATH"]))
    return state["engine"]


@atexit.register
def _shutdown():
    if state["engine"] is not None:
        try:
            state["engine"].quit()
        except Exception:
            pass


def engine_reply():
    """Let the engine move; return its move + eval for the response."""
    b = state["board"]
    res = engine().play(
        b, chess.engine.Limit(time=state["movetime"] / 1000),
        info=chess.engine.INFO_SCORE,
    )
    san = b.san(res.move)
    b.push(res.move)
    state["last"] = res.move
    sc = res.info.get("score")
    cp = sc.white().score(mate_score=100000) if sc is not None else None
    return {"engineMove": res.move.uci(), "engineSan": san, "evalCp": cp}


def snapshot(extra=None):
    b = state["board"]
    over = b.is_game_over(claim_draw=True)
    out = {
        "fen": b.fen(),
        "turn": "white" if b.turn else "black",
        "human": "white" if state["human"] == chess.WHITE else "black",
        "legal": [m.uci() for m in b.legal_moves],
        "last": state["last"].uci() if state["last"] else None,
        "check": b.is_check(),
        "over": over,
        "result": b.result(claim_draw=True) if over else None,
        "movetime": state["movetime"],
    }
    if extra:
        out.update(extra)
    return out


@app.route("/")
def index():
    return send_from_directory(HERE, "index.html")


@app.route("/state")
def get_state():
    with lock:
        return jsonify(snapshot())


@app.route("/new", methods=["POST"])
def new_game():
    data = request.get_json(force=True) or {}
    with lock:
        state["board"] = chess.Board()
        state["human"] = chess.WHITE if data.get("color", "white") == "white" else chess.BLACK
        state["movetime"] = max(50, int(data.get("movetime", 1000)))
        state["last"] = None
        extra = engine_reply() if state["board"].turn != state["human"] else None
        return jsonify(snapshot(extra))


@app.route("/move", methods=["POST"])
def human_move():
    data = request.get_json(force=True)
    with lock:
        b = state["board"]
        try:
            mv = chess.Move.from_uci(data.get("uci", ""))
        except ValueError:
            return jsonify({"error": "unparseable move"}), 400
        if mv not in b.legal_moves:
            return jsonify({"error": "illegal move"}), 400
        b.push(mv)
        state["last"] = mv
        extra = None
        if not b.is_game_over(claim_draw=True):
            extra = engine_reply()
        return jsonify(snapshot(extra))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=str(DEFAULT_ENGINE))
    ap.add_argument("--port", type=int, default=5000)
    args = ap.parse_args()
    app.config["ENGINE_PATH"] = args.engine
    print(f"IxEngine web UI  ->  http://127.0.0.1:{args.port}")
    app.run(host="127.0.0.1", port=args.port, debug=False, threaded=True)
