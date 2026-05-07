#pragma once
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>

// ============================================================
// Zobrist hash — keys for incremental board hashing
// ============================================================

static uint64_t s_zkey[8][2][10][9];  // [type][color][row][col]
static uint64_t s_zside = 0;
static bool s_zinit = false;

inline void InitZobrist() {
    if (s_zinit) return;
    s_zinit = true;
    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    auto rnd = [&]() -> uint64_t {
        seed ^= seed >> 12; seed ^= seed << 25;
        seed ^= seed >> 27;
        return seed * 0x2545F4914F6CDD1DULL;
    };
    for (int t = 1; t <= 7; t++)
        for (int s = 0; s <= 1; s++)
            for (int r = 0; r < 10; r++)
                for (int c = 0; c < 9; c++)
                    s_zkey[t][s][r][c] = rnd();
    s_zside = rnd();
}

// ============================================================
// Types
// ============================================================

enum PieceType : int { EMPTY = 0, KING, ADVISOR, ELEPHANT, HORSE, CHARIOT, CANNON, PAWN };
enum Side : int { BLACK = 0, RED = 1 };

inline Side Opponent(Side s) { return (s == RED) ? BLACK : RED; }

struct Piece {
    PieceType type = EMPTY;
    Side side = BLACK;

    bool IsEmpty()    const { return type == EMPTY; }
    bool IsSide(Side s) const { return !IsEmpty() && side == s; }
    bool IsEnemyOf(Side s) const { return !IsEmpty() && side != s; }
};

struct Move {
    int fromRow, fromCol;
    int toRow, toCol;
    Piece captured;
};

// ============================================================
// ChessBoard class — all board logic + move generation + rules
// ============================================================

class ChessBoard {
public:
    static constexpr int ROWS = 10;
    static constexpr int COLS = 9;

    ChessBoard() { Init(); }

    // --- Lifecycle ---
    void Init();

    // --- Accessors ---
    Side CurrentSide()        const { return m_side; }
    int  MoveCount()          const { return m_moves; }
    float RedTime()           const { return m_redTime; }
    float BlackTime()         const { return m_blackTime; }
    void  SetRedTime(float t)       { m_redTime = t; }
    void  SetBlackTime(float t)     { m_blackTime = t; }
    const Piece& At(int r, int c) const { return m_cells[r][c]; }
    bool  HasHistory()        const { return !m_history.empty(); }
    const Move& LastMove()    const { return m_history.back(); }
    int   HistorySize()       const { return (int)m_history.size(); }
    uint64_t Hash()           const { return m_hash; }

    // --- Move generation (non-const: temporarily mutates board for check-test) ---
    std::vector<Move> GetLegalMoves(int row, int col);
    std::vector<Move> GetAllLegalMoves();
    std::vector<Move> GetLegalCaptures();  // only capture moves (for quiescence)
    bool IsMoveLegal(const Move& m);

    // --- Move execution ---
    void ExecuteMove(const Move& m);
    bool UndoMove();

    // --- Game state ---
    bool IsInCheck() const              { return IsKingInCheck(m_side); }
    bool IsInCheck(Side s) const        { return IsKingInCheck(s); }
    bool IsCheckmate();

    bool FindKing(Side s, int& r, int& c) const;

    // --- Static helpers ---
    static bool InBounds(int r, int c)  { return r >= 0 && r < ROWS && c >= 0 && c < COLS; }
    static bool InPalace(int r, int c, Side s);
    static bool HasCrossedRiver(int r, Side s);
    static const char* PieceChar(PieceType t, Side s);

private:
    Piece m_cells[ROWS][COLS];
    Side  m_side = RED;
    std::vector<Move> m_history;
    float m_redTime  = 0;
    float m_blackTime= 0;
    int   m_moves    = 0;
    uint64_t m_hash  = 0;

    // --- Private helpers ---
    bool IsKingInCheck(Side kingSide) const;
    bool KingsAreFacing() const;

    void GenPieceMoves(int r, int c, std::vector<Move>& out) const;

    void GenKingMoves   (int r, int c, Side s, std::vector<Move>& out) const;
    void GenAdvisorMoves(int r, int c, Side s, std::vector<Move>& out) const;
    void GenElephantMoves(int r, int c, Side s, std::vector<Move>& out) const;
    void GenHorseMoves  (int r, int c, Side s, std::vector<Move>& out) const;
    void GenChariotMoves(int r, int c, Side s, std::vector<Move>& out) const;
    void GenCannonMoves (int r, int c, Side s, std::vector<Move>& out) const;
    void GenPawnMoves   (int r, int c, Side s, std::vector<Move>& out) const;
};

