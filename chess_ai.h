#pragma once
#include "chess_core.h"
#include <algorithm>
#include <climits>

// ============================================================
// ChessAI — minimax search with alpha-beta pruning
// ============================================================

class ChessAI {
public:
    ChessAI(int depth = 3) : m_maxDepth(depth) {}

    void SetDepth(int d)       { m_maxDepth = d; }
    int  GetDepth() const      { return m_maxDepth; }
    int  GetNodes() const      { return m_nodes; }

    Move GetBestMove(ChessBoard& board);

private:
    int m_maxDepth = 3;
    int m_nodes = 0;

    // Material evaluation
    static int PieceValue(PieceType t);
    static int PositionBonus(PieceType t, Side side, int row, int col);
    int Evaluate(const ChessBoard& board) const;

    // Move ordering
    static int MoveOrderScore(const ChessBoard& board, const Move& m);
    static void OrderMoves(const ChessBoard& board, std::vector<Move>& moves);

    // Search
    int Minimax(ChessBoard& board, int depth, int alpha, int beta, bool maximizing);
};

// ============================================================
// Piece values (standard Chinese chess)
// ============================================================

inline int ChessAI::PieceValue(PieceType t) {
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

// ============================================================
// Positional bonuses
// ============================================================

inline int ChessAI::PositionBonus(PieceType t, Side side, int row, int col) {
    switch (t) {
    case PAWN:
        return ChessBoard::HasCrossedRiver(row, side) ? 120 : 0;
    case CHARIOT: {
        int b = 0;
        if (col >= 2 && col <= 6) b += 10;
        if (row >= 3 && row <= 6) b += 10;
        return b;
    }
    case HORSE: {
        int b = 0;
        if (col >= 1 && col <= 7) b += 15;
        if (col >= 2 && col <= 6) b += 10;
        if (row >= 2 && row <= 7) b += 10;
        return b;
    }
    case CANNON:
        return (col >= 1 && col <= 7) ? 10 : 0;
    case ADVISOR:
        return (col == 4) ? 10 : 0;
    case ELEPHANT:
        return (col == 2 || col == 6) ? 10 : (col == 0 || col == 4 || col == 8) ? 5 : 0;
    case KING:
        return (col == 4) ? 10 : 0;
    default:
        return 0;
    }
}

// ============================================================
// Board evaluation (from RED's perspective)
// ============================================================

inline int ChessAI::Evaluate(const ChessBoard& board) const {
    int score = 0;
    for (int r = 0; r < ChessBoard::ROWS; r++) {
        for (int c = 0; c < ChessBoard::COLS; c++) {
            const Piece& p = board.At(r, c);
            if (p.IsEmpty()) continue;
            int val = PieceValue(p.type) + PositionBonus(p.type, p.side, r, c);
            score += (p.side == RED) ? val : -val;
        }
    }
    return score;
}

// ============================================================
// Move ordering
// ============================================================

inline int ChessAI::MoveOrderScore(const ChessBoard& board, const Move& m) {
    int s = 0;
    if (!m.captured.IsEmpty()) {
        s += PieceValue(m.captured.type) * 10;
        s -= PieceValue(board.At(m.fromRow, m.fromCol).type);
    }
    if (m.toCol >= 2 && m.toCol <= 6) s += 5;
    return s;
}

inline void ChessAI::OrderMoves(const ChessBoard& board, std::vector<Move>& moves) {
    int n = (int)moves.size();
    for (int i = 0; i < n - 1; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++)
            if (MoveOrderScore(board, moves[j]) > MoveOrderScore(board, moves[best]))
                best = j;
        if (best != i) std::swap(moves[i], moves[best]);
    }
}

// ============================================================
// Minimax with alpha-beta
// ============================================================

inline Move ChessAI::GetBestMove(ChessBoard& board) {
    m_nodes = 0;
    std::vector<Move> moves = board.GetAllLegalMoves();
    if (moves.empty()) return {-1,-1,-1,-1,{EMPTY,BLACK}};

    OrderMoves(board, moves);

    Move bestMove = moves[0];
    int  bestScore = INT_MIN;
    bool isRed = (board.CurrentSide() == RED);

    for (const auto& m : moves) {
        board.ExecuteMove(m);
        int score = isRed ? Minimax(board, m_maxDepth - 1, INT_MIN, INT_MAX, false)
                          : Minimax(board, m_maxDepth - 1, INT_MIN, INT_MAX, true);
        board.UndoMove();

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }
    }
    return bestMove;
}

inline int ChessAI::Minimax(ChessBoard& board, int depth, int alpha, int beta, bool maximizing) {
    m_nodes++;

    std::vector<Move> moves = board.GetAllLegalMoves();
    if (moves.empty()) {
        return maximizing ? -50000 + (m_maxDepth - depth)
                          :  50000 - (m_maxDepth - depth);
    }
    if (depth <= 0) return Evaluate(board);

    OrderMoves(board, moves);

    if (maximizing) {
        int best = INT_MIN;
        for (const auto& m : moves) {
            board.ExecuteMove(m);
            int score = Minimax(board, depth - 1, alpha, beta, false);
            board.UndoMove();
            if (score > best) best = score;
            if (best > alpha) alpha = best;
            if (alpha >= beta) break;
        }
        return best;
    } else {
        int best = INT_MAX;
        for (const auto& m : moves) {
            board.ExecuteMove(m);
            int score = Minimax(board, depth - 1, alpha, beta, true);
            board.UndoMove();
            if (score < best) best = score;
            if (best < beta) beta = best;
            if (alpha >= beta) break;
        }
        return best;
    }
}
