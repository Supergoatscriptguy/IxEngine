#pragma once
#include "position.h"

namespace ix {

namespace Eval { void init(); }

// Centipawns, side to move's point of view.
int evaluate(const Position& pos);

} // namespace ix
