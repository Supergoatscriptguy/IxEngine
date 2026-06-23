#pragma once
#include "bitboard.h"
#include <string>

namespace ix {

// Saved irreversible state, pushed on do_move / popped on undo_move.
struct StateInfo {
    int castlingRights;
    Square epSquare;
    int halfmoveClock;
    U64 key;
    Piece captured;
    Bitboard checkers; // pieces giving check in this position
};

class Position {
public:
    Position() { clear(); }

    void set(const std::string& fen);
    void set_startpos();
    std::string fen() const;

    // --- piece / occupancy queries ---
    Bitboard pieces() const { return byColor[WHITE] | byColor[BLACK]; }
    Bitboard pieces(Color c) const { return byColor[c]; }
    Bitboard pieces(PieceType pt) const { return byType[pt]; }
    Bitboard pieces(PieceType a, PieceType b) const { return byType[a] | byType[b]; }
    Bitboard pieces(Color c, PieceType pt) const { return byColor[c] & byType[pt]; }
    Bitboard pieces(Color c, PieceType a, PieceType b) const {
        return byColor[c] & (byType[a] | byType[b]);
    }
    Piece piece_on(Square s) const { return board[s]; }
    bool empty(Square s) const { return board[s] == NO_PIECE; }
    Square king_sq(Color c) const { return lsb(byColor[c] & byType[KING]); }
    int count(Color c, PieceType pt) const { return popcount(pieces(c, pt)); }

    Color side_to_move() const { return stm; }
    int castling_rights() const { return st->castlingRights; }
    Square ep_square() const { return st->epSquare; }
    int halfmove_clock() const { return st->halfmoveClock; }
    U64 key() const { return st->key; }
    Bitboard checkers() const { return st->checkers; }
    bool in_check() const { return st->checkers != 0; }
    int game_ply() const { return gamePly; }

    // --- attack detection ---
    Bitboard attackers_to(Square s, Bitboard occ) const;
    Bitboard attackers_to(Square s) const { return attackers_to(s, pieces()); }
    bool is_attacked(Square s, Color by) const;

    // --- make / unmake ---
    void do_move(Move m);
    void undo_move(Move m);
    void do_null_move();
    void undo_null_move();

    // Static Exchange Evaluation: is the capture/move worth at least `threshold`?
    bool see_ge(Move m, int threshold) const;

    bool is_repetition() const;
    bool is_draw() const; // 50-move or repetition or insufficient material
    bool has_non_pawn_material(Color c) const {
        return pieces(c, KNIGHT, BISHOP) | pieces(c, ROOK) | pieces(c, QUEEN);
    }

    // Phase for tapered eval (0 = endgame .. 24 = full midgame).
    int game_phase() const;

    bool is_pseudo_legal(Move m) const; // sanity-check a TT/killer move
    bool gives_check(Move m) const;

    std::string to_string() const; // ASCII board, for debugging

private:
    void clear();
    void put_piece(Piece pc, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    Bitboard compute_checkers() const;

    Bitboard byType[PIECE_TYPE_NB];
    Bitboard byColor[COLOR_NB];
    Piece board[SQUARE_NB];
    Color stm;
    int gamePly;

    StateInfo states[1024];
    StateInfo* st;

    // Full key history (from game start) for repetition detection.
    U64 history[2048];
    int historyCount;
};

} // namespace ix
