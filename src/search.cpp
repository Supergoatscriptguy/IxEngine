#include "search.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace ix {

std::string move_to_uci(Move m) {
    if (m == MOVE_NONE) return "0000";
    Square f = from_sq(m), t = to_sq(m);
    std::string s;
    s += char('a' + file_of(f));
    s += char('1' + rank_of(f));
    s += char('a' + file_of(t));
    s += char('1' + rank_of(t));
    if (is_promotion(m)) s += "nbrq"[move_flag(m) & 3];
    return s;
}

namespace Search {

std::atomic<bool> stopFlag{false};
int moveOverhead = 25;

namespace {

using Clock = std::chrono::steady_clock;

struct SearchStack {
    int ply;
    Move currentMove;
    Move excludedMove;
    Move killers[2];
    int staticEval;
    int moveCount;
    Move pv[MAX_PLY + 1];
};

// One independent searcher. Lazy SMP runs several of these over a shared TT;
// per-thread history naturally diversifies what each explores.
struct Thread {
    Position pos;
    SearchStack stack[MAX_PLY + 16];
    int history[COLOR_NB][SQUARE_NB][SQUARE_NB];
    int64_t nodes = 0;
    int selDepth = 0;
    Move rootBestMove = MOVE_NONE;
    int completedDepth = 0;
    int rootScore = 0;
    int id = 0;
    std::thread th;

    void clear_tables() { std::memset(history, 0, sizeof(history)); }
    void search();
    int negamax(SearchStack* ss, int alpha, int beta, int depth, bool cutNode);
    int qsearch(SearchStack* ss, int alpha, int beta);
    void score_moves(Move* moves, int n, int* scores, Move ttMove, const SearchStack* ss);
    bool root_legal(Move m);
    void check_time();
    bool move_was_legal() const {
        Color mover = ~pos.side_to_move();
        return !pos.is_attacked(pos.king_sq(mover), pos.side_to_move());
    }
};

std::vector<std::unique_ptr<Thread>> Threads;
int numThreads = 1;

Position* uciRootPos = nullptr;
Limits limits;
Clock::time_point startTime;
int64_t optimumTime = 0, maximumTime = 0;
bool useTimeLimit = false;
bool g_silent = false;   // suppress info output (data generation)

int LMRTable[64][64];

constexpr int SCORE_TT       = 1 << 28;
constexpr int SCORE_GOOD_CAP = 1 << 24;
constexpr int SCORE_KILLER0  = (1 << 23) + 1;
constexpr int SCORE_KILLER1  = (1 << 23);
constexpr int SCORE_QUIET    = 0;
constexpr int SCORE_BAD_CAP  = -(1 << 24);

const int PieceVal[7] = { 100, 320, 330, 500, 950, 20000, 0 };

inline int elapsed_ms() {
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - startTime).count());
}
int64_t total_nodes() {
    int64_t n = 0;
    for (auto& t : Threads) n += t->nodes;
    return n;
}

inline int value_to_tt(int v, int ply) {
    if (v >= VALUE_MATE_IN_MAX_PLY) return v + ply;
    if (v <= VALUE_MATED_IN_MAX_PLY) return v - ply;
    return v;
}
inline int value_from_tt(int v, int ply) {
    if (v == VALUE_NONE) return VALUE_NONE;
    if (v >= VALUE_MATE_IN_MAX_PLY) return v - ply;
    if (v <= VALUE_MATED_IN_MAX_PLY) return v + ply;
    return v;
}

int reduction(int depth, int moveCount, bool improving, bool pvNode) {
    int d = std::min(depth, 63), m = std::min(moveCount, 63);
    int r = LMRTable[d][m];
    if (pvNode) r -= 1;
    if (!improving) r += 1;
    return r < 0 ? 0 : r;
}

void update_history(int& h, int bonus) {
    h += bonus - h * std::abs(bonus) / 16384;
}

