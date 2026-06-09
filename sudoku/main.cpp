#define GAMELIB_IMPLEMENTATION
#include "../GameLib.h"
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cstdio>

// ============================================================
// 颜色常量
// ============================================================
const uint32_t COL_BG          = 0xFF1A1A2E;
const uint32_t COL_GRID_LINE   = 0xFF444466;
const uint32_t COL_BOX_LINE    = 0xFF4A90D9;
const uint32_t COL_GIVEN_NUM   = 0xFFFFFFFF;
const uint32_t COL_USER_NUM    = 0xFF88CCFF;
const uint32_t COL_CONFLICT    = 0xFFFF4444;
const uint32_t COL_SELECTED    = 0x334A90D9;
const uint32_t COL_HIGHLIGHT   = 0x22224466;
const uint32_t COL_CANDIDATE   = 0xFF777799;
const uint32_t COL_BTN_BG      = 0xFF2A2A44;
const uint32_t COL_BTN_HOVER   = 0xFF3A3A5E;
const uint32_t COL_BTN_PRESS   = 0xFF4A90D9;
const uint32_t COL_BTN_TEXT    = 0xFFCCCCDD;
const uint32_t COL_TIMER       = 0xFFAAAACC;
const uint32_t COL_STATUS      = 0xFF88FF88;
const uint32_t COL_PENCIL_ON   = 0xFFFFAA44;

// ============================================================
// 布局常量
// ============================================================
const int WIN_W        = 640;
const int WIN_H        = 610;

const int GRID_X       = 25;
const int GRID_Y       = 55;
const int CELL_SIZE    = 52;
const int GRID_W       = CELL_SIZE * 9;

const int NUM_PAD_X    = 25;
const int NUM_PAD_Y    = 540;
const int NUM_BTN_W    = 44;
const int NUM_BTN_H    = 40;
const int NUM_BTN_GAP  = 4;

const int SIDE_X       = 510;
const int SIDE_Y       = 55;
const int SIDE_W       = 115;
const int SIDE_BTN_H   = 34;
const int SIDE_BTN_GAP = 6;

const int TIMER_X      = 10;
const int TIMER_Y      = 8;

// ============================================================
// 按钮结构
// ============================================================
struct Button {
    int x, y, w, h;
    const char *text;
    bool hovered;
    bool pressed;
};

bool pointInRect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

// ============================================================
// 数独核心
// ============================================================

class SudokuBoard {
public:
    int  grid[81];        // 0 = 空, 1-9 = 数字
    bool given[81];       // 是否初始题目
    int  candidates[81];  // 位掩码: bit 1..9

    SudokuBoard() { reset(); }

    void reset() {
        memset(grid, 0, sizeof(grid));
        memset(given, 0, sizeof(given));
        memset(candidates, 0, sizeof(candidates));
    }

    void copyFrom(const SudokuBoard &src) {
        memcpy(grid, src.grid, sizeof(grid));
        memcpy(given, src.given, sizeof(given));
        memcpy(candidates, src.candidates, sizeof(candidates));
    }
};

// ============================================================
// 求解器与生成器
// ============================================================

// 返回位置 p 的同行/列/宫已有数字的位掩码
int getUsedBits(const int grid[81], int p) {
    int r = p / 9, c = p % 9;
    int br = (r / 3) * 3, bc = (c / 3) * 3;
    int used = 0;
    for (int i = 0; i < 9; i++) {
        if (grid[r * 9 + i]) used |= (1 << grid[r * 9 + i]);
        if (grid[i * 9 + c]) used |= (1 << grid[i * 9 + c]);
        int cr = br + i / 3, cc = bc + i % 3;
        if (grid[cr * 9 + cc]) used |= (1 << grid[cr * 9 + cc]);
    }
    return used;
}

