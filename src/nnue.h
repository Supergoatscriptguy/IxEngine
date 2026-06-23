#pragma once
// NNUE evaluation: a (768 -> HL) perspective network with SCReLU and
// piece-count output buckets, int16-quantized. Disabled unless a net is loaded
// via the `EvalFile` UCI option, so the hand-crafted eval stays the default.
//
// The accumulator is maintained incrementally during search (apply() from a
// move's DirtyPiece list); refresh() rebuilds it from scratch at the root.

#include "types.h"
#include "position.h"
#include <string>

namespace ix {
namespace NNUE {

constexpr int INPUT   = 768;
constexpr int HL      = 512;
constexpr int BUCKETS = 8;
constexpr int QA      = 255;
constexpr int QB      = 64;
constexpr int SCALE   = 400;

extern bool enabled;

// Per-perspective accumulators (own/other handled by stm at eval time).
struct Accumulator {
    int16_t v[COLOR_NB][HL];
};

bool load(const std::string& path);

void refresh(Accumulator& acc, const Position& pos);                 // from scratch
void apply(Accumulator& dst, const Accumulator& src, const DirtyPiece& dp); // incremental
int eval_acc(const Accumulator& acc, const Position& pos);           // forward pass, cp

int evaluate(const Position& pos);   // scratch (refresh + eval_acc), for direct callers

} // namespace NNUE
} // namespace ix