// ============================================================
// Static helpers
// ============================================================

inline bool ChessBoard::InPalace(int r, int c, Side s) {
    if (c < 3 || c > 5) return false;
    return (s == BLACK) ? (r >= 0 && r <= 2) : (r >= 7 && r <= 9);
}

inline bool ChessBoard::HasCrossedRiver(int r, Side s) {
    return (s == BLACK) ? (r >= 5) : (r <= 4);
}

inline const char* ChessBoard::PieceChar(PieceType t, Side s) {
    if (s == RED) {
        switch (t) {
            case KING:    return "\xe5\xb8\x85"; // 帅
            case ADVISOR: return "\xe4\xbb\x95"; // 仕
            case ELEPHANT:return "\xe7\x9b\xb8"; // 相
            case HORSE:   return "\xe9\xa9\xac"; // 马
            case CHARIOT: return "\xe8\xbd\xa6"; // 车
            case CANNON:  return "\xe7\x82\xae"; // 炮
            case PAWN:    return "\xe5\x85\xb5"; // 兵
            default:      return "";
        }
    } else {
        switch (t) {
            case KING:    return "\xe5\xb0\x86"; // 将
            case ADVISOR: return "\xe5\xa3\xab"; // 士
            case ELEPHANT:return "\xe8\xb1\xa1"; // 象
            case HORSE:   return "\xe9\xa9\xac"; // 马
            case CHARIOT: return "\xe8\xbd\xa6"; // 车
            case CANNON:  return "\xe7\x82\xae"; // 炮
            case PAWN:    return "\xe5\x8d\x92"; // 卒
            default:      return "";
        }
    }
}

// ============================================================
// Board lifecycle
// ============================================================

inline void ChessBoard::Init() {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            m_cells[r][c] = {EMPTY, BLACK};

    // Black (top)
    m_cells[0][0] = {CHARIOT, BLACK}; m_cells[0][1] = {HORSE,   BLACK};
    m_cells[0][2] = {ELEPHANT,BLACK}; m_cells[0][3] = {ADVISOR, BLACK};
    m_cells[0][4] = {KING,    BLACK}; m_cells[0][5] = {ADVISOR, BLACK};
    m_cells[0][6] = {ELEPHANT,BLACK}; m_cells[0][7] = {HORSE,   BLACK};
    m_cells[0][8] = {CHARIOT, BLACK};
    m_cells[2][1] = {CANNON,  BLACK}; m_cells[2][7] = {CANNON,  BLACK};
    m_cells[3][0] = {PAWN,    BLACK}; m_cells[3][2] = {PAWN,    BLACK};
    m_cells[3][4] = {PAWN,    BLACK}; m_cells[3][6] = {PAWN,    BLACK};
    m_cells[3][8] = {PAWN,    BLACK};

    // Red (bottom)
    m_cells[9][0] = {CHARIOT, RED}; m_cells[9][1] = {HORSE,   RED};
    m_cells[9][2] = {ELEPHANT,RED}; m_cells[9][3] = {ADVISOR, RED};
    m_cells[9][4] = {KING,    RED}; m_cells[9][5] = {ADVISOR, RED};
    m_cells[9][6] = {ELEPHANT,RED}; m_cells[9][7] = {HORSE,   RED};
    m_cells[9][8] = {CHARIOT, RED};
    m_cells[7][1] = {CANNON,  RED}; m_cells[7][7] = {CANNON,  RED};
    m_cells[6][0] = {PAWN,    RED}; m_cells[6][2] = {PAWN,    RED};
    m_cells[6][4] = {PAWN,    RED}; m_cells[6][6] = {PAWN,    RED};
    m_cells[6][8] = {PAWN,    RED};

    m_side = RED;
    m_history.clear();
    m_redTime = m_blackTime = 0;
    m_moves = 0;

    // Build initial Zobrist hash
    InitZobrist();
    m_hash = 0;
    if (m_side == BLACK) m_hash ^= s_zside;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (!m_cells[r][c].IsEmpty())
                m_hash ^= s_zkey[m_cells[r][c].type][m_cells[r][c].side][r][c];
}

// ============================================================
// Piece move generators (pseudo-legal)
// ============================================================

