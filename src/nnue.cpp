#include "nnue.h"
#include "position.h"
#include "bitboard.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

namespace ix {
namespace NNUE {

bool enabled = false;

// Quantized network, laid out exactly as the trainer writes it (little-endian).
struct Network {
    int16_t ftWeights[INPUT * HL];        // feature transformer
    int16_t ftBias[HL];
    int16_t outWeights[BUCKETS][2 * HL];  // [bucket][own .. opp]
    int16_t outBias[BUCKETS];
};

static Network net;

// Feature index for a piece (colour pc, type pt, square sq) from `persp`'s view.
// Own pieces occupy the first half; the board flips vertically for Black.
static inline int feature(Color persp, Color pc, PieceType pt, Square sq) {
    int relColor = (pc == persp) ? 0 : 1;
    int relSq = (persp == WHITE) ? sq : (sq ^ 56);
    return relColor * 384 + int(pt) * 64 + relSq;
}

// Rebuild both accumulators from scratch (correct, not yet incremental).
static void refresh(const Position& pos, int16_t accW[HL], int16_t accB[HL]) {
    for (int i = 0; i < HL; ++i) { accW[i] = net.ftBias[i]; accB[i] = net.ftBias[i]; }

    Bitboard occ = pos.pieces();
    while (occ) {
        Square s = pop_lsb(occ);
        Piece pc = pos.piece_on(s);
        Color c = color_of(pc);
        PieceType pt = type_of(pc);
        const int16_t* wW = net.ftWeights + feature(WHITE, c, pt, s) * HL;
        const int16_t* wB = net.ftWeights + feature(BLACK, c, pt, s) * HL;
        for (int i = 0; i < HL; ++i) { accW[i] += wW[i]; accB[i] += wB[i]; }
    }
}

int evaluate(const Position& pos) {
    int16_t accW[HL], accB[HL];
    refresh(pos, accW, accB);

    Color stm = pos.side_to_move();
    const int16_t* own = (stm == WHITE) ? accW : accB;
    const int16_t* opp = (stm == WHITE) ? accB : accW;

    int bucket = (popcount(pos.pieces()) - 2) / 4;
    if (bucket < 0) bucket = 0;
    if (bucket >= BUCKETS) bucket = BUCKETS - 1;
    const int16_t* w = net.outWeights[bucket];

    int64_t sum = 0;
    for (int i = 0; i < HL; ++i) {
        int v = std::clamp<int>(own[i], 0, QA);
        sum += int64_t(v * v) * w[i];
    }
    for (int i = 0; i < HL; ++i) {
        int v = std::clamp<int>(opp[i], 0, QA);
        sum += int64_t(v * v) * w[HL + i];
    }
    // SCReLU squares one extra QA factor; divide it out, add bias, scale to cp.
    int64_t eval = (sum / QA + net.outBias[bucket]) * SCALE / (QA * QB);
    return int(eval);
}

bool load(const std::string& path) {
    enabled = false;
    if (path.empty() || path == "<empty>") return false;

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    f.read(reinterpret_cast<char*>(&net), sizeof(Network));
    if (!f || f.gcount() != std::streamsize(sizeof(Network))) return false;

    // Reject if there's trailing data — that means a size/arch mismatch.
    char extra;
    if (f.read(&extra, 1)) return false;

    enabled = true;
    return true;
}

} // namespace NNUE
} // namespace ix
