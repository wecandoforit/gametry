#define GAMELIB_IMPLEMENTATION
#include "../GameLib.h"
#include <vector>
#include <algorithm>
#include <math.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>

// ============================================================
// Layout
// ============================================================
const int WIN_W  = 640;
const int WIN_H  = 480;
const int BRK_W  = 70;
const int BRK_H  = 22;
const int BRK_GAP= 4;
const int BRK_STEP= BRK_W + BRK_GAP;
const int GRID_COLS = 8;
const int GRID_X  = (WIN_W - (GRID_COLS * BRK_STEP - BRK_GAP)) / 2;
const int GRID_Y  = 55;
const int PAD_W   = 100;
const int PAD_H   = 16;
const int PAD_Y   = 440;
const int BALL_R  = 7;
const float BALL_SPEED = 300.f;  // pixels/sec

// ============================================================
// Brick types
// ============================================================
enum BrickType { BRK_NONE=0, BRK_NORMAL, BRK_TOUGH, BRK_UNBREAKABLE, BRK_EXPLOSIVE };

// ============================================================
// Power-up types
// ============================================================
enum PowerType { PUP_NONE=0, PUP_EXTEND, PUP_SLOW, PUP_MULTI, PUP_LIFE, PUP_STICKY, PUP_COUNT };

// ============================================================
// Ball
// ============================================================
struct Ball {
    float x, y, vx, vy;
    float speed = BALL_SPEED;
    bool  alive = false;
    bool  stuck = false;  // attached to paddle
};

// ============================================================
// Game state
// ============================================================
struct Game {
    // Paddle
    float padX = WIN_W / 2.f;
    float padW = PAD_W;

    // Balls
    std::vector<Ball> balls;
    int  activeBalls = 0;

    // Bricks
    struct Brick {
        float x, y;
        int   type  = BRK_NONE;
        int   hits  = 1;
        int   color = 0;
    };
    std::vector<Brick> bricks;

    // Power-ups
    struct PowerUp {
        float x, y;
        int   type = PUP_NONE;
        bool  alive = false;
    };
    std::vector<PowerUp> powerUps;

    // Status
    int   lives    = 3;
    int   score    = 0;
    int   level    = 1;
    int   combo    = 0;       // consecutive hits without paddle bounce
    float comboTimer = 0;
    bool  launching= true;    // ball on paddle, waiting to launch
    bool  gameOver = false;
    bool  paused   = false;

    // Power-up timers
    float extendTimer = 0;
    float slowTimer   = 0;
    float stickyTimer = 0;
};

// ============================================================
// Brick colors per row
// ============================================================
static const uint32_t ROW_COLORS[] = {
    COLOR_RGB(220, 50, 50),   // red
    COLOR_RGB(230, 120, 30),  // orange
    COLOR_RGB(240, 200, 40),  // yellow
    COLOR_RGB(60, 180, 60),   // green
    COLOR_RGB(50, 160, 200),  // cyan
    COLOR_RGB(130, 60, 200),  // purple
    COLOR_RGB(200, 60, 150),  // pink
    COLOR_RGB(80, 180, 200),  // teal
};
static const int ROW_SCORES[] = { 70, 60, 50, 40, 30, 20, 10, 5 };

// Power-up colors
static const uint32_t PUP_COLORS[] = {
    0,
    COLOR_RGB(100, 220, 100), // extend - green
    COLOR_RGB(100, 100, 240), // slow - blue
    COLOR_RGB(240, 200, 50),  // multi - gold
    COLOR_RGB(240, 80,  80),  // life - red
    COLOR_RGB(200, 130, 60),  // sticky - orange
};
static const char* PUP_LABELS[] = {
    "", "加长", "减速", "多球", "+命", "粘粘"
};

