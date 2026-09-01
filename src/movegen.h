#pragma once
#include "position.h"

namespace ix {

enum GenType { GEN_ALL, GEN_CAPTURES, GEN_QUIETS };

// Pseudo-legal moves into out[MAX_MOVES]; returns the count.
// GEN_CAPTURES also includes promotions (for quiescence).
int generate(const Position& pos, Move* out, GenType type);

} // namespace ix
