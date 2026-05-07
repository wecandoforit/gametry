#define GAMELIB_IMPLEMENTATION
#include "GameLib.h"
#include "chess_core.h"
#include "chess_ai.h"
#include <vector>
#include <cstdio>

// ============================================================
// Layout
// ============================================================
const int CELL      = 60;
const int BOARD_X   = 60;
const int BOARD_Y   = 55;
const int PIECE_R   = 26;
const int WIN_W     = 710;
const int WIN_H     = 700;

// ============================================================
// Colors
// ============================================================
const uint32_t C_BOARD_BG    = COLOR_RGB(220, 190, 140);
const uint32_t C_BOARD_LINE  = COLOR_RGB(60, 40, 20);
const uint32_t C_PIECE_BG    = COLOR_RGB(252, 240, 210);
const uint32_t C_PIECE_RING  = COLOR_RGB(70, 50, 30);
const uint32_t C_RED_TEXT    = COLOR_RGB(190, 30, 30);
const uint32_t C_BLK_TEXT    = COLOR_RGB(20, 20, 20);
const uint32_t C_SEL_RED     = COLOR_RGB(230, 55, 55);  // red-side selection
const uint32_t C_SEL_BLACK   = COLOR_RGB(180, 150, 50); // black-side selection (gold)
const uint32_t C_VALID_DOT   = COLOR_RGB(80, 180, 80);
const uint32_t C_LAST_MOVE   = COLOR_RGB(100, 160, 220);
const uint32_t C_HINT        = COLOR_RGB(255, 140, 0);
const uint32_t C_UI_BG       = COLOR_RGB(50, 40, 30);
const uint32_t C_UI_TEXT     = COLOR_RGB(220, 210, 190);
const uint32_t C_CHECK_WARN  = COLOR_RGB(255, 60, 60);
const uint32_t C_MSG_BG      = COLOR_RGB(40, 30, 20);
const uint32_t C_MSG_TEXT    = COLOR_RGB(255, 200, 80);

// ============================================================
// Sound
// ============================================================
namespace Sound {
    inline void Move(GameLib& g)    { g.PlayBeep(523, 60, 1, 55); }
    inline void Capture(GameLib& g) { g.PlayBeep(180, 130, 1, 65); }
    inline void Check(GameLib& g)   { g.PlayBeep(880, 180, 1, 70); }
    inline void Click(GameLib& g)   { g.PlayBeep(660, 40, 1, 40); }
    inline void GameOver(GameLib& g) {
        g.PlayBeep(523, 120, 1, 70);
        g.PlayBeep(659, 120, 1, 70);
        g.PlayBeep(784, 150, 1, 70);
        g.PlayBeep(1047, 250, 1, 75);
    }
}

// ============================================================
// Coordinate helpers
// ============================================================
inline int ScrX(int col) { return BOARD_X + col * CELL; }
inline int ScrY(int row) { return BOARD_Y + row * CELL; }

inline bool ScreenToBoard(int sx, int sy, int& row, int& col) {
    col = (sx - BOARD_X + CELL / 2) / CELL;
    row = (sy - BOARD_Y + CELL / 2) / CELL;
    return ChessBoard::InBounds(row, col);
}

inline bool HitIntersection(int sx, int sy, int row, int col) {
    int dx = sx - ScrX(col), dy = sy - ScrY(row);
    return dx*dx + dy*dy <= PIECE_R * PIECE_R;
}

