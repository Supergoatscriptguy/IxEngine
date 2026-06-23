#pragma once
// Core types, enums and bit-manipulation helpers for the Ixchess engine.
//
// Board convention: Little-Endian Rank-File (LERF) mapping.
//   a1 = 0, b1 = 1, ... h1 = 7, a2 = 8, ... h8 = 63.
//   file_of(sq) = sq & 7,  rank_of(sq) = sq >> 3.

#include <cstdint>
#include <string>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace ix {

using U64 = uint64_t;
using Bitboard = uint64_t;

constexpr int MAX_MOVES = 256;
constexpr int MAX_PLY = 128;

enum Color : int { WHITE, BLACK, COLOR_NB = 2 };
inline Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int {
    PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    NO_PIECE_TYPE = 6, PIECE_TYPE_NB = 6
};

// Piece = (color << 3) | type. Leaves a gap (6,7) but makes color/type
// extraction a shift/mask. NO_PIECE sits at 14.
enum Piece : int {
    W_PAWN = 0, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = 8, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    NO_PIECE = 14, PIECE_NB = 16
};

inline Piece make_piece(Color c, PieceType pt) { return Piece((c << 3) | pt); }
inline PieceType type_of(Piece p) { return PieceType(p & 7); }
inline Color color_of(Piece p) { return Color(p >> 3); }

enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    SQ_NONE = 64, SQUARE_NB = 64
};

enum File : int { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H };
enum Rank : int { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8 };

inline File file_of(Square s) { return File(s & 7); }
inline Rank rank_of(Square s) { return Rank(s >> 3); }
inline Square make_square(File f, Rank r) { return Square((r << 3) | f); }
inline Square flip_rank(Square s) { return Square(s ^ 56); }   // vertical mirror

// Manhattan-ish distances
inline int rank_distance(Square a, Square b) {
    int x = int(rank_of(a)) - int(rank_of(b));
    return x < 0 ? -x : x;
}
inline int file_distance(Square a, Square b) {
    int x = int(file_of(a)) - int(file_of(b));
    return x < 0 ? -x : x;
}
inline int square_distance(Square a, Square b) {
    int rd = rank_distance(a, b), fd = file_distance(a, b);
    return rd > fd ? rd : fd;   // Chebyshev distance
}

// Relative rank from a color's perspective (RANK_8 is promotion for both).
inline Rank relative_rank(Color c, Square s) {
    return Rank(int(rank_of(s)) ^ (c * 7));
}

enum CastlingRight : int {
    NO_CASTLING = 0,
    WHITE_OO = 1, WHITE_OOO = 2,
    BLACK_OO = 4, BLACK_OOO = 8,
    ANY_CASTLING = 15
};

// Scores in centipawns. Mate scores count plies from the root.
constexpr int VALUE_ZERO = 0;
constexpr int VALUE_DRAW = 0;
constexpr int VALUE_INFINITE = 32001;
constexpr int VALUE_NONE = 32002;
constexpr int VALUE_MATE = 32000;
constexpr int VALUE_MATE_IN_MAX_PLY = VALUE_MATE - MAX_PLY;
constexpr int VALUE_MATED_IN_MAX_PLY = -VALUE_MATE_IN_MAX_PLY;

inline int mate_in(int ply) { return VALUE_MATE - ply; }
inline int mated_in(int ply) { return -VALUE_MATE + ply; }

// Move packed into 16 bits: from (0-5), to (6-11), flags (12-15).
using Move = uint16_t;
constexpr Move MOVE_NONE = 0;   // a1a1, never a legal move
constexpr Move MOVE_NULL = 65;  // a1b1, used as null-move sentinel

enum MoveFlag : int {
    FLAG_QUIET = 0,
    FLAG_DOUBLE_PUSH = 1,
    FLAG_KING_CASTLE = 2,
    FLAG_QUEEN_CASTLE = 3,
    FLAG_CAPTURE = 4,
    FLAG_EP_CAPTURE = 5,
    FLAG_PROMO_N = 8,
    FLAG_PROMO_B = 9,
    FLAG_PROMO_R = 10,
    FLAG_PROMO_Q = 11,
    FLAG_PROMO_N_CAP = 12,
    FLAG_PROMO_B_CAP = 13,
    FLAG_PROMO_R_CAP = 14,
    FLAG_PROMO_Q_CAP = 15
};

inline Move make_move(Square from, Square to, int flag = FLAG_QUIET) {
    return Move(from | (to << 6) | (flag << 12));
}
inline Square from_sq(Move m) { return Square(m & 0x3F); }
inline Square to_sq(Move m) { return Square((m >> 6) & 0x3F); }
inline int move_flag(Move m) { return (m >> 12) & 0xF; }

inline bool is_promotion(Move m) { return move_flag(m) & 8; }
inline bool is_capture(Move m) { return move_flag(m) & 4; }
inline bool is_ep(Move m) { return move_flag(m) == FLAG_EP_CAPTURE; }
inline bool is_castle(Move m) {
    int f = move_flag(m);
    return f == FLAG_KING_CASTLE || f == FLAG_QUEEN_CASTLE;
}
// Promotion piece type (only valid when is_promotion()).
inline PieceType promotion_type(Move m) {
    return PieceType(KNIGHT + (move_flag(m) & 3));
}

#if defined(_MSC_VER)
inline int popcount(U64 b) { return (int)__popcnt64(b); }
inline Square lsb(U64 b) { unsigned long i; _BitScanForward64(&i, b); return Square(i); }
inline Square msb(U64 b) { unsigned long i; _BitScanReverse64(&i, b); return Square(i); }
#else
inline int popcount(U64 b) { return __builtin_popcountll(b); }
inline Square lsb(U64 b) { return Square(__builtin_ctzll(b)); }
inline Square msb(U64 b) { return Square(63 ^ __builtin_clzll(b)); }
#endif

inline Square pop_lsb(U64& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}
inline bool more_than_one(U64 b) { return b & (b - 1); }

} // namespace ix