void pick_move(Move* moves, int* scores, int n, int idx) {
    int best = idx;
    for (int j = idx + 1; j < n; ++j)
        if (scores[j] > scores[best]) best = j;
    std::swap(moves[idx], moves[best]);
    std::swap(scores[idx], scores[best]);
}

void Thread::check_time() {
    if ((nodes & 2047) != 0) return;
    if (limits.nodes && total_nodes() >= limits.nodes) { stopFlag = true; return; }
    if (useTimeLimit && elapsed_ms() >= maximumTime) stopFlag = true;
}

void Thread::score_moves(Move* moves, int n, int* scores, Move ttMove, const SearchStack* ss) {
    Color us = pos.side_to_move();
    for (int i = 0; i < n; ++i) {
        Move m = moves[i];
        if (m == ttMove) { scores[i] = SCORE_TT; continue; }
        if (is_capture(m) || is_promotion(m)) {
            int victim = is_ep(m) ? PAWN
                       : (is_capture(m) ? type_of(pos.piece_on(to_sq(m))) : NO_PIECE_TYPE);
            int attacker = type_of(pos.piece_on(from_sq(m)));
            int mvvlva = (victim == NO_PIECE_TYPE ? 0 : PieceVal[victim] * 16) - attacker;
            if (is_promotion(m)) mvvlva += PieceVal[promotion_type(m)] * 16;
            bool good = pos.see_ge(m, -20);
            scores[i] = (good ? SCORE_GOOD_CAP : SCORE_BAD_CAP) + mvvlva;
        } else if (m == ss->killers[0]) {
            scores[i] = SCORE_KILLER0;
        } else if (m == ss->killers[1]) {
            scores[i] = SCORE_KILLER1;
        } else {
            scores[i] = SCORE_QUIET + history[us][from_sq(m)][to_sq(m)];
        }
    }
}

int Thread::qsearch(SearchStack* ss, int alpha, int beta) {
    check_time();
    if (stopFlag) return 0;

    ++nodes;
    bool pvNode = (beta - alpha) > 1;
    if (ss->ply > selDepth) selDepth = ss->ply;

    if (pos.is_draw()) return VALUE_DRAW;
    if (ss->ply >= MAX_PLY) return pos.in_check() ? VALUE_DRAW : evaluate(pos);

    bool inCheck = pos.in_check();

    bool ttHit;
    TTEntry* tte = TT.probe(pos.key(), ttHit);
    Move ttMove = ttHit ? tte->move() : MOVE_NONE;
    int ttValue = ttHit ? value_from_tt(tte->value(), ss->ply) : VALUE_NONE;

    if (!pvNode && ttHit && ttValue != VALUE_NONE
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    int bestScore, futilityBase;
    int rawEval = VALUE_NONE;
    if (inCheck) {
        bestScore = -VALUE_INFINITE;
        futilityBase = -VALUE_INFINITE;
    } else {
        rawEval = (ttHit && tte->eval() != int(VALUE_NONE)) ? tte->eval() : evaluate(pos);
        bestScore = rawEval;
        if (ttHit && ttValue != VALUE_NONE
            && (tte->bound() & (ttValue > bestScore ? BOUND_LOWER : BOUND_UPPER)))
            bestScore = ttValue;
        if (bestScore >= beta) return bestScore;
        if (bestScore > alpha) alpha = bestScore;
        futilityBase = bestScore + 160;
    }

    Move moves[MAX_MOVES];
    int n = generate(pos, moves, inCheck ? GEN_ALL : GEN_CAPTURES);
    int scores[MAX_MOVES];
    score_moves(moves, n, scores, ttMove, ss);

    Move bestMove = MOVE_NONE;
    int legal = 0;
    (ss + 1)->ply = ss->ply + 1;

    for (int i = 0; i < n; ++i) {
        pick_move(moves, scores, n, i);
        Move m = moves[i];

        if (!inCheck) {
            if (futilityBase > -VALUE_INFINITE && !is_promotion(m)) {
                int victim = is_ep(m) ? PAWN : type_of(pos.piece_on(to_sq(m)));
                if (futilityBase + PieceVal[victim] <= alpha) continue;
            }
            if (!pos.see_ge(m, 0)) continue;
        }

        pos.do_move(m);
        if (!move_was_legal()) { pos.undo_move(m); continue; }
        ++legal;
        ss->currentMove = m;
        int score = -qsearch(ss + 1, -beta, -alpha);
        pos.undo_move(m);

        if (stopFlag) return 0;

        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                bestMove = m;
                alpha = score;
                if (score >= beta) break;
            }
        }
    }

    if (inCheck && legal == 0)
        return mated_in(ss->ply);

    Bound b = (bestScore >= beta) ? BOUND_LOWER : BOUND_UPPER;
    tte->save(pos.key(), value_to_tt(bestScore, ss->ply), b, 0, bestMove, rawEval, TT.generation());
    return bestScore;
}