// ============================================================
// Rendering: Board
// ============================================================
void DrawBoard(GameLib& g) {
    g.Clear(C_BOARD_BG);

    // Horizontal lines
    for (int r = 0; r < 10; r++)
        g.DrawLine(ScrX(0), ScrY(r), ScrX(8), ScrY(r), C_BOARD_LINE);

    // Vertical lines (break at river for inner columns)
    for (int c = 0; c < 9; c++) {
        int x = ScrX(c);
        if (c == 0 || c == 8)
            g.DrawLine(x, ScrY(0), x, ScrY(9), C_BOARD_LINE);
        else {
            g.DrawLine(x, ScrY(0), x, ScrY(4), C_BOARD_LINE);
            g.DrawLine(x, ScrY(5), x, ScrY(9), C_BOARD_LINE);
        }
    }

    // Palace diagonals
    g.DrawLine(ScrX(3), ScrY(0), ScrX(5), ScrY(2), C_BOARD_LINE);
    g.DrawLine(ScrX(5), ScrY(0), ScrX(3), ScrY(2), C_BOARD_LINE);
    g.DrawLine(ScrX(3), ScrY(7), ScrX(5), ScrY(9), C_BOARD_LINE);
    g.DrawLine(ScrX(5), ScrY(7), ScrX(3), ScrY(9), C_BOARD_LINE);

    // River text
    g.DrawTextFont(ScrX(1) + 18, ScrY(4) + 10, "楚  河", C_BOARD_LINE, 22);
    g.DrawTextFont(ScrX(5) + 10, ScrY(4) + 10, "汉  界", C_BOARD_LINE, 22);
}

// ============================================================
// Rendering: Single piece
// ============================================================
void DrawPiece(GameLib& g, int row, int col, const Piece& p, uint32_t hilite = 0) {
    int cx = ScrX(col), cy = ScrY(row);

    // Highlight halo
    if (hilite) {
        g.FillCircle(cx, cy, PIECE_R + 5, hilite);
        g.FillCircle(cx, cy, PIECE_R + 3, C_BOARD_BG); // gap between halo and piece
    }

    // Body
    g.FillCircle(cx, cy, PIECE_R,     C_PIECE_BG);
    g.DrawCircle(cx, cy, PIECE_R,     C_PIECE_RING);
    g.DrawCircle(cx, cy, PIECE_R - 3, C_PIECE_RING);

    // Character
    const char* ch = ChessBoard::PieceChar(p.type, p.side);
    uint32_t tc = (p.side == RED) ? C_RED_TEXT : C_BLK_TEXT;
    int fs = 28;
    int tw = g.GetTextWidthFont(ch, fs);
    int th = g.GetTextHeightFont(ch, fs);
    g.DrawTextFont(cx - tw/2, cy - th/2, ch, tc, fs);
}

// ============================================================
// Rendering: All pieces + overlays
// ============================================================
void DrawPieces(GameLib& g, const ChessBoard& board,
                int selR, int selC,
                const std::vector<Move>& validMoves,
                const Move* lastMove,
                const Move* hintMove,
                bool inCheck)
{
    Side curSide = board.CurrentSide();
    uint32_t selColor = (curSide == RED) ? C_SEL_RED : C_SEL_BLACK;

    for (int r = 0; r < 10; r++) {
        for (int c = 0; c < 9; c++) {
            if (board.At(r, c).IsEmpty()) continue;

            uint32_t hl = 0;
            if (r == selR && c == selC)
                hl = selColor;
            else if (inCheck && board.At(r, c).type == KING && board.At(r, c).side == curSide)
                hl = C_CHECK_WARN;

            DrawPiece(g, r, c, board.At(r, c), hl);
        }
    }

    // Valid-move dots / rings
    for (const auto& m : validMoves) {
        int cx = ScrX(m.toCol), cy = ScrY(m.toRow);
        if (m.captured.IsEmpty())
            g.FillCircle(cx, cy, 8, C_VALID_DOT);
        else {
            g.DrawCircle(cx, cy, PIECE_R + 3, C_VALID_DOT);
            g.DrawCircle(cx, cy, PIECE_R + 4, C_VALID_DOT);
        }
    }

    // Last-move corner marks
    if (lastMove && lastMove->fromRow >= 0) {
        int fx = ScrX(lastMove->fromCol), fy = ScrY(lastMove->fromRow);
        int tx = ScrX(lastMove->toCol),   ty = ScrY(lastMove->toRow);
        auto mark = [&](int x, int y) {
            g.FillRect(x - 6, y - 6, 12, 3, C_LAST_MOVE);
            g.FillRect(x - 6, y - 6,  3,12, C_LAST_MOVE);
        };
        mark(fx, fy); mark(tx, ty);
    }

    // Hint overlay
    if (hintMove && hintMove->fromRow >= 0) {
        int fx = ScrX(hintMove->fromCol), fy = ScrY(hintMove->fromRow);
        int tx = ScrX(hintMove->toCol),   ty = ScrY(hintMove->toRow);
        g.FillCircle(fx, fy, 9, C_HINT);
        g.FillCircle(tx, ty, 9, C_HINT);
        g.DrawLine(fx, fy, tx, ty, C_HINT);
    }
}