// ============================================================
// Level generation
// ============================================================
void GenerateLevel(Game& g) {
    g.bricks.clear();
    g.powerUps.clear();

    int rows = 5 + g.level;
    if (rows > 8) rows = 8;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            Game::Brick b;
            b.x = (float)(GRID_X + c * BRK_STEP);
            b.y = (float)(GRID_Y + r * (BRK_H + 2));
            b.color = (int)ROW_COLORS[r % 8];
            b.type  = BRK_NORMAL;
            b.hits  = 1;

            // Random special bricks
            int roll = rand() % 100;
            if (roll < 8 && r >= 2) {
                b.type = BRK_TOUGH;
                b.hits = 2 + (rand() % 2);  // 2-3 hits
            } else if (roll < 12 && r >= 1 && r < rows - 1) {
                b.type = BRK_UNBREAKABLE;
                b.hits = 99;
            } else if (roll < 16 && r >= 1) {
                b.type = BRK_EXPLOSIVE;
                b.hits = 1;
            }

            g.bricks.push_back(b);
        }
    }

    // Ensure at least some breakable bricks exist
    bool hasBreakable = false;
    for (auto& b : g.bricks)
        if (b.type != BRK_UNBREAKABLE) { hasBreakable = true; break; }
    if (!hasBreakable && !g.bricks.empty()) {
        g.bricks[0].type = BRK_NORMAL;
        g.bricks[0].hits = 1;
    }
}

// ============================================================
// Add ball
// ============================================================
void AddBall(Game& g, float x, float y, float vx, float vy, bool stuck) {
    Ball b;
    b.x = x; b.y = y; b.vx = vx; b.vy = vy;
    b.alive = true; b.stuck = stuck;
    g.balls.push_back(b);
    g.activeBalls++;
}

// ============================================================
// Reset after losing a life
// ============================================================
void ResetBall(Game& g) {
    g.balls.clear();
    g.activeBalls = 0;
    g.launching = true;
    g.combo = 0;
    g.comboTimer = 0;
    g.extendTimer = 0;
    g.slowTimer = 0;
    g.stickyTimer = 0;
    g.padW = PAD_W;
    AddBall(g, g.padX, PAD_Y - BALL_R - 2, 0, 0, true);
}

// ============================================================
// Spawn power-up
// ============================================================
void SpawnPowerUp(Game& g, float x, float y) {
    int r = rand() % 100;
    int t = PUP_NONE;
    if      (r < 30) t = PUP_EXTEND;
    else if (r < 55) t = PUP_SLOW;
    else if (r < 70) t = PUP_MULTI;
    else if (r < 82) t = PUP_STICKY;
    else if (r < 92) t = PUP_LIFE;

    if (t == PUP_NONE) return;

    Game::PowerUp p;
    p.x = x; p.y = y; p.type = t; p.alive = true;
    g.powerUps.push_back(p);
}

// ============================================================
// Destroy brick + effects
// ============================================================
void DestroyBrick(Game& g, int idx) {
    auto& b = g.bricks[idx];
    if (b.type == BRK_NONE) return;

    int scoreGain = (b.type == BRK_EXPLOSIVE) ? 30 : ROW_SCORES[(int)((b.y - GRID_Y) / (BRK_H + 2)) % 8];
    g.score += scoreGain;

    // Combo bonus
    g.combo++;
    g.comboTimer = 2.f;
    if (g.combo > 1) g.score += g.combo * 5;

    // Explosive: destroy neighbors
    if (b.type == BRK_EXPLOSIVE) {
        for (int i = 0; i < (int)g.bricks.size(); i++) {
            if (i == idx) continue;
            auto& nb = g.bricks[i];
            if (nb.type == BRK_NONE) continue;
            float dx = nb.x + BRK_W/2 - (b.x + BRK_W/2);
            float dy = nb.y + BRK_H/2 - (b.y + BRK_H/2);
            if (fabsf(dx) <= BRK_STEP * 1.2f && fabsf(dy) <= (BRK_H + 2) * 1.2f) {
                if (nb.type == BRK_UNBREAKABLE) continue;
                SpawnPowerUp(g, nb.x + BRK_W/2, nb.y + BRK_H/2);
                nb.type = BRK_NONE;
            }
        }
    }

    // Spawn power-up (25% chance)
    if (b.type != BRK_UNBREAKABLE && (rand() % 100) < 25)
        SpawnPowerUp(g, b.x + BRK_W/2, b.y + BRK_H/2);

    b.type = BRK_NONE;
}

