#pragma once
#include "types.h"

namespace ix {
namespace Zobrist {

extern U64 psq[PIECE_NB][SQUARE_NB];
extern U64 enpassant[8];    // by file
extern U64 castling[16];    // by rights mask
extern U64 side;            // black to move

void init();

} // namespace Zobrist
} // namespace ix
