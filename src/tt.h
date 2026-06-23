#pragma once
#include "types.h"
#include <vector>

namespace ix {

enum Bound : uint8_t {
    BOUND_NONE = 0,
    BOUND_UPPER = 1, // fail-low  (value is an upper bound)
    BOUND_LOWER = 2, // fail-high (value is a lower bound)
    BOUND_EXACT = 3
};

// 10-byte entry. Clusters of 3 fit (with padding) into a 32-byte half cache line.
struct TTEntry {
    uint16_t key16;
    uint16_t move16;
    int16_t  value16;
    int16_t  eval16;
    uint8_t  depth8;
    uint8_t  genBound8;

    Move move() const { return Move(move16); }
    int value() const { return value16; }
    int eval() const { return eval16; }
    int depth() const { return int(depth8); }
    Bound bound() const { return Bound(genBound8 & 0x3); }
    bool occupied() const { return bound() != BOUND_NONE; }

    void save(U64 k, int v, Bound b, int d, Move m, int ev, uint8_t gen);
};

class TranspositionTable {
public:
    void resize(size_t mb);
    void clear();
    void new_search() { generation8 += GEN_DELTA; }
    uint8_t generation() const { return generation8; }

    TTEntry* probe(U64 key, bool& found);
    int hashfull() const; // permille of entries used (this generation)
    void prefetch(U64 key) const;

private:
    static constexpr int CLUSTER_SIZE = 3;
    static constexpr unsigned GEN_DELTA = 0x4;   // bound uses low 2 bits
    static constexpr unsigned GEN_MASK = 0xFC;
    static constexpr unsigned GEN_CYCLE = 0x100;

    struct Cluster {
        TTEntry entry[CLUSTER_SIZE];
        char padding[2];
    };

    std::vector<Cluster> table;
    size_t clusterCount = 0;
    uint8_t generation8 = 0;
};

extern TranspositionTable TT;

} // namespace ix