// ============================================================
// Apply power-up
// ============================================================
void ApplyPowerUp(Game& g, int type) {
    switch (type) {
    case PUP_EXTEND:
        g.extendTimer = 20.f;
        g.padW = PAD_W * 1.6f;
        break;
    case PUP_SLOW:
        g.slowTimer = 20.f;
        for (auto& b : g.balls)
            if (b.alive) b.speed = BALL_SPEED * 0.55f;
        break;
    case PUP_MULTI: {
        // Create 2 extra balls from first active ball
        Ball* src = nullptr;
        for (auto& b : g.balls) { if (b.alive && !b.stuck) { src = &b; break; } }
        if (src) {
            float sp = src->speed;
            float ang = atan2f(src->vy, src->vx);
            AddBall(g, src->x, src->y, sp * cosf(ang + 0.4f), sp * sinf(ang + 0.4f), false);
            AddBall(g, src->x, src->y, sp * cosf(ang - 0.4f), sp * sinf(ang - 0.4f), false);
        }
        break;
    }
    case PUP_LIFE:
        g.lives++;
        break;
    case PUP_STICKY:
        g.stickyTimer = 20.f;
        break;
    }
}

// ============================================================
// Check: all breakable bricks destroyed?
// ============================================================
bool AllBreakableCleared(const Game& g) {
    for (const auto& b : g.bricks)
        if (b.type != BRK_NONE && b.type != BRK_UNBREAKABLE)
            return false;
    return true;
}

// ============================================================
// Rendering
// ============================================================
void DrawBrick(GameLib& gl, const Game::Brick& b) {
    if (b.type == BRK_NONE) return;

    uint32_t c;
    if (b.type == BRK_UNBREAKABLE) {
        c = COLOR_RGB(150, 150, 150);
    } else if (b.type == BRK_TOUGH) {
        // Darker shade, changes as hits decrease
        int r = (b.color >> 16) & 0xFF;
        int g = (b.color >> 8) & 0xFF;
        int bcol = b.color & 0xFF;
        float ratio = b.hits / 3.f;
        c = COLOR_RGB((int)(r * ratio), (int)(g * ratio), (int)(bcol * ratio));
    } else if (b.type == BRK_EXPLOSIVE) {
        c = COLOR_RGB(255, 140, 30);
    } else {
        c = (uint32_t)b.color;
    }

    gl.FillRect((int)b.x + 1, (int)b.y + 1, BRK_W - 2, BRK_H - 2, c);

    // Highlight
    int rr = ((c >> 16) & 0xFF) + 60; if (rr > 255) rr = 255;
    int gg = ((c >> 8)  & 0xFF) + 60; if (gg > 255) gg = 255;
    int bb = ( c        & 0xFF) + 60; if (bb > 255) bb = 255;
    uint32_t hl = COLOR_RGB(rr, gg, bb);
    gl.FillRect((int)b.x + 1, (int)b.y + 1, BRK_W - 2, 4, hl);

    // Unbreakable pattern
    if (b.type == BRK_UNBREAKABLE) {
        gl.DrawLine((int)b.x + 5,  (int)b.y + 5,  (int)b.x + BRK_W - 5, (int)b.y + BRK_H - 5, COLOR_RGB(80,80,80));
        gl.DrawLine((int)b.x + BRK_W - 5, (int)b.y + 5, (int)b.x + 5,  (int)b.y + BRK_H - 5, COLOR_RGB(80,80,80));
    }
    // Tough: show hit indicators
    if (b.type == BRK_TOUGH && b.hits >= 2) {
        for (int i = 0; i < b.hits - 1; i++) {
            gl.FillCircle((int)b.x + 12 + i * 18, (int)b.y + BRK_H/2, 3, COLOR_RGB(255,255,255));
        }
    }
}

void DrawPaddle(GameLib& gl, const Game& g) {
    int px = (int)(g.padX - g.padW / 2);
    int pw = (int)g.padW;

    // Shadow
    gl.FillRect(px + 2, PAD_Y + 2, pw, PAD_H, COLOR_RGB(30, 30, 60));
    // Body
    gl.FillRect(px, PAD_Y, pw, PAD_H, COLOR_RGB(100, 140, 240));
    // Highlight
    gl.FillRect(px + 2, PAD_Y + 2, pw - 4, 5, COLOR_RGB(150, 190, 255));
    // Border
    gl.DrawRect(px, PAD_Y, pw, PAD_H, COLOR_RGB(50, 80, 160));

    // Extended indicator
    if (g.padW > PAD_W) {
        gl.DrawRect(px, PAD_Y, pw, PAD_H, COLOR_RGB(100, 220, 100));
    }
}