inline void ChessBoard::GenKingMoves(int r, int c, Side s, std::vector<Move>& out) const {
    const int dr[] = {-1, 1, 0, 0};
    const int dc[] = {0, 0, -1, 1};
    for (int i = 0; i < 4; i++) {
        int nr = r + dr[i], nc = c + dc[i];
        if (InPalace(nr, nc, s) && !m_cells[nr][nc].IsSide(s))
            out.push_back({r, c, nr, nc, m_cells[nr][nc]});
    }
}

inline void ChessBoard::GenAdvisorMoves(int r, int c, Side s, std::vector<Move>& out) const {
    const int dr[] = {-1,-1, 1, 1};
    const int dc[] = {-1, 1,-1, 1};
    for (int i = 0; i < 4; i++) {
        int nr = r + dr[i], nc = c + dc[i];
        if (InPalace(nr, nc, s) && !m_cells[nr][nc].IsSide(s))
            out.push_back({r, c, nr, nc, m_cells[nr][nc]});
    }
}

inline void ChessBoard::GenElephantMoves(int r, int c, Side s, std::vector<Move>& out) const {
    const int dr[] = {-2,-2, 2, 2};
    const int dc[] = {-2, 2,-2, 2};
    for (int i = 0; i < 4; i++) {
        int nr = r + dr[i], nc = c + dc[i];
        if (!InBounds(nr, nc)) continue;
        if (HasCrossedRiver(nr, s)) continue;
        int eyeR = r + dr[i]/2, eyeC = c + dc[i]/2;
        if (!m_cells[eyeR][eyeC].IsEmpty()) continue;
        if (!m_cells[nr][nc].IsSide(s))
            out.push_back({r, c, nr, nc, m_cells[nr][nc]});
    }
}

inline void ChessBoard::GenHorseMoves(int r, int c, Side s, std::vector<Move>& out) const {
    // {legDr, legDc, destDr, destDc}
    static const int moves[8][4] = {
        {-1,0,-2,-1},{-1,0,-2,1},{1,0,2,-1},{1,0,2,1},
        {0,-1,-1,-2},{0,-1,1,-2},{0,1,-1,2},{0,1,1,2}
    };
    for (int i = 0; i < 8; i++) {
        int lr = r + moves[i][0], lc = c + moves[i][1];
        if (!InBounds(lr, lc) || !m_cells[lr][lc].IsEmpty()) continue;
        int nr = r + moves[i][2], nc = c + moves[i][3];
        if (InBounds(nr, nc) && !m_cells[nr][nc].IsSide(s))
            out.push_back({r, c, nr, nc, m_cells[nr][nc]});
    }
}

inline void ChessBoard::GenChariotMoves(int r, int c, Side s, std::vector<Move>& out) const {
    const int dr[] = {-1,1,0,0}, dc[] = {0,0,-1,1};
    for (int d = 0; d < 4; d++) {
        for (int k = 1; ; k++) {
            int nr = r + dr[d]*k, nc = c + dc[d]*k;
            if (!InBounds(nr, nc)) break;
            if (m_cells[nr][nc].IsEmpty()) {
                out.push_back({r, c, nr, nc, {EMPTY,BLACK}});
            } else {
                if (m_cells[nr][nc].IsEnemyOf(s))
                    out.push_back({r, c, nr, nc, m_cells[nr][nc]});
                break;
            }
        }
    }
}

inline void ChessBoard::GenCannonMoves(int r, int c, Side s, std::vector<Move>& out) const {
    const int dr[] = {-1,1,0,0}, dc[] = {0,0,-1,1};
    for (int d = 0; d < 4; d++) {
        // non-capture slide
        for (int k = 1; ; k++) {
            int nr = r + dr[d]*k, nc = c + dc[d]*k;
            if (!InBounds(nr, nc)) break;
            if (m_cells[nr][nc].IsEmpty())
                out.push_back({r, c, nr, nc, {EMPTY,BLACK}});
            else break;
        }
        // capture: jump exactly one screen
        bool mount = false;
        for (int k = 1; ; k++) {
            int nr = r + dr[d]*k, nc = c + dc[d]*k;
            if (!InBounds(nr, nc)) break;
            if (!mount) {
                if (!m_cells[nr][nc].IsEmpty()) mount = true;
            } else {
                if (!m_cells[nr][nc].IsEmpty()) {
                    if (m_cells[nr][nc].IsEnemyOf(s))
                        out.push_back({r, c, nr, nc, m_cells[nr][nc]});
                    break;
                }
            }
        }
    }
}

