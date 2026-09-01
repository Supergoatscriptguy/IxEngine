#pragma once
// 768 -> HL perspective net, SCReLU, piece-count output buckets, int16.
// Off until a net is loaded through the EvalFile option.

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

struct Accumulator {
    int16_t v[COLOR_NB][HL];
};

bool load(const std::string& path);

void refresh(Accumulator& acc, const Position& pos);
void apply(Accumulator& dst, const Accumulator& src, const DirtyPiece& dp);
int eval_acc(const Accumulator& acc, const Position& pos);

int evaluate(const Position& pos);   // refresh + eval_acc

} // namespace NNUE
} // namespace ix