int Thread::negamax(SearchStack* ss, int alpha, int beta, int depth, bool cutNode) {
    bool pvNode = (beta - alpha) > 1;
    bool root = (ss->ply == 0);

    if (depth <= 0)
        return qsearch(ss, alpha, beta);

    check_time();
    if (stopFlag) return 0;

    ++nodes;
    if (pvNode && ss->ply > selDepth) selDepth = ss->ply;

    if (!root) {
        if (pos.is_draw()) return VALUE_DRAW;
        if (ss->ply >= MAX_PLY) return pos.in_check() ? VALUE_DRAW : evaluate(pos);
        alpha = std::max(alpha, mated_in(ss->ply));
        beta = std::min(beta, mate_in(ss->ply + 1));
        if (alpha >= beta) return alpha;
    }

    ss->moveCount = 0;
    if (pvNode) ss->pv[0] = MOVE_NONE;
    (ss + 1)->ply = ss->ply + 1;
    (ss + 1)->excludedMove = MOVE_NONE;
    (ss + 1)->killers[0] = (ss + 1)->killers[1] = MOVE_NONE;

    bool inCheck = pos.in_check();
    Move excluded = ss->excludedMove;

    bool ttHit;
    TTEntry* tte = TT.probe(pos.key(), ttHit);
    Move ttMove = root ? rootBestMove : (ttHit ? tte->move() : MOVE_NONE);
    int ttValue = ttHit ? value_from_tt(tte->value(), ss->ply) : VALUE_NONE;
    int ttDepth = ttHit ? tte->depth() : -1;

    if (!pvNode && !excluded && ttHit && ttDepth >= depth && ttValue != VALUE_NONE
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    int eval;
    if (inCheck) {
        eval = ss->staticEval = VALUE_NONE;
    } else if (ttHit) {
        eval = ss->staticEval = (tte->eval() != int(VALUE_NONE)) ? tte->eval() : evaluate(pos);
        if (ttValue != VALUE_NONE
            && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = ttValue;
    } else {
        eval = ss->staticEval = evaluate(pos);
    }

    bool improving = false;
    if (!inCheck) {
        if (ss->ply >= 2 && (ss - 2)->staticEval != VALUE_NONE)
            improving = ss->staticEval > (ss - 2)->staticEval;
        else
            improving = true;
    }

    if (!pvNode && !inCheck && !excluded && std::abs(beta) < VALUE_MATE_IN_MAX_PLY) {
        if (depth <= 7 && eval - 85 * (depth - improving) >= beta)
            return eval;

        if (depth >= 3 && eval >= beta && (ss - 1)->currentMove != MOVE_NULL
            && pos.has_non_pawn_material(pos.side_to_move())) {
            int R = 3 + depth / 4 + std::min((eval - beta) / 200, 3);
            pos.do_null_move();
            ss->currentMove = MOVE_NULL;
            (ss + 1)->excludedMove = MOVE_NONE;
            int nullScore = -negamax(ss + 1, -beta, -beta + 1, depth - R, !cutNode);
            pos.undo_null_move();
            if (stopFlag) return 0;
            if (nullScore >= beta)
                return nullScore >= VALUE_MATE_IN_MAX_PLY ? beta : nullScore;
        }
    }

    if (depth >= 6 && !ttMove && !excluded)
        depth -= 1;

    Move moves[MAX_MOVES];
    int n = generate(pos, moves, GEN_ALL);
    int scores[MAX_MOVES];
    score_moves(moves, n, scores, ttMove, ss);

    int bestScore = -VALUE_INFINITE;
    Move bestMove = MOVE_NONE;
    int moveCount = 0;
    Move quietsTried[64];
    int quietCount = 0;
    Color us = pos.side_to_move();
    if (root) ss->pv[0] = MOVE_NONE;

    for (int i = 0; i < n; ++i) {
        pick_move(moves, scores, n, i);
        Move m = moves[i];
        if (m == excluded) continue;

        bool isQuiet = !is_capture(m) && !is_promotion(m);
        bool givesCheck = pos.gives_check(m);

        if (!root && bestScore > VALUE_MATED_IN_MAX_PLY && pos.has_non_pawn_material(us)) {
            if (isQuiet && !givesCheck && !inCheck) {
                int lmpLimit = (3 + depth * depth) / (improving ? 1 : 2);
                if (depth <= 8 && moveCount >= lmpLimit) continue;
                if (depth <= 6 && eval != int(VALUE_NONE) && eval + 120 + 90 * depth <= alpha) continue;
                if (depth <= 8 && !pos.see_ge(m, -25 * depth)) continue;
            } else if (!isQuiet && depth <= 6 && !givesCheck) {
                if (!pos.see_ge(m, -90 * depth)) continue;
            }
        }

        pos.do_move(m);
        if (!move_was_legal()) { pos.undo_move(m); continue; }

        ++moveCount;
        ss->moveCount = moveCount;
        ss->currentMove = m;
        if (isQuiet && quietCount < 64) quietsTried[quietCount++] = m;

        int extension = 0;
        if (givesCheck && (pvNode || pos.see_ge(m, -100)))
            extension = 1;
        int newDepth = depth - 1 + extension;

        int score;
        if (moveCount == 1) {
            score = -negamax(ss + 1, -beta, -alpha, newDepth, pvNode ? false : cutNode);
        } else {
            int r = 0;
            if (depth >= 3 && moveCount >= (pvNode ? 4 : 2) && !givesCheck && !inCheck) {
                r = reduction(depth, moveCount, improving, pvNode);
                if (!isQuiet) r = r > 1 ? r - 1 : 0;
                if (r > newDepth - 1) r = newDepth - 1;
                if (r < 0) r = 0;
            }
            score = -negamax(ss + 1, -alpha - 1, -alpha, newDepth - r, true);
            if (r > 0 && score > alpha)
                score = -negamax(ss + 1, -alpha - 1, -alpha, newDepth, !cutNode);
            if (pvNode && score > alpha && score < beta)
                score = -negamax(ss + 1, -beta, -alpha, newDepth, false);
        }

        pos.undo_move(m);
        if (stopFlag) return 0;

        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                bestMove = m;
                alpha = score;
                if (pvNode) {
                    ss->pv[0] = m;
                    int k = 0;
                    while ((ss + 1)->pv[k] != MOVE_NONE) { ss->pv[k + 1] = (ss + 1)->pv[k]; ++k; }
                    ss->pv[k + 1] = MOVE_NONE;
                }
                if (root) rootBestMove = m;
                if (score >= beta) {
                    if (isQuiet) {
                        if (ss->killers[0] != m) {
                            ss->killers[1] = ss->killers[0];
                            ss->killers[0] = m;
                        }
                        int bonus = std::min(depth * depth, 1200);
                        update_history(history[us][from_sq(m)][to_sq(m)], bonus);
                        for (int q = 0; q < quietCount; ++q) {
                            Move qm = quietsTried[q];
                            if (qm != m)
                                update_history(history[us][from_sq(qm)][to_sq(qm)], -bonus);
                        }
                    }
                    break;
                }
            }
        }
    }

    if (moveCount == 0)
        return excluded ? alpha : (inCheck ? mated_in(ss->ply) : VALUE_DRAW);

    if (!excluded) {
        Bound b = (bestScore >= beta) ? BOUND_LOWER
                : (pvNode && bestMove) ? BOUND_EXACT : BOUND_UPPER;
        tte->save(pos.key(), value_to_tt(bestScore, ss->ply), b, depth, bestMove,
                  inCheck ? int(VALUE_NONE) : ss->staticEval, TT.generation());
    }
    return bestScore;
}

