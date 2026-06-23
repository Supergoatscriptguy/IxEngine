#include "position.h"
#include "zobrist.h"
#include <sstream>
#include <cctype>

namespace ix {

// Static Exchange Evaluation piece values (centipawns), indexed by PieceType.
static const int SEEValue[7] = { 100, 320, 330, 500, 950, 0, 0 };
static int see_value(PieceType pt) { return SEEValue[pt]; }

// Mask applied to castling rights when a piece leaves/enters a square.
static int CastlingMask[SQUARE_NB];
static const bool castlingInit = []() {
    for (int i = 0; i < SQUARE_NB; ++i) CastlingMask[i] = ANY_CASTLING;
    CastlingMask[A1] &= ~WHITE_OOO;
    CastlingMask[H1] &= ~WHITE_OO;
    CastlingMask[E1] &= ~(WHITE_OO | WHITE_OOO);
    CastlingMask[A8] &= ~BLACK_OOO;
    CastlingMask[H8] &= ~BLACK_OO;
    CastlingMask[E8] &= ~(BLACK_OO | BLACK_OOO);
    return true;
}();

static Bitboard lsb_bb(Bitboard b) { return b & (~b + 1); }

// put/remove/move don't touch the hash key; do_move/undo_move own that.
void Position::put_piece(Piece pc, Square s) {
    Bitboard b = square_bb(s);
    board[s] = pc;
    byType[type_of(pc)] |= b;
    byColor[color_of(pc)] |= b;
}
void Position::remove_piece(Square s) {
    Piece pc = board[s];
    Bitboard b = square_bb(s);
    byType[type_of(pc)] ^= b;
    byColor[color_of(pc)] ^= b;
    board[s] = NO_PIECE;
}
void Position::move_piece(Square from, Square to) {
    Piece pc = board[from];
    Bitboard ft = square_bb(from) | square_bb(to);
    byType[type_of(pc)] ^= ft;
    byColor[color_of(pc)] ^= ft;
    board[from] = NO_PIECE;
    board[to] = pc;
}

void Position::clear() {
    for (int i = 0; i < PIECE_TYPE_NB; ++i) byType[i] = 0;
    byColor[WHITE] = byColor[BLACK] = 0;
    for (int s = 0; s < SQUARE_NB; ++s) board[s] = NO_PIECE;
    stm = WHITE;
    gamePly = 0;
    st = states;
    st->castlingRights = 0;
    st->epSquare = SQ_NONE;
    st->halfmoveClock = 0;
    st->key = 0;
    st->captured = NO_PIECE;
    st->checkers = 0;
    historyCount = 0;
}

void Position::set(const std::string& fen) {
    clear();

    std::istringstream is(fen);
    std::string boardStr, stmStr, castleStr, epStr;
    int halfmove = 0, fullmove = 1;
    is >> boardStr >> stmStr >> castleStr >> epStr;
    is >> halfmove >> fullmove;

    // Piece placement (rank 8 first).
    int rank = 7, file = 0;
    for (char c : boardStr) {
        if (c == '/') { rank--; file = 0; }
        else if (std::isdigit((unsigned char)c)) { file += c - '0'; }
        else {
            Color col = std::isupper((unsigned char)c) ? WHITE : BLACK;
            PieceType pt;
            switch (std::tolower((unsigned char)c)) {
                case 'p': pt = PAWN; break;
                case 'n': pt = KNIGHT; break;
                case 'b': pt = BISHOP; break;
                case 'r': pt = ROOK; break;
                case 'q': pt = QUEEN; break;
                case 'k': pt = KING; break;
                default: pt = NO_PIECE_TYPE; break;
            }
            if (pt != NO_PIECE_TYPE)
                put_piece(make_piece(col, pt), make_square(File(file), Rank(rank)));
            file++;
        }
    }

    stm = (stmStr == "b") ? BLACK : WHITE;

    int cr = 0;
    for (char c : castleStr) {
        switch (c) {
            case 'K': cr |= WHITE_OO; break;
            case 'Q': cr |= WHITE_OOO; break;
            case 'k': cr |= BLACK_OO; break;
            case 'q': cr |= BLACK_OOO; break;
            default: break;
        }
    }
    st->castlingRights = cr;

    st->epSquare = SQ_NONE;
    if (epStr.size() >= 2 && epStr[0] >= 'a' && epStr[0] <= 'h') {
        File f = File(epStr[0] - 'a');
        Rank r = Rank(epStr[1] - '1');
        st->epSquare = make_square(f, r);
    }

    st->halfmoveClock = halfmove;
    gamePly = (fullmove - 1) * 2 + (stm == BLACK ? 1 : 0);

    // Compute Zobrist key from scratch.
    U64 k = 0;
    for (int s = 0; s < SQUARE_NB; ++s)
        if (board[s] != NO_PIECE)
            k ^= Zobrist::psq[board[s]][s];
    if (stm == BLACK) k ^= Zobrist::side;
    k ^= Zobrist::castling[cr];
    if (st->epSquare != SQ_NONE) k ^= Zobrist::enpassant[file_of(st->epSquare)];
    st->key = k;

    st->captured = NO_PIECE;
    st->checkers = compute_checkers();

    history[0] = k;
    historyCount = 1;
}

void Position::set_startpos() {
    set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

std::string Position::fen() const {
    std::ostringstream os;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            Piece pc = board[make_square(File(f), Rank(r))];
            if (pc == NO_PIECE) { empty++; continue; }
            if (empty) { os << empty; empty = 0; }
            const char* sym = "PNBRQK";
            char c = sym[type_of(pc)];
            os << char(color_of(pc) == WHITE ? c : std::tolower(c));
        }
        if (empty) os << empty;
        if (r) os << '/';
    }
    os << (stm == WHITE ? " w " : " b ");

