// ====================================================================
// fsm_demo / main.cpp
//
// 用途：有限状态机（FSM）学习 Demo
//
// 场景：一根直线作为地面 + 一个渐变蓝小球作为玩家可控角色。
//       玩家可以左右移动和跳跃。跳跃/落地过程中，状态机会自动
//       在 IDLE / RUNNING / JUMPING / FALLING 四个状态之间切换。
//
// 编译（MinGW / Dev C++）：
//     g++ -o main.exe main.cpp -mwindows
//
// 控制：
//     ← →   左右移动
//     空格   跳跃
//     ESC    退出
// ====================================================================

#define GAMELIB_IMPLEMENTATION
#include "../GameLib.h"

// ====================================================================
// 游戏常量
// ====================================================================
const int   WIN_W       = 800;       // 窗口宽度
const int   WIN_H       = 600;       // 窗口高度
const int   GROUND_Y    = 480;       // 地面 Y 坐标（直线高度）
const int   PLAYER_R    = 30;        // 小球半径
const float GRAVITY     = 900.0f;    // 重力加速度（像素/秒²）
const float JUMP_VEL    = -420.0f;   // 跳跃初速度（负值=向上）
const float MOVE_SPEED  = 280.0f;    // 水平移动速度（像素/秒）

// ====================================================================
// 玩家状态枚举
//
// 设计说明：
//   状态机的「状态」是互斥的——玩家在任一时刻只能处于一个状态。
//   每个状态对应角色的一种行为模式。状态之间通过明确的「转换条件」
//   进行切换。这种设计让复杂的行为逻辑变得清晰、可维护。
// ====================================================================
enum class PlayerState {
    IDLE,     // 待机：站在地面上，水平速度为 0
    RUNNING,  // 跑动：在地面上左右移动
    JUMPING,  // 跳跃：在空中向上运动（vy < 0）
    FALLING,  // 下落：在空中向下运动（vy >= 0）
};

// ---- 状态名称（用于屏幕上显示当前状态） ----
const char* StateName(PlayerState s) {
    switch (s) {
        case PlayerState::IDLE:    return "IDLE";
        case PlayerState::RUNNING: return "RUNNING";
        case PlayerState::JUMPING: return "JUMPING";
        case PlayerState::FALLING: return "FALLING";
        default:                   return "???";
    }
}


// ====================================================================
// 渐变蓝小球绘制工具函数
//
// 原理：用多层 FillCircle 从外到内叠加，外层深蓝、内层浅蓝，
//       形成径向渐变效果，模拟立体光照感。
//
// 参数：
//   game - GameLib 实例引用
//   cx, cy - 圆心坐标
//   r      - 半径
// ====================================================================
void DrawGradientBall(GameLib& game, int cx, int cy, int r) {
    // 从最外层画到最内层：外层颜色深，内层覆盖后显现浅色
    for (int i = r; i >= 0; i--) {
        float t = (float)i / r;                     // t=0 圆心 → t=1 边缘
        // 颜色渐变：边缘深蓝 (30,80,220) → 圆心亮蓝白 (180,200,255)
        int red   = (int)(30  + 150 * (1.0f - t));
        int green = (int)(80  + 120 * (1.0f - t));
        int blue  = (int)(220 +  35 * (1.0f - t));
        game.FillCircle(cx, cy, i, COLOR_RGB(red, green, blue));
    }
    // 高光点：圆心附近加一个小白点，增强立体感
    game.FillCircle(cx - 5, cy - 8, 5, COLOR_ARGB(120, 255, 255, 255));
}


// ====================================================================
// Player 类 —— 集成了状态机的玩家角色
//
// 设计意图：
//   将状态机的所有逻辑（状态存储、转换判断、Enter/Update/Exit 回调）
//   全部封装在 Player 类内部。外部主循环只需调用 Update() 和 Draw()，
//   无需关心内部状态管理细节。
//
//   这种「角色 = 数据 + 状态机」的模式在游戏开发中非常常见，
//   可以轻松扩展到更复杂的状态（攻击、受伤、死亡等）。
// ====================================================================
class Player {
public:
    // ---- 公开属性（物理数据） ----
    float x, y;          // 位置（圆心坐标）
    float vx, vy;        // 水平/垂直速度
    bool  onGround;      // 是否站在地面上

