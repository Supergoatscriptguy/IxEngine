#include "nnue.h"
#include "bitboard.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#if defined(__AVX2__)
#include <immintrin.h>
#endif
#ifdef IX_NNUE_CHECK
#include <cstdio>
#include <cstdlib>
#endif

namespace ix {
namespace NNUE {

bool enabled = false;

struct Network {
    int16_t ftWeights[INPUT * HL];        // feature transformer (feature-major)
    int16_t ftBias[HL];
    int16_t outWeights[BUCKETS][2 * HL];  // [bucket][own .. opp]
    int16_t outBias[BUCKETS];
};

static Network net;

// Feature index for a piece (colour pc, type pt, square sq) from `persp`'s view.
static inline int feature(Color persp, Color pc, PieceType pt, Square sq) {
    int relColor = (pc == persp) ? 0 : 1;
    int relSq = (persp == WHITE) ? sq : (sq ^ 56);
    return relColor * 384 + int(pt) * 64 + relSq;
}

static inline void add_feature(Accumulator& a, Color pc, PieceType pt, Square sq) {
    const int16_t* w = net.ftWeights + feature(WHITE, pc, pt, sq) * HL;
    const int16_t* b = net.ftWeights + feature(BLACK, pc, pt, sq) * HL;
    for (int i = 0; i < HL; ++i) { a.v[WHITE][i] += w[i]; a.v[BLACK][i] += b[i]; }
}
static inline void sub_feature(Accumulator& a, Color pc, PieceType pt, Square sq) {
    const int16_t* w = net.ftWeights + feature(WHITE, pc, pt, sq) * HL;
    const int16_t* b = net.ftWeights + feature(BLACK, pc, pt, sq) * HL;
    for (int i = 0; i < HL; ++i) { a.v[WHITE][i] -= w[i]; a.v[BLACK][i] -= b[i]; }
}

void refresh(Accumulator& acc, const Position& pos) {
    for (int i = 0; i < HL; ++i) { acc.v[WHITE][i] = net.ftBias[i]; acc.v[BLACK][i] = net.ftBias[i]; }
    Bitboard occ = pos.pieces();
    while (occ) {
        Square s = pop_lsb(occ);
        Piece pc = pos.piece_on(s);
        add_feature(acc, color_of(pc), type_of(pc), s);
    }
}

void apply(Accumulator& dst, const Accumulator& src, const DirtyPiece& dp) {
    std::memcpy(&dst, &src, sizeof(Accumulator));
    for (int j = 0; j < dp.n; ++j) {
        Color c = color_of(dp.pc[j]);
        PieceType pt = type_of(dp.pc[j]);
        if (dp.from[j] != SQ_NONE) sub_feature(dst, c, pt, dp.from[j]);
        if (dp.to[j] != SQ_NONE) add_feature(dst, c, pt, dp.to[j]);
    }
}

// SCReLU dot product: sum over i of clamp(acc[i],0,QA)^2 * w[i].
static int64_t screlu_dot_scalar(const int16_t* acc, const int16_t* w) {
    int64_t s = 0;
    for (int i = 0; i < HL; ++i) {
        int v = std::clamp<int>(acc[i], 0, QA);
        s += int64_t(v * v) * w[i];
    }
    return s;
}

#if defined(__AVX2__)
// 16-wide: clamp, then madd(a, a*w) accumulates a^2*w as int32 (safe since
// output weights are clamped to +/-127, so a*w fits int16). Lanes summed in int64.
static int64_t screlu_dot(const int16_t* acc, const int16_t* w) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i qa = _mm256_set1_epi16((short)QA);
    __m256i sum = _mm256_setzero_si256();
    for (int i = 0; i < HL; i += 16) {
        __m256i a = _mm256_loadu_si256((const __m256i*)(acc + i));
        a = _mm256_min_epi16(_mm256_max_epi16(a, zero), qa);
        __m256i wv = _mm256_loadu_si256((const __m256i*)(w + i));
        sum = _mm256_add_epi32(sum, _mm256_madd_epi16(a, _mm256_mullo_epi16(a, wv)));
    }
    int32_t lanes[8];
    _mm256_storeu_si256((__m256i*)lanes, sum);
    int64_t t = 0;
    for (int k = 0; k < 8; ++k) t += lanes[k];
    return t;
}
#else
static int64_t screlu_dot(const int16_t* acc, const int16_t* w) { return screlu_dot_scalar(acc, w); }
#endif

int eval_acc(const Accumulator& acc, const Position& pos) {
    Color stm = pos.side_to_move();
    const int16_t* own = acc.v[stm];
    const int16_t* opp = acc.v[~stm];

    int bucket = std::clamp((popcount(pos.pieces()) - 2) / 4, 0, BUCKETS - 1);
    const int16_t* w = net.outWeights[bucket];

    int64_t sum = screlu_dot(own, w) + screlu_dot(opp, w + HL);
#ifdef IX_NNUE_CHECK
    int64_t scal = screlu_dot_scalar(own, w) + screlu_dot_scalar(opp, w + HL);
    if (sum != scal) { std::fprintf(stderr, "NNUE FWD MISMATCH\n"); std::abort(); }
#endif
    int64_t eval = (sum / QA + net.outBias[bucket]) * SCALE / (QA * QB);
    return int(eval);
}

int evaluate(const Position& pos) {
    Accumulator acc;
    refresh(acc, pos);
    return eval_acc(acc, pos);
}

bool load(const std::string& path) {
    enabled = false;
    if (path.empty() || path == "<empty>") return false;

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(&net), sizeof(Network));
    if (!f || f.gcount() != std::streamsize(sizeof(Network))) return false;
    char extra;
    if (f.read(&extra, 1)) return false; // trailing data => size/arch mismatch

    enabled = true;
    return true;
}

} // namespace NNUE
} // namespace ix