    int cr = st->castlingRights;
    if (!cr) os << '-';
    else {
        if (cr & WHITE_OO) os << 'K';
        if (cr & WHITE_OOO) os << 'Q';
        if (cr & BLACK_OO) os << 'k';
        if (cr & BLACK_OOO) os << 'q';
    }
    os << ' ';
    if (st->epSquare == SQ_NONE) os << '-';
    else os << char('a' + file_of(st->epSquare)) << char('1' + rank_of(st->epSquare));
    os << ' ' << st->halfmoveClock << ' ' << (gamePly / 2 + 1);
    return os.str();
}

Bitboard Position::attackers_to(Square s, Bitboard occ) const {
    return (PawnAttacks[BLACK][s] & pieces(WHITE, PAWN))
         | (PawnAttacks[WHITE][s] & pieces(BLACK, PAWN))
         | (KnightAttacks[s] & byType[KNIGHT])
         | (KingAttacks[s] & byType[KING])
         | (bishop_attacks(s, occ) & (byType[BISHOP] | byType[QUEEN]))
         | (rook_attacks(s, occ) & (byType[ROOK] | byType[QUEEN]));
}

bool Position::is_attacked(Square s, Color by) const {
    Bitboard occ = pieces();
    if (PawnAttacks[~by][s] & pieces(by, PAWN)) return true;
    if (KnightAttacks[s] & pieces(by, KNIGHT)) return true;
    if (KingAttacks[s] & pieces(by, KING)) return true;
    if (bishop_attacks(s, occ) & (pieces(by, BISHOP) | pieces(by, QUEEN))) return true;
    if (rook_attacks(s, occ) & (pieces(by, ROOK) | pieces(by, QUEEN))) return true;
    return false;
}

Bitboard Position::compute_checkers() const {
    Square ksq = king_sq(stm);
    return attackers_to(ksq, pieces()) & byColor[~stm];
}