bool Thread::root_legal(Move m) {
    pos.do_move(m);
    bool ok = move_was_legal();
    pos.undo_move(m);
    return ok;
}

std::string score_str(int v) {
    if (std::abs(v) >= VALUE_MATE_IN_MAX_PLY) {
        int mate = (v > 0 ? VALUE_MATE - v + 1 : -VALUE_MATE - v) / 2;
        return "mate " + std::to_string(mate);
    }
    return "cp " + std::to_string(v);
}

void print_info(int depth, int selDepth, int score, const SearchStack* ss) {
    if (g_silent) return;
    int t = elapsed_ms();
    int64_t nc = total_nodes();
    int64_t nps = t > 0 ? (nc * 1000 / t) : nc * 1000;
    std::cout << "info depth " << depth << " seldepth " << selDepth
              << " score " << score_str(score) << " nodes " << nc
              << " nps " << nps << " time " << t
              << " hashfull " << TT.hashfull() << " pv";
    for (int i = 0; i < MAX_PLY && ss->pv[i] != MOVE_NONE; ++i)
        std::cout << ' ' << move_to_uci(ss->pv[i]);
    std::cout << std::endl;
}

void Thread::search() {
    for (int i = 0; i < MAX_PLY + 16; ++i) {
        stack[i].ply = i;
        stack[i].currentMove = MOVE_NONE;
        stack[i].excludedMove = MOVE_NONE;
        stack[i].killers[0] = stack[i].killers[1] = MOVE_NONE;
        stack[i].staticEval = VALUE_NONE;
        stack[i].pv[0] = MOVE_NONE;
    }
    SearchStack* ss = &stack[2];
    ss->ply = 0;

    int maxDepth = limits.depth > 0 ? limits.depth : MAX_PLY - 1;
    int score = 0;
    Move lastBest = MOVE_NONE;

    for (int depth = 1; depth <= maxDepth; ++depth) {
        if (stopFlag && lastBest != MOVE_NONE) break;

        int delta = 18;
        int alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
        if (depth >= 4 && std::abs(score) < VALUE_MATE_IN_MAX_PLY) {
            alpha = std::max(score - delta, -VALUE_INFINITE);
            beta = std::min(score + delta, +VALUE_INFINITE);
        }

        int newScore;
        while (true) {
            newScore = negamax(ss, alpha, beta, depth, false);
            if (stopFlag) break;
            if (newScore <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(newScore - delta, -VALUE_INFINITE);
            } else if (newScore >= beta) {
                beta = std::min(newScore + delta, +VALUE_INFINITE);
            } else break;
            delta += delta / 3 + 5;
        }

        if (stopFlag && lastBest != MOVE_NONE) break;
        score = newScore;
        if (rootBestMove != MOVE_NONE) { lastBest = rootBestMove; completedDepth = depth; rootScore = score; }

        if (id == 0) print_info(depth, selDepth, score, ss);
        if (stopFlag) break;

        // Only the main thread enforces the soft time limit and stops the rest.
        if (id == 0 && useTimeLimit && elapsed_ms() >= optimumTime) { stopFlag = true; break; }
        if (std::abs(score) >= VALUE_MATE_IN_MAX_PLY && depth >= 6) { if (id == 0) stopFlag = true; break; }
    }

    rootBestMove = lastBest;
    if (id == 0) stopFlag = true;
}