// ============================================================
// Rendering: Bottom UI
// ============================================================
void DrawUI(GameLib& g, const ChessBoard& board, bool aiMode, bool gameOver,
            const char* gameOverMsg, int nodes, int qnodes, double searchTime)
{
    int fs = 18, panelY = 605, btnY = panelY + 12, btnH = 36, btnW = 90;

    g.FillRect(10, panelY, WIN_W - 20, 85, C_UI_BG);

    auto btn = [&](int x, const char* text, bool active) {
        uint32_t bg = active ? COLOR_RGB(90,70,50) : COLOR_RGB(60,50,35);
        uint32_t fg = active ? COLOR_WHITE : COLOR_RGB(140,130,110);
        g.FillRect(x, btnY, btnW, btnH, bg);
        g.DrawRect(x, btnY, btnW, btnH, COLOR_RGB(120,100,70));
        int tw = g.GetTextWidthFont(text, fs);
        int th = g.GetTextHeightFont(text, fs);
        g.DrawTextFont(x + (btnW-tw)/2, btnY + (btnH-th)/2, text, fg, fs);
    };

    btn(20,  "悔棋", true);
    btn(120, "提示", !gameOver && !aiMode);
    btn(220, "新局", true);

    // AI toggle
    {
        const char* t = aiMode ? "AI:开" : "AI:关";
        uint32_t bg = aiMode ? COLOR_RGB(60,90,60) : COLOR_RGB(60,50,35);
        uint32_t fg = aiMode ? COLOR_RGB(180,255,180) : COLOR_RGB(140,130,110);
        int tx = 320;
        g.FillRect(tx, btnY, btnW, btnH, bg);
        g.DrawRect(tx, btnY, btnW, btnH, COLOR_RGB(120,100,70));
        int tw = g.GetTextWidthFont(t, fs);
        int th = g.GetTextHeightFont(t, fs);
        g.DrawTextFont(tx + (btnW-tw)/2, btnY + (btnH-th)/2, t, fg, fs);
    }

    // Timers
    char buf[64];
    int timeY = btnY + 6;
    int tf = 16;

    auto drawTime = [&](int x, int y, const char* label, float secs, Side side, uint32_t activeColor) {
        int m = (int)(secs / 60);
        float s = secs - m * 60;
        snprintf(buf, sizeof(buf), "%s %02d:%04.1f", label, m, s);
        uint32_t c = (board.CurrentSide() == side && !gameOver) ? activeColor : C_UI_TEXT;
        g.DrawTextFont(x, y, buf, c, tf);
    };

    drawTime(430, timeY,      "红方", board.RedTime(),   RED,   C_RED_TEXT);
    drawTime(430, timeY + 22, "黑方", board.BlackTime(), BLACK, C_RED_TEXT);

    // Move count
    snprintf(buf, sizeof(buf), "第%d手", board.MoveCount() + 1);
    g.DrawTextFont(580, timeY, buf, C_UI_TEXT, tf);

    // AI nodes + time
    if (nodes > 0) {
        int n = nodes;
        const char* u = "";
        if (n >= 1000000) { n /= 1000000; u = "M"; }
        else if (n >= 1000) { n /= 1000; u = "K"; }
        snprintf(buf, sizeof(buf), "主搜:%d%s", n, u);
        g.DrawTextFont(580, timeY + 20, buf, C_UI_TEXT, 14);
        if (qnodes > 0) {
            int qn = qnodes;
            if (qn >= 1000) { qn /= 1000; u = "K"; }
            else u = "";
            snprintf(buf, sizeof(buf), "静搜:%d%s", qn, u);
            g.DrawTextFont(580, timeY + 36, buf, C_UI_TEXT, 14);
        }
        snprintf(buf, sizeof(buf), "用时:%.1fs", searchTime);
        g.DrawTextFont(580, timeY + 52, buf, C_UI_TEXT, 14);
    }

    // Status line
    const char* status = gameOverMsg;
    char autoBuf[32];
    if (!status) {
        snprintf(autoBuf, sizeof(autoBuf), "%s走棋", board.CurrentSide() == RED ? "红方" : "黑方");
        status = autoBuf;
    }
    uint32_t sc = gameOver ? C_CHECK_WARN :
                  (board.CurrentSide() == RED) ? C_RED_TEXT : C_UI_TEXT;
    int sw = g.GetTextWidthFont(status, 22);
    g.DrawTextFont((WIN_W - sw)/2, panelY - 35, status, sc, 22);

    if (gameOver) {
        int ow = g.GetTextWidthFont("按 新局 重新开始", 18);
        g.DrawTextFont((WIN_W - ow)/2, panelY - 50, "按 新局 重新开始", C_UI_TEXT, 18);
    }
}

