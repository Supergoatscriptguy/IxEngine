#include "perft.h"
#include "movegen.h"
#include <iostream>

namespace ix {

// True if the move just played (now it is ~mover's turn) left the mover's
// own king in check.
static inline bool move_was_legal(const Position& pos) {
    Color mover = ~pos.side_to_move();
    return !pos.is_attacked(pos.king_sq(mover), pos.side_to_move());
}

uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;

    Move moves[MAX_MOVES];
    int n = generate(pos, moves, GEN_ALL);
    uint64_t nodes = 0;

    for (int i = 0; i < n; ++i) {
        pos.do_move(moves[i]);
        if (move_was_legal(pos))
            nodes += (depth == 1) ? 1 : perft(pos, depth - 1);
        pos.undo_move(moves[i]);
    }
    return nodes;
}

static std::string move_str(Move m) {
    std::string s;
    s += char('a' + file_of(from_sq(m)));
    s += char('1' + rank_of(from_sq(m)));
    s += char('a' + file_of(to_sq(m)));
    s += char('1' + rank_of(to_sq(m)));
    if (is_promotion(m)) s += "nbrq"[move_flag(m) & 3];
    return s;
}

void perft_divide(Position& pos, int depth) {
    Move moves[MAX_MOVES];
    int n = generate(pos, moves, GEN_ALL);
    uint64_t total = 0;

    for (int i = 0; i < n; ++i) {
        pos.do_move(moves[i]);
        if (move_was_legal(pos)) {
            uint64_t cnt = (depth == 1) ? 1 : perft(pos, depth - 1);
            total += cnt;
            std::cout << move_str(moves[i]) << ": " << cnt << "\n";
        }
        pos.undo_move(moves[i]);
    }
    std::cout << "\nNodes searched: " << total << "\n";
}

} // namespace ix
