#pragma once
#include "position.h"

namespace ix {

enum GenType { GEN_ALL, GEN_CAPTURES, GEN_QUIETS };

// Generate pseudo-legal moves into `out` (capacity MAX_MOVES). Returns count.
//   GEN_ALL      : every pseudo-legal move
//   GEN_CAPTURES : captures + all promotions + en passant (used in quiescence)
//   GEN_QUIETS   : non-captures (incl. castling), excluding promotions
int generate(const Position& pos, Move* out, GenType type);

} // namespace ix