// ============================================================
// Rendering: Status message (top of board)
// ============================================================
void DrawStatusMsg(GameLib& g, const char* msg) {
    if (!msg) return;
    int fs = 20;
    int tw = g.GetTextWidthFont(msg, fs);
    int x = (WIN_W - tw) / 2;
    int y = BOARD_Y - 42;
    g.FillRect(x - 12, y - 2, tw + 24, 28, C_MSG_BG);
    g.DrawRect(x - 12, y - 2, tw + 24, 28, C_UI_TEXT);
    g.DrawTextFont(x, y, msg, C_MSG_TEXT, fs);
}

// ============================================================
// UI hit-testing
// ============================================================
enum UIClick { U_NONE, U_UNDO, U_HINT, U_NEW, U_AI, U_BOARD };

inline UIClick HitTest(int sx, int sy, bool aiMode, bool gameOver) {
    const int btnY = 617, btnH = 36;
    if (sy >= btnY && sy <= btnY + btnH) {
        if      (sx >= 20  && sx <= 110) return U_UNDO;
        else if (sx >= 120 && sx <= 210) return (gameOver || aiMode) ? U_NONE : U_HINT;
        else if (sx >= 220 && sx <= 310) return U_NEW;
        else if (sx >= 320 && sx <= 410) return U_AI;
    }
    if (sx >= BOARD_X - PIECE_R && sx <= ScrX(8) + PIECE_R &&
        sy >= BOARD_Y - PIECE_R && sy <= ScrY(9) + PIECE_R)
        return U_BOARD;
    return U_NONE;
}

// ============================================================
// Game state
// ============================================================
struct GameState {
    ChessBoard board;
    ChessAI    ai;
    bool aiMode    = false;
    bool gameOver  = false;
    const char* overMsg = nullptr;

    int   selR     = -1;
    int   selC     = -1;
    std::vector<Move> validMoves;
    Move  hintMove{-1,-1,-1,-1,{EMPTY,BLACK}};
    bool  showHint = false;
    bool  needAI   = false;
    bool  inCheck  = false;
    int   lastNodes = 0;
    int   lastQNodes= 0;
    float lastTime = 0;

    // Status message
    const char* statusMsg = nullptr;
    float statusTimer = 0;

    void ShowMsg(const char* m) { statusMsg = m; statusTimer = 2.5f; }

    void Reset() {
        board.Init();
        gameOver = false;
        overMsg  = nullptr;
        selR = selC = -1;
        validMoves.clear();
        hintMove = {-1,-1,-1,-1,{EMPTY,BLACK}};
        showHint = false;
        needAI   = false;
        inCheck  = false;
        lastNodes = 0; lastQNodes = 0;
    }

    const Move* LastMovePtr() {
        return board.HasHistory() ? &board.LastMove() : nullptr;
    }
};