void DrawBall(GameLib& gl, const Ball& b) {
    if (!b.alive) return;
    int cx = (int)b.x, cy = (int)b.y;

    // Glow
    gl.FillCircle(cx, cy, BALL_R + 3, COLOR_RGB(255, 255, 200));
    gl.FillCircle(cx, cy, BALL_R + 2, COLOR_RGB(255, 255, 150));
    // Core
    gl.FillCircle(cx, cy, BALL_R, COLOR_RGB(255, 255, 255));
    // Specular
    gl.FillCircle(cx - 2, cy - 2, 3, COLOR_RGB(255, 255, 255));
}

void DrawPowerUp(GameLib& gl, const Game::PowerUp& p) {
    if (!p.alive) return;
    int cx = (int)p.x, cy = (int)p.y;

    // Body
    gl.FillCircle(cx, cy, 10, PUP_COLORS[p.type]);
    gl.DrawCircle(cx, cy, 10, COLOR_WHITE);
    // Label
    const char* lbl = PUP_LABELS[p.type];
    int tw = gl.GetTextWidthFont(lbl, 14) / 2;
    gl.DrawTextFont(cx - tw, cy - 7, lbl, COLOR_WHITE, 14);
}

void DrawHUD(GameLib& gl, const Game& g) {
    char buf[64];
    int fs = 18;

    // Top bar background
    gl.FillRect(0, 0, WIN_W, 40, COLOR_RGB(20, 20, 35));

    // Score
    snprintf(buf, sizeof(buf), "分数: %d", g.score);
    gl.DrawTextFont(10, 10, buf, COLOR_RGB(255, 220, 80), fs);

    // Lives
    snprintf(buf, sizeof(buf), "生命: %d", g.lives);
    gl.DrawTextFont(180, 10, buf, COLOR_RGB(255, 100, 100), fs);

    // Level
    snprintf(buf, sizeof(buf), "第 %d 关", g.level);
    gl.DrawTextFont(340, 10, buf, COLOR_RGB(150, 220, 255), fs);

    // Combo
    if (g.combo >= 3) {
        snprintf(buf, sizeof(buf), "连击 x%d!", g.combo);
        gl.DrawTextFont(480, 10, buf, COLOR_RGB(255, 200, 50), fs);
    }

    // Active power-up indicators
    int px = 10;
    if (g.extendTimer > 0) {
        snprintf(buf, sizeof(buf), "[加长 %.0fs]", g.extendTimer);
        gl.DrawTextFont(px, 430, buf, COLOR_RGB(100, 220, 100), 14);
        px += 90;
    }
    if (g.slowTimer > 0) {
        snprintf(buf, sizeof(buf), "[减速 %.0fs]", g.slowTimer);
        gl.DrawTextFont(px, 430, buf, COLOR_RGB(100, 100, 240), 14);
        px += 90;
    }
    if (g.stickyTimer > 0) {
        snprintf(buf, sizeof(buf), "[粘粘 %.0fs]", g.stickyTimer);
        gl.DrawTextFont(px, 430, buf, COLOR_RGB(200, 130, 60), 14);
    }
}

void DrawOverlay(GameLib& gl, const char* title, const char* sub) {
    // Dim background
    for (int y = 0; y < WIN_H; y += 3)
        for (int x = 0; x < WIN_W; x += 3)
            gl.SetPixel(x, y, COLOR_RGB(0, 0, 0));

    int ts = 36, ss = 20;
    int tw = gl.GetTextWidthFont(title, ts);
    gl.DrawTextFont((WIN_W - tw) / 2, WIN_H/2 - 30, title, COLOR_RGB(255, 220, 80), ts);

    if (sub) {
        int sw = gl.GetTextWidthFont(sub, ss);
        gl.DrawTextFont((WIN_W - sw) / 2, WIN_H/2 + 20, sub, COLOR_RGB(200, 200, 200), ss);
    }
}

