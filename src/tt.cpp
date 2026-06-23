#include "tt.h"
#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace ix {

TranspositionTable TT;

void TTEntry::save(U64 k, int v, Bound b, int d, Move m, int ev, uint8_t gen) {
    uint16_t k16 = uint16_t(k >> 48);

    // Preserve any existing best move for this slot unless we have a new one.
    if (m || k16 != key16)
        move16 = uint16_t(m);

    // Overwrite when: exact bound, different position, or not much shallower.
    if (b == BOUND_EXACT || k16 != key16 || d + 4 > depth8) {
        key16 = k16;
        value16 = int16_t(v);
        eval16 = int16_t(ev);
        depth8 = uint8_t(d < 0 ? 0 : d);
        genBound8 = uint8_t(gen | b);
    }
}

void TranspositionTable::resize(size_t mb) {
    if (mb < 1) mb = 1;
    size_t bytes = mb * 1024 * 1024;
    size_t n = bytes / sizeof(Cluster);

    // Largest power of two not exceeding n (enables fast index masking).
    size_t pow2 = 1;
    while (pow2 * 2 <= n) pow2 *= 2;
    clusterCount = pow2;

    table.assign(clusterCount, Cluster{});
    generation8 = 0;
}

void TranspositionTable::clear() {
    std::memset(table.data(), 0, table.size() * sizeof(Cluster));
    generation8 = 0;
}

TTEntry* TranspositionTable::probe(U64 key, bool& found) {
    TTEntry* const cl = table[size_t(key) & (clusterCount - 1)].entry;
    const uint16_t k16 = uint16_t(key >> 48);

    for (int i = 0; i < CLUSTER_SIZE; ++i) {
        if (cl[i].key16 == k16 && cl[i].occupied()) {
            // Refresh generation so the entry survives replacement.
            cl[i].genBound8 = uint8_t(generation8 | cl[i].bound());
            found = true;
            return &cl[i];
        }
    }

    // Choose a victim: lowest (depth - relativeAge).
    TTEntry* replace = cl;
    for (int i = 1; i < CLUSTER_SIZE; ++i) {
        int rScore = replace->depth8
                   - ((GEN_CYCLE + generation8 - replace->genBound8) & GEN_MASK);
        int cScore = cl[i].depth8
                   - ((GEN_CYCLE + generation8 - cl[i].genBound8) & GEN_MASK);
        if (cScore < rScore)
            replace = &cl[i];
    }

    found = false;
    return replace;
}

int TranspositionTable::hashfull() const {
    if (clusterCount == 0) return 0;
    int cnt = 0;
    const int sample = 1000;
    for (int i = 0; i < sample; ++i)
        for (int j = 0; j < CLUSTER_SIZE; ++j)
            if (table[i].entry[j].occupied()
                && (table[i].entry[j].genBound8 & GEN_MASK) == generation8)
                cnt++;
    return cnt / (CLUSTER_SIZE * sample / 1000);
}

void TranspositionTable::prefetch(U64 key) const {
#if defined(__GNUC__)
    __builtin_prefetch(&table[size_t(key) & (clusterCount - 1)]);
#elif defined(_MSC_VER)
    _mm_prefetch((const char*)&table[size_t(key) & (clusterCount - 1)], _MM_HINT_T0);
#else
    (void)key;
#endif
}

} // namespace ix
