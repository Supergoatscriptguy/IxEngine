// UCI front-end and entry point for the Ixchess engine.
#include "bitboard.h"
#include "zobrist.h"
#include "position.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "search.h"
#include "nnue.h"
#include "perft.h"

#include <iostream>
#include <sstream>
#include <string>
#include <cmath>
#include <chrono>
#include <fstream>
#include <random>
#include <vector>

using namespace ix;

namespace {

const char* ENGINE_NAME = "IxEngine 1.0";
const char* ENGINE_AUTHOR = "supergoatscriptguy";

bool limitStrength = false;
int uciElo = 2850;

inline bool legal_in(Position& pos, Move m) {
    pos.do_move(m);
    Color mover = ~pos.side_to_move();
    bool ok = !pos.is_attacked(pos.king_sq(mover), pos.side_to_move());
    pos.undo_move(m);
    return ok;
}

Move parse_move(Position& pos, const std::string& s) {
    Move mv[MAX_MOVES];
    int n = generate(pos, mv, GEN_ALL);
    for (int i = 0; i < n; ++i)
        if (move_to_uci(mv[i]) == s && legal_in(pos, mv[i]))
            return mv[i];
    return MOVE_NONE;
}

void cmd_position(Position& pos, std::istringstream& is) {
    std::string token;
    is >> token;

    if (token == "startpos") {
        pos.set_startpos();
        is >> token; // expect "moves" or end
    } else if (token == "fen") {
        std::string fen;
        while (is >> token && token != "moves")
            fen += token + " ";
        pos.set(fen);
        // token is now "moves" or the stream is exhausted
    } else {
        return;
    }

    if (token == "moves") {
        while (is >> token) {
            Move m = parse_move(pos, token);
            if (m == MOVE_NONE) break;
            pos.do_move(m);
        }
    }
}

int elo_depth_cap(int elo) {
    // Crude mapping for UCI_LimitStrength compatibility (default off).
    int d = (elo - 1000) / 130;
    if (d < 1) d = 1;
    if (d > 20) d = 20;
    return d;
}

void cmd_go(Position& pos, std::istringstream& is) {
    Search::Limits limits;
    std::string token;
    while (is >> token) {
        if (token == "wtime") is >> limits.time[WHITE];
        else if (token == "btime") is >> limits.time[BLACK];
        else if (token == "winc") is >> limits.inc[WHITE];
        else if (token == "binc") is >> limits.inc[BLACK];
        else if (token == "movestogo") is >> limits.movestogo;
        else if (token == "depth") is >> limits.depth;
        else if (token == "nodes") is >> limits.nodes;
        else if (token == "movetime") is >> limits.movetime;
        else if (token == "infinite") limits.infinite = true;
    }
    if (limitStrength) {
        int cap = elo_depth_cap(uciElo);
        if (limits.depth == 0 || limits.depth > cap) limits.depth = cap;
    }
    Search::start(pos, limits);
}

void cmd_setoption(std::istringstream& is) {
    Search::wait(); // never reconfigure while a search is running
    std::string token, name, value;
    is >> token; // "name"
    while (is >> token && token != "value") {
        if (!name.empty()) name += " ";
        name += token;
    }
    while (is >> token) {
        if (!value.empty()) value += " ";
        value += token;
    }

    if (name == "Hash") {
        TT.resize(std::stoi(value));
    } else if (name == "Clear Hash") {
        TT.clear();
    } else if (name == "Move Overhead") {
        Search::moveOverhead = std::stoi(value);
    } else if (name == "Threads") {
        Search::set_threads(std::stoi(value));
    } else if (name == "UCI_LimitStrength") {
        limitStrength = (value == "true");
    } else if (name == "UCI_Elo") {
        uciElo = std::stoi(value);
    } else if (name == "EvalFile") {
        bool ok = NNUE::load(value);
        std::cout << "info string NNUE " << (ok ? "loaded: " : "disabled (HCE): ")
                  << value << std::endl;
    }
    // Ponder accepted but ignored (no pondering).
}

// Self-play data generation: writes "<FEN> | <score_cp_white> | <wdl_white>"
// lines (the text format bullet's converter ingests). Quiet positions only.
void run_datagen(const std::string& outPath, int games, int64_t nodes, unsigned seed) {
    std::ofstream out(outPath);
    if (!out) { std::cerr << "datagen: cannot open " << outPath << "\n"; return; }
    std::mt19937 rng(seed);
    Search::set_threads(1);
    int64_t totalPos = 0;
    auto t0 = std::chrono::steady_clock::now();

    for (int g = 0; g < games; ++g) {
        Position pos;
        pos.set_startpos();

        // Random opening so games differ.
        bool aborted = false;
        int openPlies = 8 + int(rng() % 5);
        for (int k = 0; k < openPlies; ++k) {
            Move mv[MAX_MOVES];
            int n = generate(pos, mv, GEN_ALL);
            Move legal[MAX_MOVES]; int ln = 0;
            for (int i = 0; i < n; ++i) if (legal_in(pos, mv[i])) legal[ln++] = mv[i];
            if (ln == 0) { aborted = true; break; }
            pos.do_move(legal[rng() % ln]);
        }
        if (aborted) continue;

        std::vector<std::pair<std::string, int>> recs;
        double wdlWhite = 0.5;
        for (int ply = 0; ply < 320; ++ply) {
            Move mv[MAX_MOVES];
            int n = generate(pos, mv, GEN_ALL), ln = 0;
            for (int i = 0; i < n; ++i) if (legal_in(pos, mv[i])) ln++;
            if (ln == 0) {                       // checkmate or stalemate
                if (pos.in_check()) wdlWhite = (pos.side_to_move() == WHITE) ? 0.0 : 1.0;
                break;
            }
            if (pos.is_draw()) break;            // wdlWhite stays 0.5

            int score;
            Move bm = Search::datagen_search(pos, nodes, score);
            if (bm == MOVE_NONE) break;

            // Record quiet, non-extreme positions (the useful training signal).
            if (!pos.in_check() && !is_capture(bm) && !is_promotion(bm) && std::abs(score) < 2000) {
                int sw = (pos.side_to_move() == WHITE) ? score : -score;
                recs.emplace_back(pos.fen(), sw);
            }
            pos.do_move(bm);
        }

        for (auto& r : recs) { out << r.first << " | " << r.second << " | " << wdlWhite << "\n"; ++totalPos; }
        if ((g + 1) % 20 == 0) {
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::cerr << "game " << (g + 1) << "/" << games << "  positions " << totalPos
                      << "  (" << int(totalPos / (s + 1e-9)) << " pos/s)        \r";
        }
    }
    out.flush();
    std::cerr << "\ndatagen done: " << totalPos << " positions -> " << outPath << "\n";
}

void run_bench() {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    };
    TT.clear();
    auto t0 = std::chrono::steady_clock::now();
    int64_t total = 0;
    for (const char* f : fens) {
        Position pos;
        pos.set(f);
        Search::Limits lim;
        lim.depth = 12;
        Search::start(pos, lim);
        Search::wait();
        TT.clear();
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "bench complete in " << ms << " ms\n";
    (void)total;
}

} // namespace