void Position::do_move(Move m) {
    StateInfo& next = *(st + 1);
    next.castlingRights = st->castlingRights;
    next.epSquare = SQ_NONE;
    next.halfmoveClock = st->halfmoveClock + 1;
    next.captured = NO_PIECE;

    U64 k = st->key;
    if (st->epSquare != SQ_NONE) k ^= Zobrist::enpassant[file_of(st->epSquare)];

    Square from = from_sq(m), to = to_sq(m);
    int flag = move_flag(m);
    Color us = stm, them = ~stm;
    Piece pc = board[from];
    PieceType pt = type_of(pc);

    if (is_capture(m)) {
        Square capSq = to;
        if (flag == FLAG_EP_CAPTURE)
            capSq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
        Piece captured = board[capSq];
        next.captured = captured;
        k ^= Zobrist::psq[captured][capSq];
        remove_piece(capSq);
        next.halfmoveClock = 0;
    }

    move_piece(from, to);
    k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];

    if (pt == PAWN) {
        next.halfmoveClock = 0;
        if (flag == FLAG_DOUBLE_PUSH) {
            Square ep = Square((from + to) / 2);
            next.epSquare = ep;
            k ^= Zobrist::enpassant[file_of(ep)];
        } else if (is_promotion(m)) {
            Piece promo = make_piece(us, promotion_type(m));
            remove_piece(to);
            put_piece(promo, to);
            k ^= Zobrist::psq[pc][to] ^ Zobrist::psq[promo][to];
        }
    } else if (flag == FLAG_KING_CASTLE) {
        Square rf = (us == WHITE) ? H1 : H8;
        Square rt = (us == WHITE) ? F1 : F8;
        move_piece(rf, rt);
        Piece rook = make_piece(us, ROOK);
        k ^= Zobrist::psq[rook][rf] ^ Zobrist::psq[rook][rt];
    } else if (flag == FLAG_QUEEN_CASTLE) {
        Square rf = (us == WHITE) ? A1 : A8;
        Square rt = (us == WHITE) ? D1 : D8;
        move_piece(rf, rt);
        Piece rook = make_piece(us, ROOK);
        k ^= Zobrist::psq[rook][rf] ^ Zobrist::psq[rook][rt];
    }

    int oldCr = next.castlingRights;
    int newCr = oldCr & CastlingMask[from] & CastlingMask[to];
    if (newCr != oldCr) {
        k ^= Zobrist::castling[oldCr] ^ Zobrist::castling[newCr];
        next.castlingRights = newCr;
    }

    k ^= Zobrist::side;
    stm = them;
    next.key = k;

    ++st;
    ++gamePly;
    st->checkers = compute_checkers();
    history[historyCount++] = k;
}

void Position::undo_move(Move m) {
    stm = ~stm;                 // back to the side that moved
    Color us = stm;
    Square from = from_sq(m), to = to_sq(m);
    int flag = move_flag(m);

    if (is_promotion(m)) {
        remove_piece(to);                       // remove promoted piece
        put_piece(make_piece(us, PAWN), to);    // restore pawn on 'to'
    }

    if (flag == FLAG_KING_CASTLE) {
        Square rf = (us == WHITE) ? H1 : H8;
        Square rt = (us == WHITE) ? F1 : F8;
        move_piece(rt, rf);
    } else if (flag == FLAG_QUEEN_CASTLE) {
        Square rf = (us == WHITE) ? A1 : A8;
        Square rt = (us == WHITE) ? D1 : D8;
        move_piece(rt, rf);
    }

    move_piece(to, from);

    if (is_capture(m)) {
        Square capSq = to;
        if (flag == FLAG_EP_CAPTURE)
            capSq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
        put_piece(st->captured, capSq);
    }

    --st;
    --gamePly;
    --historyCount;
}

void Position::do_null_move() {
    StateInfo& next = *(st + 1);
    next.castlingRights = st->castlingRights;
    next.halfmoveClock = st->halfmoveClock + 1;
    next.epSquare = SQ_NONE;
    next.captured = NO_PIECE;

    U64 k = st->key;
    if (st->epSquare != SQ_NONE) k ^= Zobrist::enpassant[file_of(st->epSquare)];
    k ^= Zobrist::side;

    stm = ~stm;
    next.key = k;

    ++st;
    ++gamePly;
    st->checkers = 0; // side to move is not in check (caller guarantees)
    history[historyCount++] = k;
}

void Position::undo_null_move() {
    stm = ~stm;
    --st;
    --gamePly;
    --historyCount;
}

// Treat the first repetition as a draw (cheap and standard inside search).
bool Position::is_repetition() const {
    int end = historyCount - 1;
    U64 cur = history[end];
    int limit = st->halfmoveClock;
    for (int i = end - 2; i >= 0 && (end - i) <= limit; i -= 2)
        if (history[i] == cur)
            return true;
    return false;
}

bool Position::is_draw() const {
    if (st->halfmoveClock >= 100) return true;
    if (is_repetition()) return true;

    if (byType[PAWN] | byType[ROOK] | byType[QUEEN]) return false;
    int minors = popcount(byType[KNIGHT] | byType[BISHOP]);
    return minors <= 1; // KvK or K+single-minor vs K
}

int Position::game_phase() const {
    int phase = popcount(byType[KNIGHT]) + popcount(byType[BISHOP])
              + popcount(byType[ROOK]) * 2 + popcount(byType[QUEEN]) * 4;
    return phase > 24 ? 24 : phase;
}