inline void ChessBoard::GenPawnMoves(int r, int c, Side s, std::vector<Move>& out) const {
    int fw = (s == BLACK) ? 1 : -1;
    int nr = r + fw, nc = c;
    if (InBounds(nr, nc) && !m_cells[nr][nc].IsSide(s))
        out.push_back({r, c, nr, nc, m_cells[nr][nc]});
    if (HasCrossedRiver(r, s)) {
        for (int dc = -1; dc <= 1; dc += 2) {
            nc = c + dc;
            if (InBounds(r, nc) && !m_cells[r][nc].IsSide(s))
                out.push_back({r, c, r, nc, m_cells[r][nc]});
        }
    }
}

inline void ChessBoard::GenPieceMoves(int r, int c, std::vector<Move>& out) const {
    const Piece& p = m_cells[r][c];
    if (p.IsEmpty()) return;
    switch (p.type) {
        case KING:    GenKingMoves(r, c, p.side, out);    break;
        case ADVISOR: GenAdvisorMoves(r, c, p.side, out);  break;
        case ELEPHANT:GenElephantMoves(r, c, p.side, out); break;
        case HORSE:   GenHorseMoves(r, c, p.side, out);    break;
        case CHARIOT: GenChariotMoves(r, c, p.side, out);  break;
        case CANNON:  GenCannonMoves(r, c, p.side, out);   break;
        case PAWN:    GenPawnMoves(r, c, p.side, out);     break;
        default: break;
    }
}

// ============================================================
// Check detection
// ============================================================

inline bool ChessBoard::KingsAreFacing() const {
    int rR, cR, rB, cB;
    if (!FindKing(RED, rR, cR) || !FindKing(BLACK, rB, cB)) return false;
    if (cR != cB) return false;
    int lo = rR < rB ? rR : rB, hi = rR > rB ? rR : rB;
    for (int r = lo + 1; r < hi; r++)
        if (!m_cells[r][cR].IsEmpty()) return false;
    return true;
}

inline bool ChessBoard::IsKingInCheck(Side kingSide) const {
    int kR, kC;
    if (!FindKing(kingSide, kR, kC)) return true;

    Side enemy = Opponent(kingSide);

    if (KingsAreFacing()) return true;

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const Piece& p = m_cells[r][c];
            if (p.IsEmpty() || p.side != enemy) continue;

            int dr = kR - r, dc = kC - c;
            switch (p.type) {
            case KING: break; // handled by KingsAreFacing
            case ADVISOR:
                if (abs(dr) == 1 && abs(dc) == 1) return true;
                break;
            case ELEPHANT:
                if (abs(dr) == 2 && abs(dc) == 2 &&
                    m_cells[r + dr/2][c + dc/2].IsEmpty()) return true;
                break;
            case HORSE:
                if ((abs(dr) == 2 && abs(dc) == 1) || (abs(dr) == 1 && abs(dc) == 2)) {
                    int legR = r + (abs(dr) == 2 ? dr/2 : 0);
                    int legC = c + (abs(dc) == 2 ? dc/2 : 0);
                    if (m_cells[legR][legC].IsEmpty()) return true;
                }
                break;
            case CHARIOT:
                if (r == kR) {
                    int lo = c<kC?c:kC, hi = c>kC?c:kC;
                    bool blocked = false;
                    for (int cc = lo+1; cc < hi; cc++)
                        if (!m_cells[r][cc].IsEmpty()) { blocked=true; break; }
                    if (!blocked) return true;
                } else if (c == kC) {
                    int lo = r<kR?r:kR, hi = r>kR?r:kR;
                    bool blocked = false;
                    for (int rr = lo+1; rr < hi; rr++)
                        if (!m_cells[rr][c].IsEmpty()) { blocked=true; break; }
                    if (!blocked) return true;
                }
                break;
            case CANNON:
                if (r == kR) {
                    int lo = c<kC?c:kC, hi = c>kC?c:kC, cnt = 0;
                    for (int cc = lo+1; cc < hi; cc++)
                        if (!m_cells[r][cc].IsEmpty()) cnt++;
                    if (cnt == 1) return true;
                } else if (c == kC) {
                    int lo = r<kR?r:kR, hi = r>kR?r:kR, cnt = 0;
                    for (int rr = lo+1; rr < hi; rr++)
                        if (!m_cells[rr][c].IsEmpty()) cnt++;
                    if (cnt == 1) return true;
                }
                break;
            case PAWN:
                if (c == kC) {
                    int fw = (enemy == BLACK) ? 1 : -1;
                    if (dr == fw) return true;
                }
                if (HasCrossedRiver(r, enemy) && r == kR && abs(dc) == 1) return true;
                break;
            default: break;
            }
        }
    }
    return false;
}

