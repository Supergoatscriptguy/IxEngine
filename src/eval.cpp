#include "eval.h"

namespace ix {

// PeSTO material values and piece-square tables, laid out rank-8-first
// (index 0 = a8). A white piece on square s reads [s ^ 56]; black reads [s].
static const int mg_value[6] = { 82, 337, 365, 477, 1025, 0 };
static const int eg_value[6] = { 94, 281, 297, 512, 936, 0 };

static const int mg_pst[6][64] = {
    { // Pawn
        0,0,0,0,0,0,0,0,
        98,134,61,95,68,126,34,-11,
        -6,7,26,31,65,56,25,-20,
        -14,13,6,21,23,12,17,-23,
        -27,-2,-5,12,17,6,10,-25,
        -26,-4,-4,-10,3,3,33,-12,
        -35,-1,-20,-23,-15,24,38,-22,
        0,0,0,0,0,0,0,0 },
    { // Knight
        -167,-89,-34,-49,61,-97,-15,-107,
        -73,-41,72,36,23,62,7,-17,
        -47,60,37,65,84,129,73,44,
        -9,17,19,53,37,69,18,22,
        -13,4,16,13,28,19,21,-8,
        -23,-9,12,10,19,17,25,-16,
        -29,-53,-12,-3,-1,18,-14,-19,
        -105,-21,-58,-33,-17,-28,-19,-23 },
    { // Bishop
        -29,4,-82,-37,-25,-42,7,-8,
        -26,16,-18,-13,30,59,18,-47,
        -16,37,43,40,35,50,37,-2,
        -4,5,19,50,37,37,7,-2,
        -6,13,13,26,34,12,10,4,
        0,15,15,15,14,27,18,10,
        4,15,16,0,7,21,33,1,
        -33,-3,-14,-21,-13,-12,-39,-21 },
    { // Rook
        32,42,32,51,63,9,31,43,
        27,32,58,62,80,67,26,44,
        -5,19,26,36,17,45,61,16,
        -24,-11,7,26,24,35,-8,-20,
        -36,-26,-12,-1,9,-7,6,-23,
        -45,-25,-16,-17,3,0,-5,-33,
        -44,-16,-20,-9,-1,11,-6,-71,
        -19,-13,1,17,16,7,-37,-26 },
    { // Queen
        -28,0,29,12,59,44,43,45,
        -24,-39,-5,1,-16,57,28,54,
        -13,-17,7,8,29,56,47,57,
        -27,-27,-16,-16,-1,17,-2,1,
        -9,-26,-9,-10,-2,-4,3,-3,
        -14,2,-11,-2,-5,2,14,5,
        -35,-8,11,2,8,15,-3,1,
        -1,-18,-9,10,-15,-25,-31,-50 },
    { // King
        -65,23,16,-15,-56,-34,2,13,
        29,-1,-20,-7,-8,-4,-38,-29,
        -9,24,2,-16,-20,6,22,-22,
        -17,-20,-12,-27,-30,-25,-14,-36,
        -49,-1,-27,-39,-46,-44,-33,-51,
        -14,-14,-22,-46,-44,-30,-15,-27,
        1,7,-8,-64,-43,-16,9,8,
        -15,36,12,-54,8,-28,24,14 }
};

static const int eg_pst[6][64] = {
    { // Pawn
        0,0,0,0,0,0,0,0,
        178,173,158,134,147,132,165,187,
        94,100,85,67,56,53,82,84,
        32,24,13,5,-2,4,17,17,
        13,9,-3,-7,-7,-8,3,-1,
        4,7,-6,1,0,-5,-1,-8,
        13,8,8,10,13,0,2,-7,
        0,0,0,0,0,0,0,0 },
    { // Knight
        -58,-38,-13,-28,-31,-27,-63,-99,
        -25,-8,-25,-2,-9,-25,-24,-52,
        -24,-20,10,9,-1,-9,-19,-41,
        -17,3,22,22,22,11,8,-18,
        -18,-6,16,25,16,17,4,-18,
        -23,-3,-1,15,10,-3,-20,-22,
        -42,-20,-10,-5,-2,-20,-23,-44,
        -29,-51,-23,-15,-22,-18,-50,-64 },
    { // Bishop
        -14,-21,-11,-8,-7,-9,-17,-24,
        -8,-4,7,-12,-3,-13,-4,-14,
        2,-8,0,-1,-2,6,0,4,
        -3,9,12,9,14,10,3,2,
        -6,3,13,19,7,10,-3,-9,
        -12,-3,8,10,13,3,-7,-15,
        -14,-18,-7,-1,4,-9,-15,-27,
        -23,-9,-23,-5,-9,-16,-5,-17 },
    { // Rook
        13,10,18,15,12,12,8,5,
        11,13,13,11,-3,3,8,3,
        7,7,7,5,4,-3,-5,-3,
        4,3,13,1,2,1,-1,2,
        3,5,8,4,-5,-6,-8,-11,
        -4,0,-5,-1,-7,-12,-8,-16,
        -6,-6,0,2,-9,-9,-11,-3,
        -9,2,3,-1,-5,-13,4,-20 },
    { // Queen
        -9,22,22,27,27,19,10,20,
        -17,20,32,41,58,25,30,0,
        -20,6,9,49,47,35,19,9,
        3,22,24,45,57,40,57,36,
        -18,28,19,47,31,34,39,23,
        -16,-27,15,6,9,17,10,5,
        -22,-23,-30,-16,-16,-23,-36,-32,
        -33,-28,-22,-43,-5,-32,-20,-41 },
    { // King
        -74,-35,-18,-18,-11,15,4,-17,
        -12,17,14,17,17,38,23,11,
        10,17,23,15,20,45,44,13,
        -8,22,24,27,26,33,26,3,
        -18,-4,21,24,27,23,9,-11,
        -19,-3,11,21,23,16,7,-9,
        -27,-11,4,13,14,4,-5,-17,
        -53,-34,-21,-11,-28,-14,-24,-43 }
};

// Stockfish "classical" mobility bonuses [mg, eg] by attack count.
struct S { int mg, eg; };
static const S MobN[9] = {
    {-62,-81},{-53,-56},{-12,-31},{-4,-16},{3,5},{13,11},{22,17},{28,20},{33,25} };
static const S MobB[14] = {
    {-48,-59},{-20,-23},{16,-3},{26,13},{38,24},{51,42},{55,54},{63,57},
    {63,65},{68,73},{81,78},{81,86},{91,88},{98,97} };
static const S MobR[15] = {
    {-60,-78},{-20,-17},{2,23},{3,39},{3,70},{11,99},{22,103},{31,121},
    {40,134},{40,139},{41,158},{42,160},{44,178},{45,178},{50,185} };
static const S MobQ[28] = {
    {-30,-48},{-12,-30},{-8,-7},{-9,19},{20,40},{23,55},{23,59},{35,75},
    {38,78},{53,96},{64,96},{65,100},{65,121},{66,116},{67,124},{67,128},
    {72,127},{74,135},{76,142},{78,149},{79,150},{80,153},{81,158},{83,159},
    {85,164},{85,169},{86,178},{87,182} };

// Tunable term weights.
static const int BishopPairMG = 24, BishopPairEG = 42;
static const int RookOpenMG = 26, RookOpenEG = 12;
static const int RookSemiMG = 12, RookSemiEG = 6;
static const int Rook7thMG = 16, Rook7thEG = 28;
static const int DoubledMG = 9, DoubledEG = 22;
static const int IsolatedMG = 13, IsolatedEG = 8;
static const int Tempo = 14;
static const int PassedMG[8] = { 0, 5, 12, 20, 35, 60, 95, 0 };
static const int PassedEG[8] = { 0, 12, 22, 38, 62, 100, 160, 0 };

// King-attack weights per attacked king-ring square.
static const int KingAtkN = 2, KingAtkB = 2, KingAtkR = 3, KingAtkQ = 5;

// Precomputed pawn-structure masks.
static Bitboard AdjacentFiles[8];
static Bitboard ForwardFile[COLOR_NB][SQUARE_NB];
static Bitboard PassedMask[COLOR_NB][SQUARE_NB];
static Bitboard KingRing[SQUARE_NB];

namespace Eval {
void init() {
    for (int f = 0; f < 8; ++f) {
        AdjacentFiles[f] = 0;
        if (f > 0) AdjacentFiles[f] |= file_bb(File(f - 1));
        if (f < 7) AdjacentFiles[f] |= file_bb(File(f + 1));
    }
    for (int s = 0; s < SQUARE_NB; ++s) {
        Square sq = Square(s);
        File f = file_of(sq);
        Rank r = rank_of(sq);

        Bitboard wFront = 0, bFront = 0;
        for (int rr = r + 1; rr <= 7; ++rr) wFront |= rank_bb(Rank(rr));
        for (int rr = r - 1; rr >= 0; --rr) bFront |= rank_bb(Rank(rr));

        ForwardFile[WHITE][s] = wFront & file_bb(f);
        ForwardFile[BLACK][s] = bFront & file_bb(f);

        Bitboard span = file_bb(f) | AdjacentFiles[f];
        PassedMask[WHITE][s] = wFront & span;
        PassedMask[BLACK][s] = bFront & span;

        KingRing[s] = KingAttacks[s] | square_bb(sq);
    }
}
} // namespace Eval

static inline int clamp_file(int f) { return f < 1 ? 1 : (f > 6 ? 6 : f); }

// King shelter penalty for color `us` (linear, midgame term).
static int king_shelter(const Position& pos, Color us) {
    Square ksq = pos.king_sq(us);
    int kf = clamp_file(file_of(ksq));
    Bitboard ownPawns = pos.pieces(us, PAWN);
    int penalty = 0;
    for (int f = kf - 1; f <= kf + 1; ++f) {
        Bitboard fp = ownPawns & file_bb(File(f));
        if (!fp) {
            penalty += 18; // (half-)open file next to the king
        } else {
            Square nearest = (us == WHITE) ? lsb(fp) : msb(fp);
            int dist = rank_distance(nearest, ksq);
            penalty += (dist - 1) * 6; // pawn advanced away from the king
        }
    }
    return penalty;
}

int evaluate(const Position& pos) {
    int mg = 0, eg = 0; // accumulators, White's perspective

    // --- material + piece-square tables ---
    for (int c = 0; c < COLOR_NB; ++c) {
        int sign = (c == WHITE) ? 1 : -1;
        for (int pt = PAWN; pt <= KING; ++pt) {
            Bitboard b = pos.pieces(Color(c), PieceType(pt));
            while (b) {
                Square s = pop_lsb(b);
                int idx = (c == WHITE) ? (s ^ 56) : s;
                mg += sign * (mg_value[pt] + mg_pst[pt][idx]);
                eg += sign * (eg_value[pt] + eg_pst[pt][idx]);
            }
        }
    }

    const Bitboard occ = pos.pieces();
    const Bitboard wPawns = pos.pieces(WHITE, PAWN), bPawns = pos.pieces(BLACK, PAWN);
    const Bitboard wPawnAtt = pawn_attacks_bb<WHITE>(wPawns);
    const Bitboard bPawnAtt = pawn_attacks_bb<BLACK>(bPawns);

    Bitboard mobArea[COLOR_NB];
    mobArea[WHITE] = ~(pos.pieces(WHITE, KING) | pos.pieces(WHITE, QUEEN) | wPawns | bPawnAtt);
    mobArea[BLACK] = ~(pos.pieces(BLACK, KING) | pos.pieces(BLACK, QUEEN) | bPawns | wPawnAtt);

    Bitboard enemyRing[COLOR_NB]; // ring of the king attacked by color c's pieces
    enemyRing[WHITE] = KingRing[pos.king_sq(BLACK)];
    enemyRing[BLACK] = KingRing[pos.king_sq(WHITE)];

    int kingDanger[COLOR_NB] = { 0, 0 };   // danger to each color's own king
    int kingAttackers[COLOR_NB] = { 0, 0 };

    // --- piece activity: mobility, king attacks, rook files, bishop pair ---
    for (int c = 0; c < COLOR_NB; ++c) {
        Color us = Color(c), them = ~us;
        int sign = (us == WHITE) ? 1 : -1;
        Bitboard ring = enemyRing[us];

        // Bishop pair
        if (pos.count(us, BISHOP) >= 2) { mg += sign * BishopPairMG; eg += sign * BishopPairEG; }

        Bitboard b;

        // Knights
        b = pos.pieces(us, KNIGHT);
        while (b) {
            Square s = pop_lsb(b);
            Bitboard a = KnightAttacks[s];
            int m = popcount(a & mobArea[us]);
            mg += sign * MobN[m].mg; eg += sign * MobN[m].eg;
            if (a & ring) { kingAttackers[them]++; kingDanger[them] += KingAtkN * popcount(a & ring); }
        }

        // Bishops
        b = pos.pieces(us, BISHOP);
        while (b) {
            Square s = pop_lsb(b);
            Bitboard a = bishop_attacks(s, occ);
            int m = popcount(a & mobArea[us]);
            mg += sign * MobB[m].mg; eg += sign * MobB[m].eg;
            if (a & ring) { kingAttackers[them]++; kingDanger[them] += KingAtkB * popcount(a & ring); }
        }

        // Rooks (with open/semi-open file and 7th-rank bonuses)
        b = pos.pieces(us, ROOK);
        Bitboard ourPawns = pos.pieces(us, PAWN);
        Bitboard theirPawns = pos.pieces(them, PAWN);
        while (b) {
            Square s = pop_lsb(b);
            Bitboard a = rook_attacks(s, occ);
            int m = popcount(a & mobArea[us]);
            mg += sign * MobR[m].mg; eg += sign * MobR[m].eg;
            if (a & ring) { kingAttackers[them]++; kingDanger[them] += KingAtkR * popcount(a & ring); }

            Bitboard fileBB = file_bb(file_of(s));
            if (!(ourPawns & fileBB)) {
                if (!(theirPawns & fileBB)) { mg += sign * RookOpenMG; eg += sign * RookOpenEG; }
                else { mg += sign * RookSemiMG; eg += sign * RookSemiEG; }
            }
            if (relative_rank(us, s) == RANK_7) { mg += sign * Rook7thMG; eg += sign * Rook7thEG; }
        }

        // Queens
        b = pos.pieces(us, QUEEN);
        while (b) {
            Square s = pop_lsb(b);
            Bitboard a = queen_attacks(s, occ);
            int m = popcount(a & mobArea[us]);
            if (m > 27) m = 27;
            mg += sign * MobQ[m].mg; eg += sign * MobQ[m].eg;
            if (a & ring) { kingAttackers[them]++; kingDanger[them] += KingAtkQ * popcount(a & ring); }
        }
    }

    // --- pawn structure: doubled, isolated, passed ---
    for (int c = 0; c < COLOR_NB; ++c) {
        Color us = Color(c), them = ~us;
        int sign = (us == WHITE) ? 1 : -1;
        Bitboard ourPawns = pos.pieces(us, PAWN);
        Bitboard theirPawns = pos.pieces(them, PAWN);
        Bitboard b = ourPawns;
        while (b) {
            Square s = pop_lsb(b);
            File f = file_of(s);
            if (!(ourPawns & AdjacentFiles[f])) { mg -= sign * IsolatedMG; eg -= sign * IsolatedEG; }
            if (ourPawns & ForwardFile[us][s]) { mg -= sign * DoubledMG; eg -= sign * DoubledEG; }
            if (!(theirPawns & PassedMask[us][s])) {
                int r = relative_rank(us, s);
                mg += sign * PassedMG[r]; eg += sign * PassedEG[r];
            }
        }
    }

    // --- king safety: dynamic danger (needs >=2 attackers) + shelter ---
    for (int c = 0; c < COLOR_NB; ++c) {
        Color us = Color(c);
        int sh = king_shelter(pos, us);
        int danger = kingDanger[us] + sh;
        int penalty = sh; // shelter is always relevant
        if (kingAttackers[us] >= 2) {
            int quad = danger * danger / 16;
            if (quad > 600) quad = 600;
            penalty += quad;
        }
        mg += (us == WHITE) ? -penalty : penalty;
    }

    // --- taper + tempo ---
    int phase = pos.game_phase();
    int score = (mg * phase + eg * (24 - phase)) / 24;
    score += (pos.side_to_move() == WHITE) ? Tempo : -Tempo;

    return (pos.side_to_move() == WHITE) ? score : -score;
}

} // namespace ix