    // ---- 状态机核心数据 ----
    PlayerState state;           // 当前状态
    PlayerState prevState;       // 上一帧的状态（用于检测刚切换时）

    // ================================================================
    // 构造函数
    // ================================================================
    Player(float startX, float startY) {
        x = startX;
        y = startY;
        vx = 0;
        vy = 0;
        onGround = true;
        state     = PlayerState::IDLE;
        prevState = PlayerState::IDLE;
        EnterState(state);           // 进入初始状态
        prevState = state;
    }

    // ================================================================
    // 主更新入口（每帧调用一次）
    //
    // 调用顺序经过精心设计，保证同一帧内输入→物理→碰撞→状态的因果链正确：
    //   1. ProcessInput()     — 读取键盘，设置水平速度
    //   2. CheckJumpInput()   — 检测跳跃触发
    //   3. ApplyPhysics(dt)   — 速度和重力更新位置
    //   4. ResolveCollision() — 地面碰撞检测与修正
    //   5. RunStateMachine()  — 根据最新数据决定状态转换
    // ================================================================
    void Update(GameLib& game, float dt) {
        prevState = state;               // 记录上帧状态

        ProcessInput(game);              // 步骤 1：水平输入
        bool jumped = CheckJumpInput(game); // 步骤 2：跳跃检测
        if (jumped) {
            vy = JUMP_VEL;
            onGround = false;
        }
        ApplyPhysics(dt);                // 步骤 3：运动学
        ResolveCollision();              // 步骤 4：碰撞处理
        RunStateMachine(jumped);         // 步骤 5：状态转换
    }

    // ================================================================
    // 绘制（每帧调用一次）
    // ================================================================
    void Draw(GameLib& game) {
        DrawGradientBall(game, (int)x, (int)y, PLAYER_R);
    }

private:
    // ================================================================
    // 步骤 1：处理水平输入
    //
    // 将 ← → 键映射为水平速度。同时按下时抵消（优先右）。
    // ================================================================
    void ProcessInput(GameLib& game) {
        bool left  = game.IsKeyDown(KEY_LEFT);
        bool right = game.IsKeyDown(KEY_RIGHT);

        if (left && !right)       vx = -MOVE_SPEED;
        else if (right && !left)  vx =  MOVE_SPEED;
        else                      vx = 0;
    }

    // ================================================================
    // 步骤 2：检测跳跃触发
    //
    // 使用 IsKeyPressed 而非 IsKeyDown，保证按一次只跳一次。
    // 返回 true 表示本帧触发了跳跃。
    // ================================================================
    bool CheckJumpInput(GameLib& game) {
        return game.IsKeyPressed(KEY_SPACE) && onGround;
    }

    // ================================================================
    // 步骤 3：运动学（速度 + 重力）
    // ================================================================
    void ApplyPhysics(float dt) {
        // 空中才受重力影响
        if (!onGround) {
            vy += GRAVITY * dt;
        }
        // 更新位置
        x += vx * dt;
        y += vy * dt;
    }

    // ================================================================
    // 步骤 4：地面碰撞检测与边界限制
    // ================================================================
    void ResolveCollision() {
        // 地面碰撞：小球底部（y + r）碰到地面线
        float groundSurface = (float)(GROUND_Y - PLAYER_R);
        if (y >= groundSurface && vy >= 0) {
            y = groundSurface;          // 修正到地面
            vy = 0;                      // 垂直速度归零
            onGround = true;
        }

        // 左右边界限制（不穿墙）
        if (x < PLAYER_R)          x = PLAYER_R;
        if (x > WIN_W - PLAYER_R)  x = WIN_W - PLAYER_R;

        // 顶部边界限制
        if (y < PLAYER_R) {
            y = PLAYER_R;
            vy = 0;                      // 撞到天花板，速度归零
        }
    }

