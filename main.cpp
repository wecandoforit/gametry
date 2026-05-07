#include "GameLib.h"
#include <deque>
#include <queue>
#include <vector>
#include <algorithm>

// ============================================================
// 游戏参数
// ============================================================
const int   CELL_SIZE     = 20;
const int   GRID_COLS     = 30;
const int   GRID_ROWS     = 20;
const int   GRID_X        = 20;
const int   GRID_Y        = 40;
const double MOVE_INTERVAL = 0.08;    // AI 模式约每秒 12 步

// 网格坐标
struct Point {
    int x, y;
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

// 手动方向枚举（值对应 DIR_OFFSETS 下标）
enum { DIR_UP = 0, DIR_DOWN = 1, DIR_LEFT = 2, DIR_RIGHT = 3 };

// 四个方向偏移
const Point DIR_OFFSETS[4] = {
    { 0, -1 },   // 上
    { 0,  1 },   // 下
    {-1,  0 },   // 左
    { 1,  0 },   // 右
};

// 自定义颜色
const uint32_t SNAKE_BODY_COLOR = 0xFF22AA22;
const uint32_t SNAKE_HEAD_COLOR = 0xFF44FF44;
const uint32_t FOOD_COLOR       = 0xFFFF3333;
const uint32_t GRID_LINE_COLOR  = 0xFF222222;
const uint32_t PATH_COLOR       = 0x44FFFF00;   // 寻路预览（半透明黄）

// ============================================================
// 工具函数
// ============================================================

// 检查某个格子是否被蛇身占据
// excludeTail: 为 true 时排除尾部（因为尾部即将移走）
bool isOccupied(int x, int y, const std::deque<Point>& body, bool excludeTail)
{
    size_t n = excludeTail ? body.size() - 1 : body.size();
    for (size_t i = 0; i < n; i++) {
        if (body[i].x == x && body[i].y == y) return true;
    }
    return false;
}

bool inBounds(int x, int y) {
    return x >= 0 && x < GRID_COLS && y >= 0 && y < GRID_ROWS;
}

// BFS 寻路：返回从 start 到 target 的最短路径（含起点和终点）
// excludeTail: 寻路时是否把蛇尾当作可通行格子
std::vector<Point> findPath(Point start, Point target,
                            const std::deque<Point>& body, bool excludeTail)
{
    bool visited[GRID_ROWS][GRID_COLS] = {};
    Point parent[GRID_ROWS][GRID_COLS];

    std::queue<Point> q;
    q.push(start);
    visited[start.y][start.x] = true;
    parent[start.y][start.x] = { -1, -1 };

    while (!q.empty()) {
        Point cur = q.front(); q.pop();

        if (cur.x == target.x && cur.y == target.y) {
            // 回溯重建路径
            std::vector<Point> path;
            for (Point p = target; !(p.x == -1 && p.y == -1);
                 p = parent[p.y][p.x]) {
                path.push_back(p);
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        for (const auto& d : DIR_OFFSETS) {
            int nx = cur.x + d.x;
            int ny = cur.y + d.y;
            if (!inBounds(nx, ny)) continue;
            if (visited[ny][nx]) continue;
            if (isOccupied(nx, ny, body, excludeTail)) continue;
            visited[ny][nx] = true;
            parent[ny][nx] = cur;
            q.push({ nx, ny });
        }
    }
    return {};  // 无路径
}

// 模拟移动后的蛇身
std::deque<Point> simulateMove(const std::deque<Point>& body,
                                Point newHead, bool eating)
{
    std::deque<Point> result = body;
    result.push_front(newHead);
    if (!eating) result.pop_back();
    return result;
}

// 生成不与蛇身重叠的随机食物
Point randomFood(const std::deque<Point>& snake) {
    Point food;
    while (true) {
        food.x = GameLib::Random(0, GRID_COLS - 1);
        food.y = GameLib::Random(0, GRID_ROWS - 1);
        if (!isOccupied(food.x, food.y, snake, false)) break;
    }
    return food;
}

// ============================================================
// AI 决策引擎
// 返回蛇头下一步应该去的位置
// ============================================================
Point aiDecide(const std::deque<Point>& snake, Point food,
               std::vector<Point>* outPath = nullptr)
{
    Point head = snake.front();
    Point tail = snake.back();

    // ---- 策略 1：食物在眼前，直接吃（不检查安全） ----
    auto pathToFood = findPath(head, food, snake, true);
    if (!pathToFood.empty() && pathToFood.size() == 2) {
        // 路径长度=2 即 head→food 一步可达
        if (outPath) *outPath = pathToFood;
        return pathToFood[1];
    }

    // ---- 策略 2：多步路径去食物 + 安全检查 ----
    if (!pathToFood.empty() && pathToFood.size() > 2) {
        Point nextStep = pathToFood[1];
        auto simBody = simulateMove(snake, nextStep, false);
        auto pathToTail = findPath(simBody.front(), simBody.back(),
                                   simBody, true);
        if (!pathToTail.empty()) {
            if (outPath) *outPath = pathToFood;
            return nextStep;
        }
    }

    // ---- 策略 3：贪心 —— 在所有安全的走法中，选最接近食物的 ----
    // 避免边界食物导致盲目追尾的死循环
    Point bestMove = { -1, -1 };
    int   bestDist = 999999;
    for (const auto& d : DIR_OFFSETS) {
        int nx = head.x + d.x;
        int ny = head.y + d.y;
        if (!inBounds(nx, ny)) continue;
        if (isOccupied(nx, ny, snake, true)) continue;  // 排除尾部即可

        // 安全检查：走这步后能到达尾部吗？
        auto simBody = simulateMove(snake, { nx, ny }, false);
        auto safePath = findPath(simBody.front(), simBody.back(),
                                 simBody, true);
        if (!safePath.empty()) {
            int dist = abs(nx - food.x) + abs(ny - food.y);  // 曼哈顿距离
            if (dist < bestDist) {
                bestDist = dist;
                bestMove = { nx, ny };
                if (outPath) *outPath = { head, bestMove };
            }
        }
    }
    if (bestMove.x != -1)
        return bestMove;

    // ---- 策略 4：追尾部（最后保命手段） ----
    auto pathToTail = findPath(head, tail, snake, true);
    if (!pathToTail.empty() && pathToTail.size() > 1) {
        if (outPath) *outPath = pathToTail;
        return pathToTail[1];
    }

    // 完全无路可走
    return { -1, -1 };
}

// ============================================================
// 主程序
// ============================================================
int main() {
    GameLib game;
    game.Open(660, 500, "AI Snake - BFS 自动贪吃蛇", true);
    srand((unsigned)time(nullptr));

    // 游戏状态
    std::deque<Point> snake;
    Point food;
    Point nextHead;
    int  score      = 0;
    bool gameOver   = false;
    bool aiEnabled  = true;
    double moveTimer = 0.0;

    // 手动模式状态
    int  dir        = DIR_RIGHT;
    int  nextDir    = DIR_RIGHT;
    Point manDir    = { 1, 0 };  // 手动方向偏移

    // AI 寻路预览
    std::vector<Point> aiPath;

    auto initGame = [&]() {
        snake.clear();
        int cx = GRID_COLS / 2;
        int cy = GRID_ROWS / 2;
        snake.push_back({ cx,     cy });
        snake.push_back({ cx - 1, cy });
        snake.push_back({ cx - 2, cy });

        dir        = DIR_RIGHT;
        nextDir    = DIR_RIGHT;
        manDir     = { 1, 0 };
        score      = 0;
        gameOver   = false;
        moveTimer  = 0.0;
        aiPath.clear();

        food = randomFood(snake);
    };

    initGame();

    // ---- 游戏主循环 ----
    while (!game.IsClosed()) {
        double dt = game.GetDeltaTime();

        // ============ 输入处理 ============
        if (gameOver) {
            if (game.IsKeyPressed(KEY_SPACE))
                initGame();
        } else {
            // 空格切换 AI / 手动
            if (game.IsKeyPressed(KEY_SPACE)) {
                aiEnabled = !aiEnabled;
                aiPath.clear();
            }

            if (!aiEnabled) {
                // 手动输入：方向键 / WASD，禁止 180 度掉头
                if (game.IsKeyDown(KEY_UP)    || game.IsKeyDown(KEY_W))
                    if (dir != DIR_DOWN)  nextDir = DIR_UP;
                if (game.IsKeyDown(KEY_DOWN)  || game.IsKeyDown(KEY_S))
                    if (dir != DIR_UP)    nextDir = DIR_DOWN;
                if (game.IsKeyDown(KEY_LEFT)  || game.IsKeyDown(KEY_A))
                    if (dir != DIR_RIGHT) nextDir = DIR_LEFT;
                if (game.IsKeyDown(KEY_RIGHT) || game.IsKeyDown(KEY_D))
                    if (dir != DIR_LEFT)  nextDir = DIR_RIGHT;
            }
        }

        // ============ 移动逻辑 ============
        if (!gameOver) {
            moveTimer += dt;
            if (moveTimer >= MOVE_INTERVAL) {
                moveTimer -= MOVE_INTERVAL;

                Point head = snake.front();

                if (aiEnabled) {
                    // AI 决策
                    aiPath.clear();
                    nextHead = aiDecide(snake, food, &aiPath);
                } else {
                    // 手动决策
                    dir = nextDir;
                    manDir = DIR_OFFSETS[dir];
                    nextHead = { head.x + manDir.x, head.y + manDir.y };
                }

                // 无路可走 → 游戏结束
                if (nextHead.x == -1 && nextHead.y == -1) {
                    gameOver = true;
                } else {
                    bool willEat = (nextHead == food);

                    // 撞墙
                    if (!inBounds(nextHead.x, nextHead.y)) {
                        gameOver = true;
                    }

                    // 撞自身
                    if (!gameOver) {
                        size_t checkLen = willEat ? snake.size() : snake.size() - 1;
                        for (size_t i = 0; i < checkLen; i++) {
                            if (snake[i] == nextHead) {
                                gameOver = true;
                                break;
                            }
                        }
                    }

                    if (!gameOver) {
                        snake.push_front(nextHead);
                        if (willEat) {
                            score += 10;
                            food = randomFood(snake);
                            aiPath.clear();
                        } else {
                            snake.pop_back();
                        }
                    }
                }
            }
        }

        // ============ 渲染 ============
        game.Clear(COLOR_BLACK);

        // 网格
        game.DrawGrid(GRID_X, GRID_Y, GRID_ROWS, GRID_COLS,
                      CELL_SIZE, GRID_LINE_COLOR);

        // AI 寻路预览（淡黄色半透明路径）
        if (aiEnabled && !aiPath.empty()) {
            for (size_t i = 1; i < aiPath.size(); i++) {
                game.FillCell(GRID_X, GRID_Y,
                              aiPath[i].y, aiPath[i].x,
                              CELL_SIZE, PATH_COLOR);
            }
        }

        // 蛇身
        for (size_t i = 0; i < snake.size(); i++) {
            uint32_t color = (i == 0) ? SNAKE_HEAD_COLOR : SNAKE_BODY_COLOR;
            game.FillCell(GRID_X, GRID_Y, snake[i].y, snake[i].x,
                          CELL_SIZE, color);
        }

        // 食物
        game.FillCell(GRID_X, GRID_Y, food.y, food.x,
                      CELL_SIZE, FOOD_COLOR);

        // HUD
        game.DrawPrintf(10, 10, COLOR_WHITE,
                        "Score: %d", score);
        game.DrawPrintf(200, 10, COLOR_WHITE,
                        "Len: %d", (int)snake.size());
        game.DrawPrintf(340, 10, aiEnabled ? COLOR_CYAN : COLOR_YELLOW,
                        "[%s]  SPACE:切换",
                        aiEnabled ? "AI ON " : "AI OFF");
        game.DrawPrintf(540, 10, COLOR_WHITE,
                        "FPS: %.0f", game.GetFPS());

        // 游戏结束
        if (gameOver) {
            game.FillRect(GRID_X, GRID_Y,
                          GRID_COLS * CELL_SIZE,
                          GRID_ROWS * CELL_SIZE,
                          0x88000000);   // 半透明黑色遮罩

            int cx = GRID_X + GRID_COLS * CELL_SIZE / 2;
            int cy = GRID_Y + GRID_ROWS * CELL_SIZE / 2;
            game.DrawText(cx - 60, cy - 20, "GAME OVER", COLOR_YELLOW);
            game.DrawPrintf(cx - 90, cy + 2, COLOR_WHITE,
                            "Score: %d", score);
            game.DrawText(cx - 90, cy + 18,
                          "Press SPACE to restart", COLOR_WHITE);
        }

        game.Update();
        game.WaitFrame(60);
    }

    return 0;
}