// 回溯生成完整数独
bool fillBoard(int grid[81], int pos) {
    if (pos == 81) return true;
    if (grid[pos] != 0) return fillBoard(grid, pos + 1);

    int used = getUsedBits(grid, pos);
    // 随机排列 1-9
    int nums[9];
    for (int i = 0; i < 9; i++) nums[i] = i + 1;
    // Fisher-Yates shuffle
    for (int i = 8; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = nums[i]; nums[i] = nums[j]; nums[j] = t;
    }

    for (int k = 0; k < 9; k++) {
        int d = nums[k];
        if (used & (1 << d)) continue;
        grid[pos] = d;
        if (fillBoard(grid, pos + 1)) return true;
    }
    grid[pos] = 0;
    return false;
}

void generateFullBoard(int grid[81]) {
    memset(grid, 0, 81 * sizeof(int));
    fillBoard(grid, 0);
}

// 求解器：返回解的个数，countOnly 为 true 时找到第二个解就停止
int solveCount(int grid[81], int start, bool countOnly) {
    if (start == 81) return 1;

    // 找约束最强的空格（最少候选数）
    int best = -1, bestCnt = 10;
    for (int i = start; i < 81; i++) {
        if (grid[i] == 0) {
            int used = getUsedBits(grid, i);
            int cnt = 0;
            for (int d = 1; d <= 9; d++)
                if (!(used & (1 << d))) cnt++;
            if (cnt < bestCnt) { bestCnt = cnt; best = i; }
            if (cnt == 0) return 0; // 死路
        }
    }
    if (best == -1) return 1; // 全填满

    int used = getUsedBits(grid, best);
    int total = 0;
    for (int d = 1; d <= 9; d++) {
        if (used & (1 << d)) continue;
        grid[best] = d;
        total += solveCount(grid, best + 1, countOnly);
        if (countOnly && total >= 2) {
            grid[best] = 0;
            return 2; // 提前终止
        }
        if (total > 1000) { // 保底，避免爆炸
            grid[best] = 0;
            return total;
        }
    }
    grid[best] = 0;
    return total;
}

// 判断唯一解
bool hasUniqueSolution(const int grid[81]) {
    int tmp[81];
    memcpy(tmp, grid, 81 * sizeof(int));
    return solveCount(tmp, 0, true) == 1;
}

// 求解（用于提示），返回第一个解
bool solveOne(int grid[81]) {
    for (int i = 0; i < 81; i++) {
        if (grid[i] == 0) {
            int used = getUsedBits(grid, i);
            for (int d = 1; d <= 9; d++) {
                if (used & (1 << d)) continue;
                grid[i] = d;
                if (solveOne(grid)) return true;
            }
            grid[i] = 0;
            return false;
        }
    }
    return true; // 全填满
}

// 生成题目：完整盘面 + 挖格子
void generatePuzzle(int outGrid[81], int blanks) {
    int full[81];
    generateFullBoard(full);

    // 创建所有位置索引，随机打乱
    int order[81];
    for (int i = 0; i < 81; i++) order[i] = i;
    for (int i = 80; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }

    // 复制完整盘面
    int puzzle[81];
    memcpy(puzzle, full, 81 * sizeof(int));

    int removed = 0;
    for (int i = 0; i < 81 && removed < blanks; i++) {
        int pos = order[i];
        int backup = puzzle[pos];
        puzzle[pos] = 0;
        if (hasUniqueSolution(puzzle)) {
            removed++;
        } else {
            puzzle[pos] = backup;
        }
    }

    memcpy(outGrid, puzzle, 81 * sizeof(int));
}

// ============================================================
// 游戏状态管理
// ============================================================

enum { DIFF_EASY = 0, DIFF_MEDIUM, DIFF_HARD, DIFF_COUNT };

const int DIFF_BLANKS[DIFF_COUNT] = { 35, 45, 54 };
const char *DIFF_NAMES[DIFF_COUNT] = { "Easy", "Medium", "Hard" };

struct Action {
    int pos;
    int old_val;
    int old_cand;
    int new_val;
    int new_cand;
};

static double gameTime();

class GameState {
public:
    SudokuBoard board;
    int selected;        // -1 表示未选中
    bool pencilMode;     // 候选模式
    int difficulty;
    bool solved;
    int conflictCells[81]; // 冲突标记

    std::vector<Action> undoStack;
    std::vector<Action> redoStack;

    // 计时器
    double startTime;
    double elapsed;
    bool   timerRunning;