void compute_time() {
    useTimeLimit = false;
    optimumTime = maximumTime = 0;
    Color us = uciRootPos->side_to_move();

    if (limits.infinite || limits.depth || limits.nodes) {
        if (limits.movetime == 0 && limits.time[us] == 0) return;
    }
    if (limits.movetime > 0) {
        useTimeLimit = true;
        int ded = std::min(moveOverhead, limits.movetime / 8);
        optimumTime = maximumTime = std::max(1, limits.movetime - ded);
    } else if (limits.time[us] > 0) {
        useTimeLimit = true;
        int t = std::max(1, limits.time[us] - moveOverhead);
        int inc = limits.inc[us];
        int mtg = limits.movestogo > 0 ? limits.movestogo : 40;
        int opt = t / mtg + inc * 3 / 4;
        optimumTime = std::min(opt, t);
        maximumTime = std::min(t, std::max(opt * 4, opt + 1));
        maximumTime = std::min<int64_t>(maximumTime, t);
    }
    if (limits.infinite) useTimeLimit = false;
}

std::thread coordinator;

void think() {
    startTime = Clock::now();
    stopFlag = false;
    compute_time();
    TT.new_search();

    for (auto& t : Threads) {
        t->pos = *uciRootPos;
        t->nodes = 0;
        t->selDepth = 0;
        t->rootBestMove = MOVE_NONE;
        t->completedDepth = 0;
    }

    for (size_t i = 1; i < Threads.size(); ++i)
        Threads[i]->th = std::thread(&Thread::search, Threads[i].get());

    Threads[0]->search();        // main thread searches here and prints
    stopFlag = true;

    for (size_t i = 1; i < Threads.size(); ++i)
        if (Threads[i]->th.joinable()) Threads[i]->th.join();

    Move best = Threads[0]->rootBestMove;
    if (best == MOVE_NONE) {     // ultra-short time: grab any legal move
        Move mv[MAX_MOVES];
        int n = generate(Threads[0]->pos, mv, GEN_ALL);
        for (int i = 0; i < n; ++i)
            if (Threads[0]->root_legal(mv[i])) { best = mv[i]; break; }
    }
    std::cout << "bestmove " << move_to_uci(best) << std::endl;
}

} // namespace