    // ================================================================
    // 步骤 5：运行状态机
    //
    // 这是整个 Demo 的核心。根据玩家当前的物理状态（onGround、vx、vy）
    // 和输入（jumped），决定是否需要在四个状态之间切换。
    //
    // 状态转换图（ASCII）：
    //
    //           [按空格]              [vy > 0]
    //   IDLE  ──────────→  JUMPING  ──────────→  FALLING
    //    ↑                   ↑                      │
    //    │                   │                      │
    //    │   [vx != 0]       │   [vx != 0]          │
    //    │                   │                      │
    //    ↓                   │                      │
    //  RUNNING ◄────────────────────────────────────┘
    //           [落地，vx == 0]
    //
    //           落地后 vx != 0 → RUNNING
    //           落地后 vx == 0 → IDLE
    // ================================================================
    void RunStateMachine(bool jumped) {
        // ------------------------------------------------------------
        // 转换规则 1：在地面时
        // ------------------------------------------------------------
        if (onGround) {
            if (vx != 0) {
                TransitionTo(PlayerState::RUNNING);
            } else {
                TransitionTo(PlayerState::IDLE);
            }
        }
        // ------------------------------------------------------------
        // 转换规则 2：在空中时（不在地面）
        // ------------------------------------------------------------
        else {
            if (vy < 0) {
                TransitionTo(PlayerState::JUMPING);
            } else {
                TransitionTo(PlayerState::FALLING);
            }
        }

        // ------------------------------------------------------------
        // 逐状态的特殊行为（每帧执行）
        // 目前四个状态没有额外的每帧行为，保留此结构供扩展。
        //
        // 实际项目中，你可以在这里添加：
        //   - IDLE: 播放待机动画
        //   - RUNNING: 播放跑步动画，调整动画速度
        //   - JUMPING: 播放跳跃上升动画
        //   - FALLING: 播放下落动画，检测是否该播放落地效果
        // ------------------------------------------------------------
        switch (state) {
            case PlayerState::IDLE:
                break;
            case PlayerState::RUNNING:
                break;
            case PlayerState::JUMPING:
                break;
            case PlayerState::FALLING:
                break;
        }
    }

    // ================================================================
    // 状态转换函数
    //
    // 封装了「退出旧状态 → 切换 → 进入新状态」三步流程。
    // 相同状态的重复转换会被忽略，避免不必要的 Enter/Exit 调用。
    // ================================================================
    void TransitionTo(PlayerState newState) {
        if (newState == state) return;      // 相同状态不切换

        ExitState(state);                    // 1. 退出当前状态
        state = newState;                    // 2. 更新状态
        EnterState(state);                   // 3. 进入新状态
    }

    // ================================================================
    // 进入状态回调
    //
    // 当状态机切换到该状态时调用一次。
    // 在这里做「刚进入该状态时需要初始化的事情」。
    //
    // 典型用途：
    //   - JUMPING: 播放起跳音效
    //   - FALLING: 开始计算下落时间（用于落地伤害判定）
    //   - IDLE:   切换为待机动画
    // ================================================================
    void EnterState(PlayerState s) {
        switch (s) {
            case PlayerState::IDLE:
                // IDLE 进入：无需特殊初始化
                break;
            case PlayerState::RUNNING:
                // RUNNING 进入：无需特殊初始化
                break;
            case PlayerState::JUMPING:
                // JUMPING 进入：跳跃已由 CheckJumpInput 设置了 vy
                // 这里可以播放跳跃音效、设置跳跃动画帧等
                break;
            case PlayerState::FALLING:
                // FALLING 进入：从上升转为下落
                // 这里可以播放「到达顶点」的微效果
                break;
        }
    }

    // ================================================================
    // 退出状态回调
    //
    // 当状态机离开该状态时调用一次。
    // 在这里做「离开该状态时需要清理/收尾的事情」。
    //
    // 典型用途：
    //   - RUNNING: 停止跑步粒子特效
    //   - JUMPING: 记录跳跃结束时间
    // ================================================================
    void ExitState(PlayerState s) {
        switch (s) {
            case PlayerState::IDLE:
                break;
            case PlayerState::RUNNING:
                break;
            case PlayerState::JUMPING:
                break;
            case PlayerState::FALLING:
                break;
        }
    }
};


