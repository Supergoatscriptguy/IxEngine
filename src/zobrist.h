#pragma once
#include "types.h"

namespace ix {
namespace Zobrist {

extern U64 psq[PIECE_NB][SQUARE_NB]; // [piece][square]
extern U64 enpassant[8];             // [file]
extern U64 castling[16];             // [castling-rights bitmask]
extern U64 side;                     // side-to-move == BLACK

void init();

} // namespace Zobrist
} // namespace ix