void set_threads(int n) {
    if (n < 1) n = 1;
    if (n > 256) n = 256;
    numThreads = n;
    Threads.clear();
    for (int i = 0; i < n; ++i) {
        Threads.push_back(std::make_unique<Thread>());
        Threads.back()->id = i;
        Threads.back()->clear_tables();
    }
}

void init() {
    for (int d = 1; d < 64; ++d)
        for (int m = 1; m < 64; ++m)
            LMRTable[d][m] = int(0.75 + std::log(double(d)) * std::log(double(m)) / 2.25);
    if (Threads.empty()) set_threads(1);
}

void clear() {
    for (auto& t : Threads) t->clear_tables();
    TT.clear();
}

void start(Position& pos, const Limits& lim) {
    wait();
    uciRootPos = &pos;
    limits = lim;
    stopFlag = false;
    coordinator = std::thread(think);
}

void stop() { stopFlag = true; }

void wait() {
    if (coordinator.joinable()) coordinator.join();
}

Move datagen_search(Position& pos, int64_t nodeLimit, int& outScore) {
    if (Threads.empty()) set_threads(1);
    uciRootPos = &pos;
    limits = Limits{};
    limits.nodes = nodeLimit;
    stopFlag = false;
    startTime = Clock::now();
    optimumTime = maximumTime = 0;
    useTimeLimit = false;
    TT.new_search();
    g_silent = true;

    Thread* t = Threads[0].get();
    t->pos = pos;
    t->nodes = 0;
    t->selDepth = 0;
    t->rootBestMove = MOVE_NONE;
    t->completedDepth = 0;
    t->rootScore = 0;
    t->search();

    g_silent = false;
    outScore = t->rootScore;
    return t->rootBestMove;
}

} // namespace Search
} // namespace ix
