#pragma once
#include "position.h"
#include <cstdint>

namespace ix {

uint64_t perft(Position& pos, int depth);
void perft_divide(Position& pos, int depth); // prints per-move node counts

} // namespace ix