    // 提示冷却
    int hintCooldown;

    GameState() {
        selected = -1;
        pencilMode = false;
        difficulty = DIFF_EASY;
        solved = false;
        memset(conflictCells, 0, sizeof(conflictCells));
        elapsed = 0;
        timerRunning = false;
        hintCooldown = 0;
        board.reset();
    }

    void newGame(int diff) {
        difficulty = diff;
        board.reset();
        selected = -1;
        pencilMode = false;
        solved = false;
        memset(conflictCells, 0, sizeof(conflictCells));
        undoStack.clear();
        redoStack.clear();
        elapsed = 0;
        timerRunning = false;
        hintCooldown = 0;

        int blanks = DIFF_BLANKS[diff];
        generatePuzzle(board.grid, blanks);

        // 标记初始题目
        for (int i = 0; i < 81; i++) {
            board.given[i] = (board.grid[i] != 0);
        }
    }

    void recordAction(int pos, int oldVal, int oldCand, int newVal, int newCand) {
        Action act;
        act.pos = pos;
        act.old_val = oldVal;
        act.old_cand = oldCand;
        act.new_val = newVal;
        act.new_cand = newCand;
        undoStack.push_back(act);
        redoStack.clear();
    }

    void undo() {
        if (undoStack.empty()) return;
        Action act = undoStack.back();
        undoStack.pop_back();

        int curVal = board.grid[act.pos];
        int curCand = board.candidates[act.pos];

        board.grid[act.pos] = act.old_val;
        board.candidates[act.pos] = act.old_cand;

        Action redo;
        redo.pos = act.pos;
        redo.old_val = act.old_val;
        redo.old_cand = act.old_cand;
        redo.new_val = curVal;
        redo.new_cand = curCand;
        redoStack.push_back(redo);

        updateConflicts();
        checkSolved();
    }

    void redo() {
        if (redoStack.empty()) return;
        Action act = redoStack.back();
        redoStack.pop_back();

        int curVal = board.grid[act.pos];
        int curCand = board.candidates[act.pos];

        board.grid[act.pos] = act.new_val;
        board.candidates[act.pos] = act.new_cand;

        Action undoAct;
        undoAct.pos = act.pos;
        undoAct.old_val = curVal;
        undoAct.old_cand = curCand;
        undoAct.new_val = act.new_val;
        undoAct.new_cand = act.new_cand;
        undoStack.push_back(undoAct);

        updateConflicts();
        checkSolved();
    }

    void startTimer() {
        if (!timerRunning) {
            startTime = gameTime();
            timerRunning = true;
        }
    }

    double getTime() const {
        if (timerRunning)
            return elapsed + (gameTime() - startTime);
        return elapsed;
    }

    // 检查某个格子的数字是否有冲突
    bool isConflicting(int pos) {
        int val = board.grid[pos];
        if (val == 0) return false;

        int r = pos / 9, c = pos % 9;
        int br = (r / 3) * 3, bc = (c / 3) * 3;

        for (int i = 0; i < 9; i++) {
            int pc = r * 9 + i;
            if (pc != pos && board.grid[pc] == val) return true;
            int pr = i * 9 + c;
            if (pr != pos && board.grid[pr] == val) return true;
            int cr = br + i / 3, cc = bc + i % 3;
            int p3 = cr * 9 + cc;
            if (p3 != pos && board.grid[p3] == val) return true;
        }
        return false;
    }

    void updateConflicts() {
        memset(conflictCells, 0, sizeof(conflictCells));
        for (int i = 0; i < 81; i++) {
            if (!board.given[i] && board.grid[i] != 0 && isConflicting(i)) {
                conflictCells[i] = 1;
            }
        }
    }

    bool isBoardFull() {
        for (int i = 0; i < 81; i++)
            if (board.grid[i] == 0) return false;
        return true;
    }

    bool isBoardCorrect() {
        for (int i = 0; i < 81; i++)
            if (isConflicting(i) || board.grid[i] == 0) return false;
        return true;
    }

