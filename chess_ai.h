#pragma once
#include "chess_core.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <ctime>
#include <vector>

// ============================================================
// Transposition Table
// ============================================================

class TransTable {
public:
    static constexpr int SIZE = 1 << 17; // 131072 entries
    static constexpr int MASK = SIZE - 1;

    struct Entry {
        uint64_t hash = 0;
        int  depth    = -1;
        int  score    = 0;
        int  flag     = 0;       // 0=empty, 1=EXACT, 2=ALPHA, 3=BETA
        Move bestMove = {-1,-1,-1,-1,{EMPTY,BLACK}};

        bool IsValid()  const { return flag != 0; }
        bool IsExact()  const { return flag == 1; }
        bool IsUpper()  const { return flag == 2; }
        bool IsLower()  const { return flag == 3; }
    };

    TransTable() : m_table(SIZE) {}

    void Clear() { memset(m_table.data(), 0, SIZE * sizeof(Entry)); }

    Entry* Probe(uint64_t hash) {
        Entry& e = m_table[hash & MASK];
        return (e.hash == hash) ? &e : nullptr;
    }

    void Store(uint64_t hash, int depth, int score, int flag, const Move& best) {
        Entry& e = m_table[hash & MASK];
        if (e.hash != hash || depth >= e.depth) {
            e.hash = hash; e.depth = depth;
            e.score = score; e.flag = flag; e.bestMove = best;
        }
    }

    int  Size()  const { return SIZE; }

private:
    std::vector<Entry> m_table;
};

// ============================================================
// ChessAI — iterative deepening + TT + alpha-beta + quiescence
// ============================================================

class ChessAI {
public:
    static constexpr int MAX_DEPTH = 24;

    ChessAI() { ClearKillers(); }

    void SetTimeLimit(double seconds) { m_timeLimit = seconds; }
    void SetMaxDepth(int d)           { m_maxDepth = d; }
    int  GetNodes()    const { return m_nodes; }
    int  GetQNodes()   const { return m_qnodes; }
    double LastTime()  const { return m_lastTime; }

    Move GetBestMove(ChessBoard& board);

private:
    double m_timeLimit = 3.0;       // seconds per move
    int    m_maxDepth  = 12;        // absolute cap (iterative deepening will stop earlier)
    int    m_nodes     = 0;
    int    m_qnodes    = 0;
    double m_lastTime  = 0;
    bool   m_timeout   = false;
    clock_t m_start    = 0;

    TransTable m_tt;

    // --- Killer heuristic ---
    Move m_k1[MAX_DEPTH];
    Move m_k2[MAX_DEPTH];

    void ClearKillers() {
        Move inv = {-1,-1,-1,-1,{EMPTY,BLACK}};
        for (int i = 0; i < MAX_DEPTH; i++) m_k1[i] = m_k2[i] = inv;
    }

    void StoreKiller(const Move& m, int ply) {
        if (!m.captured.IsEmpty()) return;
        if (SameMove(m, m_k1[ply])) return;
        m_k2[ply] = m_k1[ply];
        m_k1[ply] = m;
    }

    static bool SameMove(const Move& a, const Move& b) {
        return a.fromRow == b.fromRow && a.fromCol == b.fromCol
            && a.toRow   == b.toRow   && a.toCol   == b.toCol;
    }

    bool IsTimeUp() {
        return (double)(clock() - m_start) / CLOCKS_PER_SEC >= m_timeLimit;
    }

    // --- Evaluation ---
    static int PieceVal(PieceType t);
    static int PiecePos(PieceType t, Side side, int row, int col, const ChessBoard& board);
    int Evaluate(const ChessBoard& board) const;

    // --- Search ---
    int Quiesce(ChessBoard& board, int alpha, int beta, bool maximizing);
    int Minimax(ChessBoard& board, int depth, int alpha, int beta, bool maximizing);

    // --- Move ordering ---
    int MoveScore(const ChessBoard& board, const Move& m, int ply,
                  const Move* ttMove) const;
    void OrderMoves(const ChessBoard& board, std::vector<Move>& moves, int ply,
                    const Move* ttMove);
};

// ============================================================
// Piece values + positional evaluation
// ============================================================

inline int ChessAI::PieceVal(PieceType t) {
    switch (t) {
        case KING:    return 10000;
        case CHARIOT: return 900;
        case CANNON:  return 450;
        case HORSE:   return 400;
        case ELEPHANT:return 200;
        case ADVISOR: return 200;
        case PAWN:    return 100;
        default:      return 0;
    }
}