// Static Exchange Evaluation: is the move worth at least `threshold`?
// Plays out the capture sequence on `to` with least-valuable-attacker order.
// (No pinned-piece refinement — good enough for ordering and pruning.)
bool Position::see_ge(Move m, int threshold) const {
    if (is_castle(m)) return 0 >= threshold;

    Square from = from_sq(m), to = to_sq(m);

    int swap = see_value(type_of(piece_on(to))) - threshold;
    if (is_ep(m)) swap = see_value(PAWN) - threshold;
    if (swap < 0) return false;

    swap = see_value(type_of(piece_on(from))) - swap;
    if (swap <= 0) return true;

    Bitboard occupied = pieces() ^ square_bb(from) ^ square_bb(to);
    if (is_ep(m)) {
        Square capSq = (color_of(piece_on(from)) == WHITE) ? Square(to - 8) : Square(to + 8);
        occupied ^= square_bb(capSq);
    }
    Color side = color_of(piece_on(from));
    Bitboard attackers = attackers_to(to, occupied) & occupied;
    int res = 1;

    while (true) {
        side = ~side;
        attackers &= occupied;
        Bitboard stmAtt = attackers & byColor[side];
        if (!stmAtt) break;
        res ^= 1;

        Bitboard bb;
        if ((bb = stmAtt & byType[PAWN])) {
            if ((swap = see_value(PAWN) - swap) < res) break;
            occupied ^= lsb_bb(bb);
            attackers |= bishop_attacks(to, occupied) & (byType[BISHOP] | byType[QUEEN]);
        } else if ((bb = stmAtt & byType[KNIGHT])) {
            if ((swap = see_value(KNIGHT) - swap) < res) break;
            occupied ^= lsb_bb(bb);
        } else if ((bb = stmAtt & byType[BISHOP])) {
            if ((swap = see_value(BISHOP) - swap) < res) break;
            occupied ^= lsb_bb(bb);
            attackers |= bishop_attacks(to, occupied) & (byType[BISHOP] | byType[QUEEN]);
        } else if ((bb = stmAtt & byType[ROOK])) {
            if ((swap = see_value(ROOK) - swap) < res) break;
            occupied ^= lsb_bb(bb);
            attackers |= rook_attacks(to, occupied) & (byType[ROOK] | byType[QUEEN]);
        } else if ((bb = stmAtt & byType[QUEEN])) {
            if ((swap = see_value(QUEEN) - swap) < res) break;
            occupied ^= lsb_bb(bb);
            attackers |= (bishop_attacks(to, occupied) & (byType[BISHOP] | byType[QUEEN]))
                       | (rook_attacks(to, occupied) & (byType[ROOK] | byType[QUEEN]));
        } else { // king
            return (attackers & byColor[~side]) ? bool(res ^ 1) : bool(res);
        }
    }
    return bool(res);
}

// Validate an untrusted move (e.g. from a hash hit) against the real board.
bool Position::is_pseudo_legal(Move m) const {
    if (m == MOVE_NONE || m == MOVE_NULL) return false;

    Square from = from_sq(m), to = to_sq(m);
    int flag = move_flag(m);
    Color us = stm;
    Piece pc = board[from];

    if (pc == NO_PIECE || color_of(pc) != us) return false;
    PieceType pt = type_of(pc);

    // Castling: validate fully (rights, emptiness, not through/into check).
    if (is_castle(m)) {
        if (pt != KING || in_check()) return false;
        bool kingSide = (flag == FLAG_KING_CASTLE);
        Square kf = (us == WHITE) ? E1 : E8;
        if (from != kf) return false;
        Bitboard occ = pieces();
        if (us == WHITE) {
            if (kingSide) {
                if (!(st->castlingRights & WHITE_OO)) return false;
                if (occ & (square_bb(F1) | square_bb(G1))) return false;
                if (is_attacked(F1, BLACK) || is_attacked(G1, BLACK)) return false;
                return to == G1;
            } else {
                if (!(st->castlingRights & WHITE_OOO)) return false;
                if (occ & (square_bb(B1) | square_bb(C1) | square_bb(D1))) return false;
                if (is_attacked(D1, BLACK) || is_attacked(C1, BLACK)) return false;
                return to == C1;
            }
        } else {
            if (kingSide) {
                if (!(st->castlingRights & BLACK_OO)) return false;
                if (occ & (square_bb(F8) | square_bb(G8))) return false;
                if (is_attacked(F8, WHITE) || is_attacked(G8, WHITE)) return false;
                return to == G8;
            } else {
                if (!(st->castlingRights & BLACK_OOO)) return false;
                if (occ & (square_bb(B8) | square_bb(C8) | square_bb(D8))) return false;
                if (is_attacked(D8, WHITE) || is_attacked(C8, WHITE)) return false;
                return to == C8;
            }
        }
    }

    // Cannot capture own piece.
    if (board[to] != NO_PIECE && color_of(board[to]) == us) return false;

    bool cap = is_capture(m);
    if (flag == FLAG_EP_CAPTURE) {
        if (pt != PAWN || to != st->epSquare || board[to] != NO_PIECE) return false;
    } else if (cap) {
        if (board[to] == NO_PIECE) return false;
    } else {
        if (board[to] != NO_PIECE) return false;
    }

    if (is_promotion(m)) {
        if (pt != PAWN || relative_rank(us, to) != RANK_8) return false;
    } else if (pt == PAWN && relative_rank(us, to) == RANK_8) {
        return false; // pawn reaching last rank must promote
    }

    if (pt == PAWN) {
        int up = (us == WHITE) ? 8 : -8;
        if (flag == FLAG_EP_CAPTURE || cap) {
            return (PawnAttacks[us][from] & square_bb(to)) != 0;
        } else if (flag == FLAG_DOUBLE_PUSH) {
            Square mid = Square(from + up);
            return relative_rank(us, from) == RANK_2
                && to == Square(from + 2 * up)
                && board[mid] == NO_PIECE && board[to] == NO_PIECE;
        } else {
            return to == Square(from + up) && board[to] == NO_PIECE;
        }
    }

    // Knight / bishop / rook / queen / (non-castle) king
    return (attacks_bb(pt, from, pieces()) & square_bb(to)) != 0;
}