    void checkSolved() {
        if (isBoardFull() && isBoardCorrect()) {
            solved = true;
            timerRunning = false;
            elapsed = gameTime() - startTime;
        }
    }

    // 提示：在 selected 位置填入正确答案
    void hint() {
        if (selected < 0) return;
        if (board.given[selected]) return;
        if (board.grid[selected] != 0) return;
        if (hintCooldown > 0) return;

        int tmp[81];
        memcpy(tmp, board.grid, 81 * sizeof(int));
        if (!solveOne(tmp)) return;

        int answer = tmp[selected];
        int oldVal = board.grid[selected];
        int oldCand = board.candidates[selected];

        board.grid[selected] = answer;
        board.candidates[selected] = 0;
        recordAction(selected, oldVal, oldCand, answer, 0);
        updateConflicts();
        checkSolved();
        hintCooldown = 30;
        startTimer();
    }

    void checkAnswer() {
        updateConflicts();
    }
};

// 全局 GameLib 指针（用于 GameState 中获取时间——因为 GameState 无法直接访问 game 对象）
// 用简单的替代方案：在 main 里传入 GameLib 引用或直接用 clock()
static GameLib *g_game = nullptr;
static double gameTime() {
    if (g_game) return g_game->GetTime();
    return (double)clock() / CLOCKS_PER_SEC;
}

// ============================================================
// 渲染
// ============================================================

void drawButton(GameLib &game, const Button &btn) {
    uint32_t bg = btn.pressed ? COL_BTN_PRESS :
                  btn.hovered ? COL_BTN_HOVER : COL_BTN_BG;
    game.FillRect(btn.x, btn.y, btn.w, btn.h, bg);
    game.DrawRect(btn.x, btn.y, btn.w, btn.h, COL_BOX_LINE);

    // 文字居中（用内置 8x8 字体）
    int tw = (int)strlen(btn.text) * 8;
    int tx = btn.x + (btn.w - tw) / 2;
    int ty = btn.y + (btn.h - 8) / 2;
    game.DrawText(tx, ty, btn.text, COL_BTN_TEXT);
}

void drawGrid(GameLib &game, const GameState &state) {
    const SudokuBoard &b = state.board;

    // 绘制格子背景
    for (int r = 0; r < 9; r++) {
        for (int c = 0; c < 9; c++) {
            int x = GRID_X + c * CELL_SIZE;
            int y = GRID_Y + r * CELL_SIZE;
            int idx = r * 9 + c;

            // 选中格子高亮
            if (idx == state.selected) {
                game.FillRect(x + 1, y + 1, CELL_SIZE - 1, CELL_SIZE - 1, COL_SELECTED);
            }
            // 同行/同列/同宫高亮
            else if (state.selected >= 0) {
                int sr = state.selected / 9, sc = state.selected % 9;
                int sbr = (sr / 3) * 3, sbc = (sc / 3) * 3;
                if (r == sr || c == sc ||
                    (r >= sbr && r < sbr + 3 && c >= sbc && c < sbc + 3)) {
                    game.FillRect(x + 1, y + 1, CELL_SIZE - 1, CELL_SIZE - 1, COL_HIGHLIGHT);
                }
            }
        }
    }

    // 网格线
    for (int i = 0; i <= 9; i++) {
        int x = GRID_X + i * CELL_SIZE;
        int y = GRID_Y + i * CELL_SIZE;
        bool isBold = (i % 3 == 0);
        uint32_t color = isBold ? COL_BOX_LINE : COL_GRID_LINE;
        int thick = isBold ? 3 : 1;

        // 竖线
        game.FillRect(x - thick/2, GRID_Y, thick, 9 * CELL_SIZE, color);
        // 横线
        if (i < 9) {
            game.FillRect(GRID_X, y - thick/2, 9 * CELL_SIZE, thick, color);
        } else {
            game.FillRect(GRID_X, y - 1, 9 * CELL_SIZE, 2, color);
        }
    }

    // 数字与候选数
    for (int r = 0; r < 9; r++) {
        for (int c = 0; c < 9; c++) {
            int idx = r * 9 + c;
            int val = b.grid[idx];
            int x = GRID_X + c * CELL_SIZE;
            int y = GRID_Y + r * CELL_SIZE;

            if (val != 0) {
                uint32_t textColor;
                if (state.conflictCells[idx])
                    textColor = COL_CONFLICT;
                else if (b.given[idx])
                    textColor = COL_GIVEN_NUM;
                else
                    textColor = COL_USER_NUM;

                char buf[4];
                sprintf(buf, "%d", val);
                int tw = game.GetTextWidthFont(buf, "Consolas", 28);
                int th = game.GetTextHeightFont(buf, "Consolas", 28);
                int tx = x + (CELL_SIZE - tw) / 2;
                int ty = y + (CELL_SIZE - th) / 2;
                game.DrawTextFont(tx, ty, buf, textColor, "Consolas", 28);
            } else if (b.candidates[idx] != 0) {
                // 候选数：3×3 布局
                int mask = b.candidates[idx];
                int subW = CELL_SIZE / 3;
                int subH = CELL_SIZE / 3;
                for (int d = 1; d <= 9; d++) {
                    if (mask & (1 << d)) {
                        int cx = (d - 1) % 3;
                        int cy = (d - 1) / 3;
                        int px = x + cx * subW + subW / 2;
                        int py = y + cy * subH + subH / 2;
                        char buf[2];
                        buf[0] = '0' + d; buf[1] = 0;
                        int tw = game.GetTextWidthFont(buf, "Consolas", 12);
                        int th = game.GetTextHeightFont(buf, "Consolas", 12);
                        game.DrawTextFont(px - tw/2, py - th/2, buf, COL_CANDIDATE, "Consolas", 12);
                    }
                }
            }
        }
    }
}