inline int ChessAI::PiecePos(PieceType t, Side side, int row, int col,
                              const ChessBoard& board) {
    int prog = (side == RED) ? (9 - row) : row;   // distance from own back rank
    int cent = 4 - abs(col - 4);                   // 0=edge … 4=center

    switch (t) {
    case PAWN: {
        if (prog < 5) return 0;
        int v = (prog - 4) * 25 + cent * 6;
        if (prog >= 7 && col >= 3 && col <= 5) v += 40;
        return v;
    }
    case CHARIOT: {
        int v = prog * 4 + cent * 5;
        if (prog >= 5) v += 12;
        // Open / semi-open file detection
        bool ownBlock = false, oppBlock = false;
        Side opp = Opponent(side);
        for (int rr = 0; rr < 10; rr++) {
            if (rr == row) continue;
            const Piece& p = board.At(rr, col);
            if (!p.IsEmpty()) {
                if (p.side == side)      ownBlock = true;
                else if (p.side == opp)  oppBlock = true;
            }
        }
        if (!ownBlock)        v += 30;
        else if (!oppBlock)   v += 15;
        return v;
    }
    case HORSE: {
        int v = cent * 8;
        if (col == 0 || col == 8) v -= 25;
        if (prog >= 2 && prog <= 6) v += 12;
        if (prog >= 7)  v += 10;
        if (prog <= 1)  v -= 8;
        return v;
    }
    case CANNON:
        return cent * 4 + ((prog >= 2 && prog <= 6) ? 10 : 0);
    case ADVISOR:
        return (col == 4) ? 12 : (col == 3 || col == 5) ? 4 : 0;
    case ELEPHANT: {
        if (col == 2 || col == 6) return 15;
        if (col == 4) return 8;
        if (col == 0 || col == 8) return -5;
        return 0;
    }
    case KING:
        return (col == 4 && prog <= 2) ? 12 : (col == 3 || col == 5) ? 5 : 0;
    default: return 0;
    }
}

inline int ChessAI::Evaluate(const ChessBoard& board) const {
    int score = 0;
    for (int r = 0; r < ChessBoard::ROWS; r++)
        for (int c = 0; c < ChessBoard::COLS; c++) {
            const Piece& p = board.At(r, c);
            if (p.IsEmpty()) continue;
            int v = PieceVal(p.type) + PiecePos(p.type, p.side, r, c, board);
            score += (p.side == RED) ? v : -v;
        }
    return score;
}

// ============================================================
// Move ordering
// ============================================================

inline int ChessAI::MoveScore(const ChessBoard& board, const Move& m, int ply,
                               const Move* ttMove) const {
    // TT move gets absolute top priority
    if (ttMove && SameMove(m, *ttMove)) return INT_MAX;

    int s = 0;

    // MVV-LVA captures
    if (!m.captured.IsEmpty())
        s = 20000 + PieceVal(m.captured.type) * 10
                  - PieceVal(board.At(m.fromRow, m.fromCol).type);

    // Killers
    if (SameMove(m, m_k1[ply]))      s += 10000;
    else if (SameMove(m, m_k2[ply])) s += 9000;

    // Center + advance tie-breaks
    s += 4 - abs(m.toCol - 4);
    Side side = board.At(m.fromRow, m.fromCol).side;
    int dRow = (side == RED) ? (m.fromRow - m.toRow) : (m.toRow - m.fromRow);
    if (dRow > 0) s += dRow;

    return s;
}

inline void ChessAI::OrderMoves(const ChessBoard& board,
                                 std::vector<Move>& moves, int ply,
                                 const Move* ttMove) {
    int n = (int)moves.size();
    // Bubble sort by score (small lists, fine)
    for (int i = 0; i < n - 1; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++)
            if (MoveScore(board, moves[j], ply, ttMove)
                > MoveScore(board, moves[best], ply, ttMove))
                best = j;
        if (best != i) std::swap(moves[i], moves[best]);
    }
}

// ============================================================
// Quiescence search
// ============================================================

inline int ChessAI::Quiesce(ChessBoard& board, int alpha, int beta, bool maximizing) {
    m_qnodes++;

    int standPat = Evaluate(board);
    if (maximizing) {
        if (standPat >= beta)  return beta;
        if (standPat > alpha)  alpha = standPat;
    } else {
        if (standPat <= alpha) return alpha;
        if (standPat < beta)   beta  = standPat;
    }

    std::vector<Move> caps = board.GetLegalCaptures();
    OrderMoves(board, caps, 0, nullptr);

    for (const auto& m : caps) {
        int gain = PieceVal(m.captured.type) + 200;
        if (maximizing && standPat + gain < alpha) continue;
        if (!maximizing && standPat - gain > beta) continue;

        board.ExecuteMove(m);
        int score = Quiesce(board, alpha, beta, !maximizing);
        board.UndoMove();

        if (maximizing) {
            if (score > alpha) alpha = score;
            if (alpha >= beta) return beta;
        } else {
            if (score < beta) beta = score;
            if (alpha >= beta) return alpha;
        }
    }
    return maximizing ? alpha : beta;
}