// ============================================================
// Main
// ============================================================
int main() {
    GameLib gl;
    gl.Open(WIN_W, WIN_H, "Breakout", true);

    Game g;
    srand((unsigned)time(nullptr));
    GenerateLevel(g);
    AddBall(g, g.padX, PAD_Y - BALL_R - 2, 0, 0, true);

    float lastTime = gl.GetTime();

    while (!gl.IsClosed()) {
        float now = gl.GetTime();
        float dt = now - lastTime;
        lastTime = now;
        if (dt > 0.1f) dt = 0.016f; // clamp to avoid physics jumps

        // --- Input ---
        if (!g.gameOver) {
            float moveSpeed = 500.f;

            if (gl.IsKeyDown(KEY_LEFT)  || gl.IsKeyDown(KEY_A))
                g.padX -= moveSpeed * dt;
            if (gl.IsKeyDown(KEY_RIGHT) || gl.IsKeyDown(KEY_D))
                g.padX += moveSpeed * dt;

            // Clamp paddle
            if (g.padX - g.padW/2 < 0) g.padX = g.padW/2;
            if (g.padX + g.padW/2 > WIN_W) g.padX = WIN_W - g.padW/2;

            // Launch ball
            if (g.launching && (gl.IsKeyPressed(KEY_SPACE) || gl.IsMousePressed(MOUSE_LEFT))) {
                g.launching = false;
                for (auto& b : g.balls) {
                    if (b.stuck) {
                        b.stuck = false;
                        float ang = -1.2f + (rand() % 100) * 0.6f / 100.f; // random angle
                        b.vx = b.speed * cosf(ang);
                        b.vy = b.speed * sinf(ang);
                    }
                }
            }

            // Pause
            if (gl.IsKeyPressed(KEY_P))
                g.paused = !g.paused;
        }

        // Restart
        if (g.gameOver && gl.IsKeyPressed(KEY_SPACE)) {
            g = Game();
            srand((unsigned)time(nullptr));
            GenerateLevel(g);
            AddBall(g, g.padX, PAD_Y - BALL_R - 2, 0, 0, true);
            lastTime = gl.GetTime();
        }

        // --- N key: skip level ---
        if (gl.IsKeyPressed(KEY_N) && !g.gameOver) {
            g.level++;
            GenerateLevel(g);
            ResetBall(g);
            lastTime = gl.GetTime();
        }

        // --- Update ---
        if (!g.paused && !g.gameOver) {
            // Power-up timers
            if (g.extendTimer > 0) { g.extendTimer -= dt; if (g.extendTimer <= 0) g.padW = PAD_W; }
            if (g.slowTimer > 0) {
                g.slowTimer -= dt;
                if (g.slowTimer <= 0)
                    for (auto& b : g.balls) b.speed = BALL_SPEED;
            }
            if (g.stickyTimer > 0) g.stickyTimer -= dt;

            // Combo timer decay
            if (g.comboTimer > 0) {
                g.comboTimer -= dt;
                if (g.comboTimer <= 0) g.combo = 0;
            }

            // Update balls
            for (auto& b : g.balls) {
                if (!b.alive) continue;

                if (b.stuck) {
                    b.x = g.padX;
                    b.y = PAD_Y - BALL_R - 2;
                    continue;
                }

                b.x += b.vx * dt;
                b.y += b.vy * dt;

                // Wall bounces
                if (b.x - BALL_R < 0) { b.x = BALL_R; b.vx = -b.vx; }
                if (b.x + BALL_R > WIN_W) { b.x = WIN_W - BALL_R; b.vx = -b.vx; }
                if (b.y - BALL_R < 0) { b.y = BALL_R; b.vy = -b.vy; }

                // Fall below screen → die
                if (b.y > WIN_H + 20) {
                    b.alive = false;
                    g.activeBalls--;
                    g.combo = 0;
                }

                // Paddle bounce
                if (b.vy > 0 && b.y + BALL_R >= PAD_Y &&
                    b.y + BALL_R <= PAD_Y + PAD_H + 5 &&
                    b.x >= g.padX - g.padW/2 && b.x <= g.padX + g.padW/2) {

                    if (g.stickyTimer > 0 && !b.stuck) {
                        b.stuck = true;
                        b.vx = 0; b.vy = 0;
                        continue;
                    }

                    g.combo = 0; // reset combo on paddle bounce

                    // Angle based on where ball hits paddle
                    float relX = (b.x - g.padX) / (g.padW / 2.f);
                    if (relX < -1.f) relX = -1.f;
                    if (relX >  1.f) relX =  1.f;
                    float angle = relX * 1.1f; // ~63 degrees max
                    b.vx =  b.speed * sinf(angle);
                    b.vy = -b.speed * cosf(angle);
                    b.y  = PAD_Y - BALL_R - 1;
                }

                // Brick collisions
                for (int i = 0; i < (int)g.bricks.size(); i++) {
                    auto& br = g.bricks[i];
                    if (br.type == BRK_NONE) continue;

                    // AABB collision with ball circle
                    float bx = br.x, by = br.y, bw = BRK_W, bh = BRK_H;
                    float closestX = b.x;
                    if (closestX < bx) closestX = bx;
                    if (closestX > bx + bw) closestX = bx + bw;
                    float closestY = b.y;
                    if (closestY < by) closestY = by;
                    if (closestY > by + bh) closestY = by + bh;

                    float dx = b.x - closestX;
                    float dy = b.y - closestY;
                    if (dx*dx + dy*dy > BALL_R * BALL_R) continue;

                    // Collision! Determine bounce direction
                    if (fabsf(dx) > fabsf(dy))
                        b.vx = -b.vx;
                    else
                        b.vy = -b.vy;

                    // Push ball out
                    float overlap = BALL_R - sqrtf(dx*dx + dy*dy);
                    float nx = (dx != 0) ? dx / fabsf(dx) : 0;
                    float ny = (dy != 0) ? dy / fabsf(dy) : 0;
                    if (fabsf(dx) > fabsf(dy)) b.x += nx * (overlap + 1);
                    else                       b.y += ny * (overlap + 1);

                    // Damage brick
                    if (br.type == BRK_UNBREAKABLE) continue; // no damage
                    if (br.type == BRK_TOUGH) {
                        br.hits--;
                        if (br.hits <= 0) DestroyBrick(g, i);
                    } else {
                        DestroyBrick(g, i);
                    }
                    break; // one collision per ball per frame
                }
            }

            // Remove dead balls
            g.balls.erase(
                std::remove_if(g.balls.begin(), g.balls.end(),
                    [](const Ball& b) { return !b.alive; }),
                g.balls.end());

            // All balls lost → lose life
            if (g.activeBalls <= 0) {
                g.lives--;
                if (g.lives <= 0) {
                    g.gameOver = true;
                } else {
                    ResetBall(g);
                }
            }

            // Update power-ups
            for (auto& p : g.powerUps) {
                if (!p.alive) continue;
                p.y += 120.f * dt; // fall speed

                // Check paddle collision
                if (p.y + 10 >= PAD_Y && p.y - 10 <= PAD_Y + PAD_H &&
                    p.x >= g.padX - g.padW/2 && p.x <= g.padX + g.padW/2) {
                    ApplyPowerUp(g, p.type);
                    p.alive = false;
                    continue;
                }
                // Fall off screen
                if (p.y > WIN_H + 20) p.alive = false;
            }
            g.powerUps.erase(
                std::remove_if(g.powerUps.begin(), g.powerUps.end(),
                    [](const Game::PowerUp& p) { return !p.alive; }),
                g.powerUps.end());

            // Level complete?
            if (AllBreakableCleared(g)) {
                g.level++;
                GenerateLevel(g);
                ResetBall(g);
                g.score += 500; // level bonus
            }
        }

        // --- Render ---
        gl.Clear(COLOR_RGB(15, 15, 30)); // dark background

        // Stars effect
        for (int i = 0; i < 30; i++) {
            int sx = (i * 197 + 53) % WIN_W;
            int sy = (i * 311 + 97) % WIN_H;
            gl.SetPixel(sx, sy, COLOR_RGB(60, 60, 90));
        }

        for (const auto& b : g.bricks) DrawBrick(gl, b);
        DrawPaddle(gl, g);
        for (const auto& b : g.balls) DrawBall(gl, b);
        for (const auto& p : g.powerUps) DrawPowerUp(gl, p);
        DrawHUD(gl, g);

        if (g.gameOver)
            DrawOverlay(gl, "游戏结束", "按空格键重新开始");
        else if (g.paused)
            DrawOverlay(gl, "暂停", "按 P 键继续");
        else if (g.launching)
            DrawOverlay(gl, "准备", "按空格键发球");

        gl.Update();
        gl.WaitFrame(60);
    }

    return 0;
}