void drawNumPad(GameLib &game, const Button btns[10]) {
    for (int i = 0; i < 10; i++) {
        drawButton(game, btns[i]);
    }
}

void drawSidePanel(GameLib &game, const GameState &state, const Button btns[], int btnCount) {
    for (int i = 0; i < btnCount; i++) {
        drawButton(game, btns[i]);
    }
}

void drawTimer(GameLib &game, const GameState &state) {
    double t = state.getTime();
    int mins = (int)t / 60;
    int secs = (int)t % 60;
    char buf[32];
    sprintf(buf, "%02d:%02d", mins, secs);
    game.DrawTextFont(TIMER_X, TIMER_Y, buf, COL_TIMER, "Consolas", 20);
}

void drawDifficulty(GameLib &game, const GameState &state) {
    game.DrawTextFont(SIDE_X, TIMER_Y - 1, DIFF_NAMES[state.difficulty],
                      state.difficulty == DIFF_HARD ? 0xFFFF8844 :
                      state.difficulty == DIFF_MEDIUM ? 0xFFFFCC44 : COL_STATUS,
                      "Consolas", 18);
}

void drawStatus(GameLib &game, const GameState &state) {
    if (state.solved) {
        game.DrawTextFont(SIDE_X, GRID_Y + 450, "SOLVED!", COL_STATUS, "Consolas", 16);

        double t = state.getTime();
        int mins = (int)t / 60;
        int secs = (int)t % 60;
        char buf[32];
        sprintf(buf, "Time %02d:%02d", mins, secs);
        game.DrawTextFont(SIDE_X, GRID_Y + 475, buf, COL_TIMER, "Consolas", 14);
    }
}

void drawPencilIndicator(GameLib &game, const GameState &state) {
    if (state.pencilMode) {
        game.DrawTextFont(GRID_X + GRID_W + 5, GRID_Y + GRID_W - 18,
                          "Pencil", COL_PENCIL_ON, "Consolas", 14);
    }
}

// ============================================================
// 交互处理
// ============================================================

int getGridIndex(int mx, int my) {
    if (mx < GRID_X || mx >= GRID_X + 9 * CELL_SIZE) return -1;
    if (my < GRID_Y || my >= GRID_Y + 9 * CELL_SIZE) return -1;
    int c = (mx - GRID_X) / CELL_SIZE;
    int r = (my - GRID_Y) / CELL_SIZE;
    return r * 9 + c;
}