// ====================================================================
// 辅助函数：绘制 HUD（状态信息叠加层）
// ====================================================================
void DrawHUD(GameLib& game, const Player& player) {
    int leftX = 15;
    int lineY = 10;
    const int LINE_H = 18;

    // 当前状态（高亮显示）
    game.DrawPrintf(leftX, lineY, COLOR_YELLOW,
        "State: %s", StateName(player.state));
    lineY += LINE_H;

    // 上帧状态（用于观察切换时机）
    game.DrawPrintf(leftX, lineY, COLOR_GRAY,
        "Prev:  %s", StateName(player.prevState));
    lineY += LINE_H;

    // 物理数据
    game.DrawPrintf(leftX, lineY, COLOR_WHITE,
        "Pos: (%.0f, %.0f)", player.x, player.y);
    lineY += LINE_H;

    game.DrawPrintf(leftX, lineY, COLOR_WHITE,
        "Vel: (%.0f, %.0f)", player.vx, player.vy);
    lineY += LINE_H;

    // 地面状态
    game.DrawPrintf(leftX, lineY,
        player.onGround ? COLOR_GREEN : COLOR_RED,
        "Ground: %s", player.onGround ? "YES" : "NO");
    lineY += LINE_H + 5;

    // 操作提示
    game.DrawPrintf(leftX, lineY, COLOR_GRAY,
        "Controls:");
    lineY += LINE_H;
    game.DrawPrintf(leftX + 10, lineY, COLOR_GRAY,
        "LEFT / RIGHT  =  Move");
    lineY += LINE_H;
    game.DrawPrintf(leftX + 10, lineY, COLOR_GRAY,
        "SPACE         =  Jump");
    lineY += LINE_H;
    game.DrawPrintf(leftX + 10, lineY, COLOR_GRAY,
        "ESC           =  Quit");
}


// ====================================================================
// 辅助函数：绘制状态转换图（右下角）
// ====================================================================
void DrawStateDiagram(GameLib& game) {
    int x = WIN_W - 370;
    int y = WIN_H - 130;
    const int LH = 18;

    game.DrawPrintf(x, y, COLOR_YELLOW,
        "State Transition Diagram:");
    y += LH + 2;

    game.DrawPrintf(x, y, COLOR_WHITE,
        "  IDLE  --[vx != 0]---------->  RUNNING");
    y += LH;
    game.DrawPrintf(x, y, COLOR_WHITE,
        "  IDLE  --[SPACE]------------>  JUMPING");
    y += LH;
    game.DrawPrintf(x, y, COLOR_WHITE,
        "  RUNNING --[SPACE]---------->  JUMPING");
    y += LH;
    game.DrawPrintf(x, y, COLOR_WHITE,
        "  JUMPING --[vy >= 0]-------->  FALLING");
    y += LH;
    game.DrawPrintf(x, y, COLOR_WHITE,
        "  FALLING --[land]----------->  IDLE / RUNNING");
}


// ====================================================================
// 辅助函数：绘制场景（地面 + 装饰）
// ====================================================================
void DrawScene(GameLib& game) {
    // 地面以下填充深绿色（表示土壤）
    game.FillRect(0, GROUND_Y, WIN_W, WIN_H - GROUND_Y, COLOR_RGB(30, 60, 20));

    // 地面线（亮绿色粗线）
    game.DrawLine(0, GROUND_Y, WIN_W, GROUND_Y, COLOR_RGB(80, 200, 80));

    // 地面标签
    game.DrawText(WIN_W / 2 - 25, GROUND_Y + 5, "GROUND", COLOR_RGB(60, 140, 60));
}


// ====================================================================
// 主程序
// ====================================================================
int main() {
    // ---- 初始化窗口 ----
    GameLib game;
    game.Open(WIN_W, WIN_H, "FSM Demo - 状态机学习 (渐变蓝小球)", true);

    // ---- 创建玩家：初始位置在地面上方 ----
    Player player((float)(WIN_W / 2), (float)(GROUND_Y - PLAYER_R));

    // ---- 主循环 ----
    while (!game.IsClosed()) {
        float dt = (float)game.GetDeltaTime();

        // 限制最大 dt，防止卡顿时物理跳跃异常
        if (dt > 0.1f) dt = 0.1f;

        // ---- 更新玩家 ----
        player.Update(game, dt);

        // ---- 渲染 ----
        game.Clear(COLOR_RGB(15, 15, 35));   // 深蓝紫色夜空背景

        DrawScene(game);                      // 地面
        player.Draw(game);                    // 渐变蓝小球
        DrawHUD(game, player);                // 状态信息
        DrawStateDiagram(game);               // 状态转换图

        // ---- 帧同步 ----
        game.Update();
        game.WaitFrame(60);                   // 目标 60 FPS
    }

    return 0;
}