// ============================================================
// Minimax with TT, killers, quiescence
// ============================================================

inline int ChessAI::Minimax(ChessBoard& board, int depth, int alpha, int beta,
                             bool maximizing) {
    m_nodes++;

    // Time check every 2048 nodes
    if ((m_nodes & 2047) == 0 && IsTimeUp()) {
        m_timeout = true;
        return Evaluate(board);
    }

    // TT probe
    uint64_t hash = board.Hash();
    auto* entry = m_tt.Probe(hash);
    if (entry && entry->depth >= depth) {
        if (entry->IsExact()) return entry->score;
        if (entry->IsUpper() && entry->score <= alpha) return alpha;
        if (entry->IsLower() && entry->score >= beta)  return beta;
    }

    // Quiescence at leaves
    if (depth <= 0)
        return Quiesce(board, alpha, beta, maximizing);

    std::vector<Move> moves = board.GetAllLegalMoves();
    if (moves.empty())
        return maximizing ? -50000 + (m_maxDepth - depth)
                          :  50000 - (m_maxDepth - depth);

    // Order moves (TT move first if available)
    const Move* ttMove = (entry && entry->bestMove.fromRow >= 0)
                         ? &entry->bestMove : nullptr;
    OrderMoves(board, moves, depth, ttMove);

    Move bestLocal = moves[0];
    int  bestScore;
    int  origAlpha = alpha;

    if (maximizing) {
        bestScore = INT_MIN;
        for (const auto& m : moves) {
            board.ExecuteMove(m);
            int score = Minimax(board, depth - 1, alpha, beta, false);
            board.UndoMove();
            if (score > bestScore) { bestScore = score; bestLocal = m; }
            if (bestScore > alpha)  alpha = bestScore;
            if (alpha >= beta)     { StoreKiller(m, depth); break; }
        }
    } else {
        bestScore = INT_MAX;
        for (const auto& m : moves) {
            board.ExecuteMove(m);
            int score = Minimax(board, depth - 1, alpha, beta, true);
            board.UndoMove();
            if (score < bestScore) { bestScore = score; bestLocal = m; }
            if (bestScore < beta)   beta   = bestScore;
            if (alpha >= beta)     { StoreKiller(m, depth); break; }
        }
    }

    // Store in TT
    int flag;
    if (bestScore <= origAlpha)      flag = 2; // upper bound
    else if (bestScore >= beta)      flag = 3; // lower bound
    else                             flag = 1; // exact
    m_tt.Store(hash, depth, bestScore, flag, bestLocal);

    return bestScore;
}

// ============================================================
// Root: iterative deepening with time control
// ============================================================

inline Move ChessAI::GetBestMove(ChessBoard& board) {
    m_nodes  = 0;
    m_qnodes = 0;
    m_timeout = false;
    m_start   = clock();
    ClearKillers();

    std::vector<Move> moves = board.GetAllLegalMoves();
    if (moves.empty()) return {-1,-1,-1,-1,{EMPTY,BLACK}};
    if (moves.size() == 1) return moves[0];  // only one legal move

    // Preliminary ordering at root
    OrderMoves(board, moves, m_maxDepth, nullptr);

    Move  bestMove   = moves[0];
    bool  isRed      = (board.CurrentSide() == RED);
    double totalTime = 0;
    int    lastDepth = 0;

    // Iterative deepening: depth 1 → 2 → ... → m_maxDepth
    for (int depth = 1; depth <= m_maxDepth; depth++) {
        m_timeout = false;
        int  iterBestScore = INT_MIN;
        Move iterBest      = moves[0];

        for (const auto& m : moves) {
            board.ExecuteMove(m);
            int score = isRed ? Minimax(board, depth - 1, INT_MIN, INT_MAX, false)
                              : Minimax(board, depth - 1, INT_MIN, INT_MAX, true);
            board.UndoMove();

            if (m_timeout) break;

            if (score > iterBestScore) {
                iterBestScore = score;
                iterBest      = m;
            }
        }

        totalTime = (double)(clock() - m_start) / CLOCKS_PER_SEC;
        lastDepth = depth;

        if (!m_timeout) {
            bestMove = iterBest;
            // Re-order for next iteration: best move first
            for (auto& m : moves)
                if (SameMove(m, bestMove)) { std::swap(m, moves[0]); break; }
        }

        if (m_timeout || totalTime >= m_timeLimit) break;
    }

    m_lastTime = totalTime;
    return bestMove;
}