void handleNumberKey(GameState &state, int num) {
    if (state.selected < 0) return;
    if (state.solved) return;
    int idx = state.selected;
    if (state.board.given[idx]) return;

    if (state.pencilMode) {
        if (state.board.grid[idx] != 0) return;
        int oldVal = state.board.grid[idx];
        int oldCand = state.board.candidates[idx];
        state.board.candidates[idx] ^= (1 << num);
        int newVal = state.board.grid[idx];
        int newCand = state.board.candidates[idx];
        state.recordAction(idx, oldVal, oldCand, newVal, newCand);
    } else {
        int oldVal = state.board.grid[idx];
        int oldCand = state.board.candidates[idx];

        if (state.board.grid[idx] == num) {
            state.board.grid[idx] = 0;
        } else {
            state.board.grid[idx] = num;
            state.board.candidates[idx] = 0;
        }

        int newVal = state.board.grid[idx];
        int newCand = state.board.candidates[idx];
        state.recordAction(idx, oldVal, oldCand, newVal, newCand);
        state.updateConflicts();
        state.checkSolved();
    }
    state.startTimer();
}

void handleClear(GameState &state) {
    if (state.selected < 0) return;
    if (state.solved) return;
    int idx = state.selected;
    if (state.board.given[idx]) return;

    int oldVal = state.board.grid[idx];
    int oldCand = state.board.candidates[idx];

    state.board.grid[idx] = 0;
    state.board.candidates[idx] = 0;

    int newVal = 0, newCand = 0;
    state.recordAction(idx, oldVal, oldCand, newVal, newCand);
    state.updateConflicts();
    state.checkSolved();
    state.startTimer();
}