// ============================================================
// Move legality
// ============================================================

inline bool ChessBoard::IsMoveLegal(const Move& m) {
    Piece mover = m_cells[m.fromRow][m.fromCol];
    Piece cap   = m_cells[m.toRow][m.toCol];

    m_cells[m.toRow][m.toCol] = mover;
    m_cells[m.fromRow][m.fromCol] = {EMPTY, BLACK};

    bool legal = !IsKingInCheck(mover.side);

    m_cells[m.fromRow][m.fromCol] = mover;
    m_cells[m.toRow][m.toCol] = cap;
    return legal;
}

inline std::vector<Move> ChessBoard::GetLegalMoves(int row, int col) {
    std::vector<Move> pseudo, legal;
    GenPieceMoves(row, col, pseudo);
    for (const auto& m : pseudo)
        if (IsMoveLegal(m))
            legal.push_back(m);
    return legal;
}

inline std::vector<Move> ChessBoard::GetAllLegalMoves() {
    std::vector<Move> all;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (m_cells[r][c].IsSide(m_side))
                for (const auto& m : GetLegalMoves(r, c))
                    all.push_back(m);
    return all;
}

inline std::vector<Move> ChessBoard::GetLegalCaptures() {
    std::vector<Move> caps;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (m_cells[r][c].IsSide(m_side)) {
                std::vector<Move> pseudo;
                GenPieceMoves(r, c, pseudo);
                for (const auto& m : pseudo)
                    if (!m.captured.IsEmpty() && IsMoveLegal(m))
                        caps.push_back(m);
            }
    return caps;
}

// ============================================================
// Move execution
// ============================================================

inline void ChessBoard::ExecuteMove(const Move& m) {
    const Piece& mover = m_cells[m.fromRow][m.fromCol];
    const Piece  cap   = m_cells[m.toRow][m.toCol];

    // XOR out old positions
    m_hash ^= s_zkey[mover.type][mover.side][m.fromRow][m.fromCol];
    if (!cap.IsEmpty())
        m_hash ^= s_zkey[cap.type][cap.side][m.toRow][m.toCol];

    // Move
    m_cells[m.toRow][m.toCol] = mover;
    m_cells[m.fromRow][m.fromCol] = {EMPTY, BLACK};

    // XOR in new position + toggle side
    m_hash ^= s_zkey[mover.type][mover.side][m.toRow][m.toCol];
    m_hash ^= s_zside;

    m_history.push_back(m);
    m_moves++;
    m_side = Opponent(m_side);
}

inline bool ChessBoard::UndoMove() {
    if (m_history.empty()) return false;
    const Move& m = m_history.back();
    const Piece& mover = m_cells[m.toRow][m.toCol];

    // XOR out current position of moved piece
    m_hash ^= s_zkey[mover.type][mover.side][m.toRow][m.toCol];
    if (!m.captured.IsEmpty())
        m_hash ^= s_zkey[m.captured.type][m.captured.side][m.toRow][m.toCol];

    // Undo
    m_cells[m.fromRow][m.fromCol] = mover;
    m_cells[m.toRow][m.toCol] = m.captured;

    // XOR back original position + toggle side
    m_hash ^= s_zkey[mover.type][mover.side][m.fromRow][m.fromCol];
    m_hash ^= s_zside;

    m_history.pop_back();
    m_moves--;
    m_side = Opponent(m_side);
    return true;
}

// ============================================================
// King / checkmate
// ============================================================

inline bool ChessBoard::FindKing(Side s, int& r, int& c) const {
    for (int rr = 0; rr < ROWS; rr++)
        for (int cc = 0; cc < COLS; cc++)
            if (m_cells[rr][cc].type == KING && m_cells[rr][cc].side == s)
                { r = rr; c = cc; return true; }
    return false;
}

inline bool ChessBoard::IsCheckmate() {
    return GetAllLegalMoves().empty();
}