// Does playing m check the opponent? Covers direct, discovered, ep and castling.
bool Position::gives_check(Move m) const {
    Square from = from_sq(m), to = to_sq(m);
    Color us = stm, them = ~stm;
    Square ksq = king_sq(them);
    int flag = move_flag(m);

    if (is_castle(m)) {
        Square rf, rt;
        if (flag == FLAG_KING_CASTLE) { rf = (us == WHITE) ? H1 : H8; rt = (us == WHITE) ? F1 : F8; }
        else { rf = (us == WHITE) ? A1 : A8; rt = (us == WHITE) ? D1 : D8; }
        Bitboard occ = pieces() ^ square_bb(from) ^ square_bb(to) ^ square_bb(rf) ^ square_bb(rt);
        return (rook_attacks(rt, occ) & square_bb(ksq)) != 0;
    }

    Piece pc = board[from];
    PieceType movedType = is_promotion(m) ? promotion_type(m) : type_of(pc);

    Bitboard occ = pieces() ^ square_bb(from);
    if (is_capture(m)) {
        Square capSq = (flag == FLAG_EP_CAPTURE)
                     ? ((us == WHITE) ? Square(to - 8) : Square(to + 8))
                     : to;
        occ ^= square_bb(capSq);
    }
    occ |= square_bb(to);

    // Direct check from the moved/promoted piece on its destination square.
    Bitboard att;
    switch (movedType) {
        case PAWN:   att = PawnAttacks[us][to]; break;
        case KNIGHT: att = KnightAttacks[to]; break;
        case BISHOP: att = bishop_attacks(to, occ); break;
        case ROOK:   att = rook_attacks(to, occ); break;
        case QUEEN:  att = queen_attacks(to, occ); break;
        default:     att = 0; break;
    }
    if (att & square_bb(ksq)) return true;

    // Discovered check: another of our sliders now bears on the king.
    Bitboard diag = (byColor[us] & (byType[BISHOP] | byType[QUEEN])) & ~square_bb(from) & ~square_bb(to);
    Bitboard orth = (byColor[us] & (byType[ROOK] | byType[QUEEN])) & ~square_bb(from) & ~square_bb(to);
    if (bishop_attacks(ksq, occ) & diag) return true;
    if (rook_attacks(ksq, occ) & orth) return true;

    return false;
}

std::string Position::to_string() const {
    std::ostringstream os;
    os << "\n +---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) {
            Piece pc = board[make_square(File(f), Rank(r))];
            char c = '.';
            if (pc != NO_PIECE) {
                const char* sym = "PNBRQK";
                c = sym[type_of(pc)];
                if (color_of(pc) == BLACK) c = std::tolower(c);
            }
            os << " | " << c;
        }
        os << " | " << (r + 1) << "\n +---+---+---+---+---+---+---+---+\n";
    }
    os << "   a   b   c   d   e   f   g   h\n";
    os << "Fen: " << fen() << "\nKey: " << std::hex << key() << std::dec << "\n";
    return os.str();
}

} // namespace ix
