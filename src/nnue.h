#pragma once
// NNUE evaluation: a (768 -> HL) perspective network with SCReLU and
// piece-count output buckets, int16-quantized. Disabled unless a net is loaded
// via the `EvalFile` UCI option, so the hand-crafted eval remains the default.
//
// Architecture (must match the net produced by the bullet trainer):
//   inputs   : 768  = 2 colours x 6 piece types x 64 squares, side-to-move POV
//   accum    : HL per perspective (own + opponent), int16
//   activation: SCReLU  (clamp to [0,QA] then square)
//   output   : 8 buckets by piece count, each (2*HL -> 1)
//   quant    : feature weights x QA, output weights x QB, eval x SCALE

#include "types.h"
#include <string>

namespace ix {

class Position;

namespace NNUE {

constexpr int INPUT   = 768;
constexpr int HL      = 512;
constexpr int BUCKETS = 8;
constexpr int QA      = 255;
constexpr int QB      = 64;
constexpr int SCALE   = 400;

extern bool enabled;

// Load a quantized net from disk. Returns false (and leaves NNUE disabled) on
// any size/IO mismatch.
bool load(const std::string& path);

// Static evaluation from the side-to-move's perspective, in centipawns.
int evaluate(const Position& pos);

} // namespace NNUE
} // namespace ix
