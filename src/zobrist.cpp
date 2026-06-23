#include "zobrist.h"

namespace ix {
namespace Zobrist {

U64 psq[PIECE_NB][SQUARE_NB];
U64 enpassant[8];
U64 castling[16];
U64 side;

namespace {
struct PRNG {
    U64 s;
    explicit PRNG(U64 seed) : s(seed) {}
    U64 rand64() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
};
}

void init() {
    PRNG rng(1070372ULL);

    for (int p = 0; p < PIECE_NB; ++p)
        for (int s = 0; s < SQUARE_NB; ++s)
            psq[p][s] = rng.rand64();

    for (int f = 0; f < 8; ++f)
        enpassant[f] = rng.rand64();

    // Build all 16 castling combinations from 4 independent base keys so that
    // partial-right updates compose consistently.
    U64 base[4];
    for (int i = 0; i < 4; ++i) base[i] = rng.rand64();
    for (int cr = 0; cr < 16; ++cr) {
        castling[cr] = 0;
        for (int i = 0; i < 4; ++i)
            if (cr & (1 << i)) castling[cr] ^= base[i];
    }

    side = rng.rand64();
}

} // namespace Zobrist
} // namespace ix
