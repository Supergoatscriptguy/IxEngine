#pragma once
#include "position.h"
#include <atomic>
#include <cstdint>
#include <string>

namespace ix {

std::string move_to_uci(Move m);

namespace Search {

struct Limits {
    int time[COLOR_NB] = { 0, 0 };
    int inc[COLOR_NB] = { 0, 0 };
    int movestogo = 0;
    int depth = 0;
    int64_t nodes = 0;
    int movetime = 0;
    bool infinite = false;
};

extern std::atomic<bool> stopFlag;
extern int moveOverhead; // ms, UCI-configurable

void init();          // precompute reduction tables
void clear();         // reset history/killers (new game)
void set_threads(int n); // number of search threads (Lazy SMP)

// Launch an asynchronous search on `pos`. The caller must not touch `pos`
// until wait() returns.
void start(Position& pos, const Limits& limits);
void stop();          // request the running search to finish ASAP
void wait();          // join the search thread

} // namespace Search
} // namespace ix
