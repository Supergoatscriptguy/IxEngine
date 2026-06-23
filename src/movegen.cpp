#include "movegen.h"

namespace ix {

namespace {

inline Move* add_promotions(Move* m, Square from, Square to, bool capture) {
    int base = capture ? FLAG_PROMO_N_CAP : FLAG_PROMO_N;
    *m++ = make_move(from, to, base + 3); // queen first (most likely best)
    *m++ = make_move(from, to, base + 0); // knight
    *m++ = make_move(from, to, base + 2); // rook
    *m++ = make_move(from, to, base + 1); // bishop
    return m;
}

template <Color C>
Move* gen_pawns(const Position& pos, Move* m, GenType type) {
    constexpr Color Them = (C == WHITE) ? BLACK : WHITE;
    constexpr Bitboard PromoRank = (C == WHITE) ? RANK_7_BB : RANK_2_BB;
    constexpr Bitboard DblRank = (C == WHITE) ? RANK_3_BB : RANK_6_BB;
    constexpr int Up = (C == WHITE) ? 8 : -8;
    constexpr int CapL = (C == WHITE) ? 7 : -9;  // toward a-file side
    constexpr int CapR = (C == WHITE) ? 9 : -7;  // toward h-file side

    const Bitboard pawns = pos.pieces(C, PAWN);
    const Bitboard empty = ~pos.pieces();
    const Bitboard enemies = pos.pieces(Them);

    const Bitboard promoPawns = pawns & PromoRank;
    const Bitboard rest = pawns & ~PromoRank;

    const bool wantCaps = (type != GEN_QUIETS);
    const bool wantQuiets = (type != GEN_CAPTURES);

    // --- non-promoting pushes (quiet) ---
    if (wantQuiets) {
        Bitboard push1 = shift<Up>(rest) & empty;
        Bitboard push2 = shift<Up>(push1 & DblRank) & empty;
        Bitboard t = push1;
        while (t) { Square to = pop_lsb(t); *m++ = make_move(Square(to - Up), to, FLAG_QUIET); }
        t = push2;
        while (t) { Square to = pop_lsb(t); *m++ = make_move(Square(to - 2 * Up), to, FLAG_DOUBLE_PUSH); }
    }

    // --- non-promoting captures + en passant ---
    if (wantCaps) {
        Bitboard l = shift<CapL>(rest) & enemies;
        Bitboard r = shift<CapR>(rest) & enemies;
        while (l) { Square to = pop_lsb(l); *m++ = make_move(Square(to - CapL), to, FLAG_CAPTURE); }
        while (r) { Square to = pop_lsb(r); *m++ = make_move(Square(to - CapR), to, FLAG_CAPTURE); }

        Square ep = pos.ep_square();
        if (ep != SQ_NONE) {
            Bitboard attackers = rest & PawnAttacks[Them][ep];
            while (attackers) { Square from = pop_lsb(attackers); *m++ = make_move(from, ep, FLAG_EP_CAPTURE); }
        }
    }

    // --- promotions (pushes + captures) ---
    if (wantCaps && promoPawns) {
        Bitboard push = shift<Up>(promoPawns) & empty;
        Bitboard l = shift<CapL>(promoPawns) & enemies;
        Bitboard r = shift<CapR>(promoPawns) & enemies;
        while (push) { Square to = pop_lsb(push); m = add_promotions(m, Square(to - Up), to, false); }
        while (l) { Square to = pop_lsb(l); m = add_promotions(m, Square(to - CapL), to, true); }
        while (r) { Square to = pop_lsb(r); m = add_promotions(m, Square(to - CapR), to, true); }
    }

    return m;
}

template <PieceType Pt>
Move* gen_pieces(const Position& pos, Move* m, Bitboard target, Bitboard enemies) {
    Color us = pos.side_to_move();
    Bitboard bb = pos.pieces(us, Pt);
    Bitboard occ = pos.pieces();
    while (bb) {
        Square from = pop_lsb(bb);
        Bitboard att = attacks_bb(Pt, from, occ) & target;
        while (att) {
            Square to = pop_lsb(att);
            int flag = (enemies & square_bb(to)) ? FLAG_CAPTURE : FLAG_QUIET;
            *m++ = make_move(from, to, flag);
        }
    }
    return m;
}

template <Color C>
Move* gen_castling(const Position& pos, Move* m) {
    if (pos.in_check()) return m;
    constexpr Color Them = (C == WHITE) ? BLACK : WHITE;
    const Bitboard occ = pos.pieces();
    const int cr = pos.castling_rights();

    if (C == WHITE) {
        if ((cr & WHITE_OO) && !(occ & (square_bb(F1) | square_bb(G1)))
            && !pos.is_attacked(F1, Them) && !pos.is_attacked(G1, Them))
            *m++ = make_move(E1, G1, FLAG_KING_CASTLE);
        if ((cr & WHITE_OOO) && !(occ & (square_bb(B1) | square_bb(C1) | square_bb(D1)))
            && !pos.is_attacked(D1, Them) && !pos.is_attacked(C1, Them))
            *m++ = make_move(E1, C1, FLAG_QUEEN_CASTLE);
    } else {
        if ((cr & BLACK_OO) && !(occ & (square_bb(F8) | square_bb(G8)))
            && !pos.is_attacked(F8, Them) && !pos.is_attacked(G8, Them))
            *m++ = make_move(E8, G8, FLAG_KING_CASTLE);
        if ((cr & BLACK_OOO) && !(occ & (square_bb(B8) | square_bb(C8) | square_bb(D8)))
            && !pos.is_attacked(D8, Them) && !pos.is_attacked(C8, Them))
            *m++ = make_move(E8, C8, FLAG_QUEEN_CASTLE);
    }
    return m;
}

template <Color C>
int gen_all(const Position& pos, Move* out, GenType type) {
    Move* m = out;
    constexpr Color Them = (C == WHITE) ? BLACK : WHITE;

    Bitboard enemies = pos.pieces(Them);
    Bitboard target;
    if (type == GEN_CAPTURES) target = enemies;
    else if (type == GEN_QUIETS) target = ~pos.pieces();
    else target = ~pos.pieces(C);

    m = gen_pawns<C>(pos, m, type);
    m = gen_pieces<KNIGHT>(pos, m, target, enemies);
    m = gen_pieces<BISHOP>(pos, m, target, enemies);
    m = gen_pieces<ROOK>(pos, m, target, enemies);
    m = gen_pieces<QUEEN>(pos, m, target, enemies);
    m = gen_pieces<KING>(pos, m, target, enemies);

    if (type != GEN_CAPTURES)
        m = gen_castling<C>(pos, m);

    return int(m - out);
}

} // namespace

int generate(const Position& pos, Move* out, GenType type) {
    return pos.side_to_move() == WHITE ? gen_all<WHITE>(pos, out, type)
                                       : gen_all<BLACK>(pos, out, type);
}

} // namespace ix