// ============================================================
// Main
// ============================================================
int main() {
    GameLib g;
    g.Open(WIN_W, WIN_H, "中国象棋", true);
    g.ShowMouse(true);

    GameState s;
    s.ai.SetMaxDepth(12);
    s.ai.SetTimeLimit(3.0);
    s.lastTime = g.GetTime();

    while (!g.IsClosed()) {
        float now = g.GetTime();
        float dt  = now - s.lastTime;
        s.lastTime = now;

        // --- Timer ---
        if (!s.gameOver) {
            if (s.board.CurrentSide() == RED)
                s.board.SetRedTime(s.board.RedTime() + dt);
            else
                s.board.SetBlackTime(s.board.BlackTime() + dt);
        }

        // --- Status message timer ---
        if (s.statusTimer > 0) {
            s.statusTimer -= dt;
            if (s.statusTimer <= 0) s.statusMsg = nullptr;
        }

        // --- Keyboard ---
        if (g.IsKeyPressed(KEY_SPACE)) {
            s.aiMode = !s.aiMode;
            s.needAI = false;
        }

        if (g.IsKeyPressed(KEY_U) && !s.gameOver && s.board.HasHistory()) {
            s.board.UndoMove();
            if (s.aiMode && s.board.CurrentSide() == BLACK && s.board.HasHistory())
                s.board.UndoMove();
            s.selR = s.selC = -1;
            s.validMoves.clear();
            s.showHint = false;
            s.inCheck = s.board.IsInCheck();
            s.lastNodes = 0; s.lastQNodes = 0;
            s.needAI = false;
        }

        if (g.IsKeyPressed(KEY_H) && !s.gameOver && !s.aiMode) {
            s.hintMove = s.ai.GetBestMove(s.board);
            s.showHint = true;
            s.lastNodes = s.ai.GetNodes(); s.lastQNodes = s.ai.GetQNodes();
            Sound::Click(g);
        }

        if (g.IsKeyPressed(KEY_N)) {
            s.Reset();
            s.lastTime = g.GetTime();
        }

        // --- Mouse ---
        if (g.IsMousePressed(MOUSE_LEFT)) {
            int mx = g.GetMouseX(), my = g.GetMouseY();
            UIClick clk = HitTest(mx, my, s.aiMode, s.gameOver);

            switch (clk) {
            case U_UNDO:
                Sound::Click(g);
                if (!s.gameOver && s.board.HasHistory()) {
                    s.board.UndoMove();
                    if (s.aiMode && s.board.CurrentSide() == BLACK && s.board.HasHistory())
                        s.board.UndoMove();
                    s.selR = s.selC = -1;
                    s.validMoves.clear();
                    s.showHint = false;
                    s.inCheck = s.board.IsInCheck();
                    s.lastNodes = 0; s.lastQNodes = 0;
                    s.needAI = false;
                }
                break;

            case U_HINT:
                Sound::Click(g);
                if (!s.gameOver && !s.aiMode) {
                    s.hintMove = s.ai.GetBestMove(s.board);
                    s.showHint = true;
                    s.lastNodes = s.ai.GetNodes(); s.lastQNodes = s.ai.GetQNodes();
                }
                break;

            case U_NEW:
                Sound::Click(g);
                s.Reset();
                s.lastTime = g.GetTime();
                break;

            case U_AI:
                Sound::Click(g);
                s.aiMode = !s.aiMode;
                s.selR = s.selC = -1;
                s.validMoves.clear();
                s.showHint = false;
                s.needAI = false;
                s.lastNodes = 0; s.lastQNodes = 0;
                break;

            case U_BOARD:
                if (!s.gameOver) {
                    // Block clicks during AI's turn
                    if (s.aiMode && s.board.CurrentSide() == BLACK && !s.needAI) {
                        s.ShowMsg("请等待 AI 走棋");
                        break;
                    }

                    int row, col;
                    if (!ScreenToBoard(mx, my, row, col)) break;

                    // 1. Check if clicking a valid-move destination
                    if (!s.validMoves.empty()) {
                        Move chosen = {-1,-1,-1,-1,{EMPTY,BLACK}};
                        for (const auto& m : s.validMoves) {
                            if (m.toRow == row && m.toCol == col && HitIntersection(mx, my, row, col)) {
                                chosen = m; break;
                            }
                        }
                        if (chosen.fromRow >= 0) {
                            bool isCap = !chosen.captured.IsEmpty();
                            s.board.ExecuteMove(chosen);
                            s.selR = s.selC = -1;
                            s.validMoves.clear();
                            s.showHint = false;
                            s.lastNodes = 0; s.lastQNodes = 0;

                            s.inCheck = s.board.IsInCheck();
                            if (isCap)       Sound::Capture(g);
                            else if (s.inCheck) Sound::Check(g);
                            else             Sound::Move(g);

                            if (s.board.IsCheckmate()) {
                                s.gameOver = true;
                                Side w = Opponent(s.board.CurrentSide());
                                s.overMsg = (w == RED) ? "红方胜!" : "黑方胜!";
                                Sound::GameOver(g);
                            } else if (s.aiMode && s.board.CurrentSide() == BLACK) {
                                s.needAI = true;
                            }
                            break;
                        }
                    }

                    // 2. Click on own piece → select / reselect
                    if (HitIntersection(mx, my, row, col) &&
                        s.board.At(row, col).IsSide(s.board.CurrentSide())) {
                        if (row == s.selR && col == s.selC) {
                            // Already selected → deselect
                            s.selR = s.selC = -1;
                            s.validMoves.clear();
                        } else {
                            s.selR = row; s.selC = col;
                            s.validMoves = s.board.GetLegalMoves(row, col);
                            s.showHint = false;
                            Sound::Click(g);
                        }
                        break;
                    }

                    // 3. Click on something else → deselect + message
                    s.selR = s.selC = -1;
                    s.validMoves.clear();
                    if (HitIntersection(mx, my, row, col))
                        s.ShowMsg("该位置无法到达");
                }
                break;

            default: break;
            }
        }

        // Right-click → deselect
        if (g.IsMousePressed(MOUSE_RIGHT)) {
            s.selR = s.selC = -1;
            s.validMoves.clear();
        }

        // --- AI move ---
        if (s.needAI && !s.gameOver) {
            // Show "thinking"
            DrawBoard(g);
            DrawPieces(g, s.board, -1, -1, {}, s.LastMovePtr(),
                       s.showHint ? &s.hintMove : nullptr, s.inCheck);
            DrawUI(g, s.board, s.aiMode, false, nullptr, s.lastNodes, s.lastQNodes, s.ai.LastTime());
            int tw = g.GetTextWidthFont("AI思考中...", 20);
            g.DrawTextFont((WIN_W - tw)/2, BOARD_Y - 30, "AI思考中...", C_HINT, 20);
            g.Update();

            // Compute
            Move aim = s.ai.GetBestMove(s.board);
            s.lastNodes = s.ai.GetNodes(); s.lastQNodes = s.ai.GetQNodes();

            if (aim.fromRow >= 0) {
                bool isCap = !aim.captured.IsEmpty();
                s.board.ExecuteMove(aim);
                s.inCheck = s.board.IsInCheck();

                if (isCap)       Sound::Capture(g);
                else if (s.inCheck) Sound::Check(g);
                else             Sound::Move(g);

                if (s.board.IsCheckmate()) {
                    s.gameOver = true;
                    s.overMsg = "红方胜!";
                    Sound::GameOver(g);
                }
            }
            s.needAI = false;
            s.selR = s.selC = -1;
            s.validMoves.clear();
            s.showHint = false;
        }

        // --- Render ---
        DrawBoard(g);
        DrawPieces(g, s.board, s.selR, s.selC, s.validMoves, s.LastMovePtr(),
                   s.showHint ? &s.hintMove : nullptr, s.inCheck);
        DrawUI(g, s.board, s.aiMode, s.gameOver, s.overMsg, s.lastNodes, s.lastQNodes, s.ai.LastTime());
        DrawStatusMsg(g, s.statusMsg);

        g.Update();
        g.WaitFrame(60);
    }
    return 0;
}
