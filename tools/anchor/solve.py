"""Fixed-anchor performance rating: hold every opponent at its known CCRL 40/40
rating, solve for the ONE unknown (IxEngine), report a 95% bootstrap CI.

This is the same idea Ordo's multi-anchor mode implements: the opponents are not
re-estimated from the games, they are pinned to their published ratings, and we
find the single IxEngine rating that makes expected score == actual score under
the logistic (Elo) model. Draws count as half a point, as in standard Elo.

Pure-Python, no external rating binary required. Importable, or run standalone on
a results file:

  python tools/anchor/solve.py results.json        # [{"opp": 3050, "score": 0.5}, ...]
"""

import json
import random
import sys


def expected(r, opp):
    """Logistic expected score for a player rated r against opponent rated opp."""
    return 1.0 / (1.0 + 10 ** ((opp - r) / 400.0))


def solve_rating(games, lo=200.0, hi=4200.0, iters=200):
    """games: list of (opp_rating, score in [0,1]). Returns the rating r such that
    sum(expected(r, opp)) == sum(score), found by bisection. Monotone => unique."""
    target = sum(s for _, s in games)
    n = len(games)
    if target <= 0:
        return lo            # scored 0% — rating is below the floor
    if target >= n:
        return hi            # scored 100% — rating is above the ceiling
    for _ in range(iters):
        mid = 0.5 * (lo + hi)
        e = sum(expected(mid, o) for o, _ in games)
        if e < target:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


def bootstrap_ci(games, b=3000, seed=20260624, lo_q=0.025, hi_q=0.975):
    """Percentile bootstrap over games. Returns (point, lo, hi, saturated_frac)."""
    point = solve_rating(games)
    rng = random.Random(seed)
    n = len(games)
    samples = []
    saturated = 0
    for _ in range(b):
        resample = [games[rng.randrange(n)] for _ in range(n)]
        r = solve_rating(resample)
        if r <= 201.0 or r >= 4199.0:
            saturated += 1
        samples.append(r)
    samples.sort()
    lo = samples[int(lo_q * b)]
    hi = samples[min(int(hi_q * b), b - 1)]
    return point, lo, hi, saturated / b


def per_opponent(games):
    """games: list of (opp_rating, score). Returns {opp_rating: (w, d, l)}."""
    tab = {}
    for opp, s in games:
        w, d, l = tab.get(opp, (0, 0, 0))
        if s >= 0.99:
            w += 1
        elif s <= 0.01:
            l += 1
        else:
            d += 1
        tab[opp] = (w, d, l)
    return tab


def summary(games):
    point, lo, hi, sat = bootstrap_ci(games)
    n = len(games)
    score = sum(s for _, s in games) / n if n else 0.0
    return {
        "rating": round(point),
        "ci_lo": round(lo),
        "ci_hi": round(hi),
        "ci_half": round((hi - lo) / 2),
        "score_pct": round(100 * score, 1),
        "games": n,
        "saturated_frac": round(sat, 3),
    }


if __name__ == "__main__":
    with open(sys.argv[1]) as f:
        raw = json.load(f)
    games = [(g["opp"], g["score"]) for g in raw]
    s = summary(games)
    print(f"IxEngine (CCRL-anchored): {s['rating']}  "
          f"95% CI [{s['ci_lo']}, {s['ci_hi']}]  (+/-{s['ci_half']})")
    print(f"score {s['score_pct']}%  over {s['games']} games")
    for opp, (w, d, l) in sorted(per_opponent(games).items()):
        print(f"  vs {opp}:  +{w} ={d} -{l}")
