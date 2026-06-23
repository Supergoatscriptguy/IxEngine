#pragma once
// Bitboard constants, attack tables, and (fancy) magic-bitboard sliders.

#include "types.h"

namespace ix {

// File / rank masks
constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_B_BB = FILE_A_BB << 1;
constexpr Bitboard FILE_C_BB = FILE_A_BB << 2;
constexpr Bitboard FILE_D_BB = FILE_A_BB << 3;
constexpr Bitboard FILE_E_BB = FILE_A_BB << 4;
constexpr Bitboard FILE_F_BB = FILE_A_BB << 5;
constexpr Bitboard FILE_G_BB = FILE_A_BB << 6;
constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;

constexpr Bitboard RANK_1_BB = 0xFFULL;
constexpr Bitboard RANK_2_BB = RANK_1_BB << (8 * 1);
constexpr Bitboard RANK_3_BB = RANK_1_BB << (8 * 2);
constexpr Bitboard RANK_4_BB = RANK_1_BB << (8 * 3);
constexpr Bitboard RANK_5_BB = RANK_1_BB << (8 * 4);
constexpr Bitboard RANK_6_BB = RANK_1_BB << (8 * 5);
constexpr Bitboard RANK_7_BB = RANK_1_BB << (8 * 6);
constexpr Bitboard RANK_8_BB = RANK_1_BB << (8 * 7);

inline Bitboard square_bb(Square s) { return 1ULL << s; }
inline Bitboard file_bb(File f) { return FILE_A_BB << f; }
inline Bitboard rank_bb(Rank r) { return RANK_1_BB << (8 * r); }

inline bool test_bit(Bitboard b, Square s) { return b & square_bb(s); }

// Directional one-step shifts that mask off wraps.
template <int D> inline Bitboard shift(Bitboard b);
template <> inline Bitboard shift<8>(Bitboard b) { return b << 8; }            // North
template <> inline Bitboard shift<-8>(Bitboard b) { return b >> 8; }           // South
template <> inline Bitboard shift<1>(Bitboard b) { return (b & ~FILE_H_BB) << 1; }   // East
template <> inline Bitboard shift<-1>(Bitboard b) { return (b & ~FILE_A_BB) >> 1; }  // West
template <> inline Bitboard shift<9>(Bitboard b) { return (b & ~FILE_H_BB) << 9; }   // NE
template <> inline Bitboard shift<7>(Bitboard b) { return (b & ~FILE_A_BB) << 7; }   // NW
template <> inline Bitboard shift<-7>(Bitboard b) { return (b & ~FILE_H_BB) >> 7; }  // SE
template <> inline Bitboard shift<-9>(Bitboard b) { return (b & ~FILE_A_BB) >> 9; }  // SW

// Pawn attacks for a whole bitboard of pawns of color c.
template <Color C> inline Bitboard pawn_attacks_bb(Bitboard pawns) {
    return C == WHITE ? shift<7>(pawns) | shift<9>(pawns)
                      : shift<-7>(pawns) | shift<-9>(pawns);
}

// Attack tables, filled by Bitboards::init().
extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
extern Bitboard KnightAttacks[SQUARE_NB];
extern Bitboard KingAttacks[SQUARE_NB];
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB]; // squares strictly between (incl. none if not aligned)
extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];    // full line through two aligned squares

struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    unsigned shift;
    unsigned index(Bitboard occ) const {
        return unsigned(((occ & mask) * magic) >> shift);
    }
};

extern Magic RookMagics[SQUARE_NB];
extern Magic BishopMagics[SQUARE_NB];

inline Bitboard bishop_attacks(Square s, Bitboard occ) {
    const Magic& m = BishopMagics[s];
    return m.attacks[m.index(occ)];
}
inline Bitboard rook_attacks(Square s, Bitboard occ) {
    const Magic& m = RookMagics[s];
    return m.attacks[m.index(occ)];
}
inline Bitboard queen_attacks(Square s, Bitboard occ) {
    return bishop_attacks(s, occ) | rook_attacks(s, occ);
}

// Generic attacks by piece type (non-pawn).
inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occ) {
    switch (pt) {
        case KNIGHT: return KnightAttacks[s];
        case BISHOP: return bishop_attacks(s, occ);
        case ROOK:   return rook_attacks(s, occ);
        case QUEEN:  return queen_attacks(s, occ);
        case KING:   return KingAttacks[s];
        default:     return 0;
    }
}

namespace Bitboards {
void init();
std::string pretty(Bitboard b); // debug print
}

} // namespace ix
