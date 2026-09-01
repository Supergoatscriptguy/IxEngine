#include "bitboard.h"
#include <sstream>

namespace ix {

Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Bitboard KnightAttacks[SQUARE_NB];
Bitboard KingAttacks[SQUARE_NB];

Magic RookMagics[SQUARE_NB];
Magic BishopMagics[SQUARE_NB];

static Bitboard RookTable[102400];
static Bitboard BishopTable[5248];

namespace {

// Slow ray walk, only used to build the magic tables.
Bitboard sliding_attack(PieceType pt, Square sq, Bitboard occ) {
    Bitboard attacks = 0;
    const int rookDirs[4] = { 8, -8, 1, -1 };
    const int bishopDirs[4] = { 9, 7, -7, -9 };
    const int* dirs = (pt == ROOK) ? rookDirs : bishopDirs;

    for (int i = 0; i < 4; ++i) {
        int d = dirs[i];
        Square s = sq;
        while (true) {
            Square next = Square(s + d);
            if (next < A1 || next > H8) break;
            if (square_distance(s, next) > 2) break;   // wrapped around the edge
            s = next;
            attacks |= square_bb(s);
            if (occ & square_bb(s)) break;
        }
    }
    return attacks;
}

struct PRNG {
    U64 s;
    explicit PRNG(U64 seed) : s(seed) {}
    U64 rand64() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    U64 sparse_rand() { return rand64() & rand64() & rand64(); }
};

void init_magics(PieceType pt, Bitboard table[], Magic magics[]) {
    static const U64 seeds[8] = {
        728, 10316, 55013, 32803, 12281, 15100, 16645, 255
    };

    Bitboard occupancy[4096], reference[4096];
    int epoch[4096] = {0};
    int cnt = 0;
    Bitboard* attacks = table;

    for (Square s = A1; s <= H8; s = Square(s + 1)) {
        Bitboard edges = ((RANK_1_BB | RANK_8_BB) & ~rank_bb(rank_of(s)))
                       | ((FILE_A_BB | FILE_H_BB) & ~file_bb(file_of(s)));

        Magic& m = magics[s];
        m.mask = sliding_attack(pt, s, 0) & ~edges;
        m.shift = 64 - popcount(m.mask);
        m.attacks = (s == A1) ? table : magics[s - 1].attacks + (1 << popcount(magics[s - 1].mask));

        Bitboard b = 0;
        int size = 0;
        do {
            occupancy[size] = b;
            reference[size] = sliding_attack(pt, s, b);
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);

        PRNG rng(seeds[rank_of(s)]);

        for (int i = 0; i < size;) {
            m.magic = 0;
            while (popcount((m.magic * m.mask) >> 56) < 6)
                m.magic = rng.sparse_rand();

            ++cnt;
            for (i = 0; i < size; ++i) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < cnt) {
                    epoch[idx] = cnt;
                    m.attacks[idx] = reference[i];
                } else if (m.attacks[idx] != reference[i]) {
                    break;
                }
            }
        }
        (void)attacks;
    }
}

} // namespace

namespace Bitboards {

void init() {
    for (Square s = A1; s <= H8; s = Square(s + 1)) {
        Bitboard b = square_bb(s);
        PawnAttacks[WHITE][s] = pawn_attacks_bb<WHITE>(b);
        PawnAttacks[BLACK][s] = pawn_attacks_bb<BLACK>(b);

        Bitboard n = 0;
        const int kOff[8] = { 17, 15, 10, 6, -6, -10, -15, -17 };
        for (int o : kOff) {
            Square t = Square(int(s) + o);
            if (t >= A1 && t <= H8 && square_distance(s, t) <= 2)
                n |= square_bb(t);
        }
        KnightAttacks[s] = n;

        Bitboard k = 0;
        const int kkOff[8] = { 8, -8, 1, -1, 9, 7, -7, -9 };
        for (int o : kkOff) {
            Square t = Square(int(s) + o);
            if (t >= A1 && t <= H8 && square_distance(s, t) <= 1)
                k |= square_bb(t);
        }
        KingAttacks[s] = k;
    }

    init_magics(ROOK, RookTable, RookMagics);
    init_magics(BISHOP, BishopTable, BishopMagics);
}

std::string pretty(Bitboard b) {
    std::ostringstream os;
    os << "  +-----------------+\n";
    for (int r = 7; r >= 0; --r) {
        os << (r + 1) << " | ";
        for (int f = 0; f < 8; ++f)
            os << (test_bit(b, make_square(File(f), Rank(r))) ? "X " : ". ");
        os << "|\n";
    }
    os << "  +-----------------+\n    a b c d e f g h\n";
    return os.str();
}

} // namespace Bitboards
} // namespace ix
