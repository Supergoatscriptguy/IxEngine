#include "bitboard.h"
#include <sstream>

namespace ix {

Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Bitboard KnightAttacks[SQUARE_NB];
Bitboard KingAttacks[SQUARE_NB];
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard LineBB[SQUARE_NB][SQUARE_NB];

Magic RookMagics[SQUARE_NB];
Magic BishopMagics[SQUARE_NB];

// Backing storage for the magic attack tables.
static Bitboard RookTable[102400];
static Bitboard BishopTable[5248];

namespace {

// Reference slow slider attack used to build the magic tables and the
// between/line tables. Walks each ray until it hits a blocker or the edge.
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
            // Stop if we'd leave the board or wrap across a file edge.
            if (next < A1 || next > H8) break;
            if (square_distance(s, next) > 2) break; // wrap guard for diagonal/horizontal
            s = next;
            attacks |= square_bb(s);
            if (occ & square_bb(s)) break;
        }
    }
    return attacks;
}

// xorshift64* PRNG with the "sparse" trick for finding magics fast.
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
    // Per-rank seeds (these reliably produce magics quickly on x86-64).
    static const U64 seeds[8] = {
        728, 10316, 55013, 32803, 12281, 15100, 16645, 255
    };

    Bitboard occupancy[4096], reference[4096];
    int epoch[4096] = {0};
    int cnt = 0;
    Bitboard* attacks = table;

    for (Square s = A1; s <= H8; s = Square(s + 1)) {
        // Relevant occupancy mask: exclude squares on the board edges that
        // cannot affect the attack set for this square.
        Bitboard edges = ((RANK_1_BB | RANK_8_BB) & ~rank_bb(rank_of(s)))
                       | ((FILE_A_BB | FILE_H_BB) & ~file_bb(file_of(s)));

        Magic& m = magics[s];
        m.mask = sliding_attack(pt, s, 0) & ~edges;
        m.shift = 64 - popcount(m.mask);
        m.attacks = (s == A1) ? table : magics[s - 1].attacks + (1 << popcount(magics[s - 1].mask));

        // Enumerate all subsets of the mask (Carry-Rippler) and record the
        // reference attack for each.
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
            // Pick a candidate magic with a sufficiently "spread" product.
            m.magic = 0;
            while (popcount((m.magic * m.mask) >> 56) < 6)
                m.magic = rng.sparse_rand();

            // Verify it produces a constructive collision-free mapping.
            ++cnt;
            for (i = 0; i < size; ++i) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < cnt) {
                    epoch[idx] = cnt;
                    m.attacks[idx] = reference[i];
                } else if (m.attacks[idx] != reference[i]) {
                    break; // collision; retry with a new magic
                }
            }
        }
        (void)attacks;
    }
}

} // namespace

namespace Bitboards {

void init() {
    // Leaper attacks.
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

    // Sliding attacks via magics.
    init_magics(ROOK, RookTable, RookMagics);
    init_magics(BISHOP, BishopTable, BishopMagics);

    // Between/line tables for pins, checks, SEE, etc.
    for (Square s1 = A1; s1 <= H8; s1 = Square(s1 + 1)) {
        for (Square s2 = A1; s2 <= H8; s2 = Square(s2 + 1)) {
            BetweenBB[s1][s2] = 0;
            LineBB[s1][s2] = 0;
            if (s1 == s2) continue;

            for (PieceType pt : { BISHOP, ROOK }) {
                if (sliding_attack(pt, s1, 0) & square_bb(s2)) {
                    LineBB[s1][s2] = (sliding_attack(pt, s1, 0)
                                      & sliding_attack(pt, s2, 0))
                                     | square_bb(s1) | square_bb(s2);
                    BetweenBB[s1][s2] = sliding_attack(pt, s1, square_bb(s2))
                                        & sliding_attack(pt, s2, square_bb(s1));
                }
            }
        }
    }
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
