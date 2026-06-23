#pragma once
#include "position.h"

namespace ix {

namespace Eval { void init(); }

// Static evaluation in centipawns, from the side-to-move's perspective
// (positive = good for the side to move).
int evaluate(const Position& pos);

} // namespace ix