// ============================================================
// 主函数
// ============================================================
int main() {
    GameLib game;
    game.Open(WIN_W, WIN_H, "Sudoku", true);
    srand((unsigned)time(nullptr));
    g_game = &game;

    GameState state;
    state.newGame(DIFF_EASY);

    // ---- 创建数字小键盘按钮 ----
    Button numPad[10];
    char numPadLabels[10][2];
    for (int i = 0; i < 9; i++) {
        numPad[i].x = NUM_PAD_X + i * (NUM_BTN_W + NUM_BTN_GAP);
        numPad[i].y = NUM_PAD_Y;
        numPad[i].w = NUM_BTN_W;
        numPad[i].h = NUM_BTN_H;
        numPadLabels[i][0] = '1' + i;
        numPadLabels[i][1] = 0;
        numPad[i].text = numPadLabels[i];
        numPad[i].hovered = false;
        numPad[i].pressed = false;
    }
    numPad[9].x = NUM_PAD_X + 9 * (NUM_BTN_W + NUM_BTN_GAP);
    numPad[9].y = NUM_PAD_Y;
    numPad[9].w = NUM_BTN_W;
    numPad[9].h = NUM_BTN_H;
    numPadLabels[9][0] = 'X';
    numPadLabels[9][1] = 0;
    numPad[9].text = numPadLabels[9];
    numPad[9].hovered = false;
    numPad[9].pressed = false;

    // ---- 创建右侧面板按钮 ----
    const int SIDE_BTN_COUNT = 7;
    Button sideBtns[SIDE_BTN_COUNT];
    const char *sideLabels[SIDE_BTN_COUNT] = {
        "Pencil",
        "Undo",
        "Redo",
        "Hint",
        "Check",
        "New",
        "Level >"
    };
    for (int i = 0; i < SIDE_BTN_COUNT; i++) {
        sideBtns[i].x = SIDE_X;
        sideBtns[i].y = SIDE_Y + i * (SIDE_BTN_H + SIDE_BTN_GAP);
        sideBtns[i].w = SIDE_W;
        sideBtns[i].h = SIDE_BTN_H;
        sideBtns[i].text = sideLabels[i];
        sideBtns[i].hovered = false;
        sideBtns[i].pressed = false;
    }

    // ---- 主循环 ----
    while (!game.IsClosed()) {
        game.Clear(COL_BG);

        int mx = game.GetMouseX();
        int my = game.GetMouseY();
        bool mouseDown = game.IsMouseDown(0);
        bool mouseClicked = game.IsMousePressed(0);

        // ---- 更新按钮状态 ----
        for (int i = 0; i < 10; i++) {
            numPad[i].hovered = pointInRect(mx, my, numPad[i].x, numPad[i].y, numPad[i].w, numPad[i].h);
            numPad[i].pressed = numPad[i].hovered && mouseDown;
        }
        for (int i = 0; i < SIDE_BTN_COUNT; i++) {
            sideBtns[i].hovered = pointInRect(mx, my, sideBtns[i].x, sideBtns[i].y, sideBtns[i].w, sideBtns[i].h);
            sideBtns[i].pressed = sideBtns[i].hovered && mouseDown;
        }

        // ---- 鼠标点击网格 ----
        if (mouseClicked) {
            int idx = getGridIndex(mx, my);
            if (idx >= 0) {
                state.selected = idx;
            }
        }

        // ---- 鼠标点击小键盘 ----
        for (int i = 0; i < 9; i++) {
            if (mouseClicked && numPad[i].hovered) {
                handleNumberKey(state, i + 1);
            }
        }
        if (mouseClicked && numPad[9].hovered) {
            handleClear(state);
        }

        // ---- 鼠标点击右侧按钮 ----
        if (mouseClicked) {
            if (sideBtns[0].hovered) {
                state.pencilMode = !state.pencilMode;
            }
            if (sideBtns[1].hovered) {
                state.undo();
            }
            if (sideBtns[2].hovered) {
                state.redo();
            }
            if (sideBtns[3].hovered) {
                state.hint();
            }
            if (sideBtns[4].hovered) {
                state.checkAnswer();
            }
            if (sideBtns[5].hovered) {
                state.newGame(state.difficulty);
            }
            if (sideBtns[6].hovered) {
                int nd = (state.difficulty + 1) % DIFF_COUNT;
                state.newGame(nd);
            }
        }

        // ---- 键盘输入 ----
        for (int d = 0; d < 9; d++) {
            if (game.IsKeyPressed('1' + d)) {
                handleNumberKey(state, d + 1);
            }
        }
        for (int d = 0; d < 9; d++) {
            if (game.IsKeyPressed(VK_NUMPAD1 + d)) {
                handleNumberKey(state, d + 1);
            }
        }
        if (game.IsKeyPressed(VK_BACK) || game.IsKeyPressed(VK_DELETE)) {
            handleClear(state);
        }
        // 方向键移动选中
        if (state.selected >= 0) {
            int r = state.selected / 9, c = state.selected % 9;
            if (game.IsKeyPressed(VK_LEFT)  && c > 0) state.selected--;
            if (game.IsKeyPressed(VK_RIGHT) && c < 8) state.selected++;
            if (game.IsKeyPressed(VK_UP)    && r > 0) state.selected -= 9;
            if (game.IsKeyPressed(VK_DOWN)  && r < 8) state.selected += 9;
        }
        // Ctrl+Z 撤销, Ctrl+Y 重做
        if (game.IsKeyDown(VK_CONTROL)) {
            if (game.IsKeyPressed('Z')) state.undo();
            if (game.IsKeyPressed('Y')) state.redo();
        }
        // P 铅笔模式切换
        if (game.IsKeyPressed('P')) {
            state.pencilMode = !state.pencilMode;
        }
        // H 提示
        if (game.IsKeyPressed('H')) {
            state.hint();
        }
        // Ctrl+N 新游戏
        if (game.IsKeyDown(VK_CONTROL) && game.IsKeyPressed('N')) {
            state.newGame(state.difficulty);
        }

        // ---- 提示冷却 ----
        if (state.hintCooldown > 0) state.hintCooldown--;

        // ---- 渲染 ----
        drawGrid(game, state);
        drawNumPad(game, numPad);
        drawSidePanel(game, state, sideBtns, SIDE_BTN_COUNT);
        drawTimer(game, state);
        drawDifficulty(game, state);
        drawStatus(game, state);
        drawPencilIndicator(game, state);

        game.Update();
        game.WaitFrame(60);
    }

    return 0;
}