int main(int argc, char** argv) {
    Bitboards::init();
    Zobrist::init();
    Eval::init();
    Search::init();
    TT.resize(64);

    Position pos;
    pos.set_startpos();

    // Allow `engine bench` / `engine perft N <fen>` from the command line.
    if (argc > 1) {
        std::string a1 = argv[1];
        if (a1 == "bench") { run_bench(); return 0; }
        if (a1 == "datagen") {
            std::string outp = argc > 2 ? argv[2] : "data.txt";
            int games = argc > 3 ? std::stoi(argv[3]) : 1000;
            int64_t nodes = argc > 4 ? std::stoll(argv[4]) : 5000;
            unsigned seed = argc > 5 ? (unsigned)std::stoul(argv[5]) : 1u;
            if (argc > 6) NNUE::load(argv[6]);   // optional net (bootstrap datagen)
            run_datagen(outp, games, nodes, seed);
            return 0;
        }
        if (a1 == "perft" && argc > 2) {
            int d = std::stoi(argv[2]);
            if (argc > 3) {
                std::string fen;
                for (int i = 3; i < argc; ++i) fen += std::string(argv[i]) + " ";
                pos.set(fen);
            }
            perft_divide(pos, d);
            return 0;
        }
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        // Strip a leading UTF-8 BOM and any stray CR (some shells add these).
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF
            && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::istringstream is(line);
        std::string cmd;
        is >> cmd;

        if (cmd == "uci") {
            std::cout << "id name " << ENGINE_NAME << "\n";
            std::cout << "id author " << ENGINE_AUTHOR << "\n";
            std::cout << "option name Hash type spin default 64 min 1 max 4096\n";
            std::cout << "option name Threads type spin default 1 min 1 max 256\n";
            std::cout << "option name Move Overhead type spin default 25 min 0 max 5000\n";
            std::cout << "option name Ponder type check default false\n";
            std::cout << "option name UCI_LimitStrength type check default false\n";
            std::cout << "option name UCI_Elo type spin default 2850 min 1320 max 3000\n";
            std::cout << "option name EvalFile type string default <empty>\n";
            std::cout << "uciok" << std::endl;
        } else if (cmd == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (cmd == "setoption") {
            cmd_setoption(is);
        } else if (cmd == "ucinewgame") {
            Search::wait();
            Search::clear();
        } else if (cmd == "position") {
            Search::wait();
            cmd_position(pos, is);
        } else if (cmd == "go") {
            cmd_go(pos, is);
        } else if (cmd == "stop") {
            Search::stop();
        } else if (cmd == "ponderhit") {
            // treat as a normal continuation (no pondering implemented)
        } else if (cmd == "quit" || cmd == "exit") {
            Search::stop();
            Search::wait();
            break;
        } else if (cmd == "d") {
            std::cout << pos.to_string();
        } else if (cmd == "eval") {
            std::cout << "eval " << evaluate(pos) << " cp (side to move)\n";
        } else if (cmd == "perft") {
            int d; if (is >> d) perft_divide(pos, d);
        } else if (cmd == "bench") {
            run_bench();
        }
    }

    Search::wait();
    return 0;
}
