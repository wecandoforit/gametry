//=====================================================================
// Water Ripple Demo — 水滴落水溅射 + 水面波动
//
// 设计原则：完全解耦，每个子系统独立
//   - WaterSurface : 1D 波动方程 + 白线渲染
//   - Drop         : 单个水滴物理（自由落体 + 尾迹）
//   - Particle     : 溅射粒子（初始速度 + 重力 + 衰减）
//   - main()       : 调度 + 渲染
//
// 编译：g++ -o main.exe main.cpp -mwindows -std=c++11 -O2
//=====================================================================

#define GAMELIB_IMPLEMENTATION
#include "../GameLib.h"

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <vector>

// ============================================================
// 常量
// ============================================================
const int   WIN_W        = 800;
const int   WIN_H        = 600;
const int   WATER_Y      = 420;          // 水面基准 Y 坐标
const int   WAVE_COLS    = WIN_W;        // 水面波动列数（每像素一列）
const float WAVE_DAMPING = 0.968f;       // 波衰减系数
const float GRAVITY      = 520.0f;       // 水滴重力加速度 (px/s²)
const float PARTICLE_G   = 280.0f;       // 粒子重力加速度
const float DROP_SPAWN_MIN = 1.2f;       // 水滴最小生成间隔（秒）
const float DROP_SPAWN_MAX = 2.5f;       // 水滴最大生成间隔
const int   DROP_RADIUS_MIN = 3;
const int   DROP_RADIUS_MAX = 5;
const int   SPLASH_COUNT_MIN = 12;       // 溅射粒子最少数量
const int   SPLASH_COUNT_MAX = 20;       // 溅射粒子最多数量


// ============================================================
// 工具函数
// ============================================================
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int   clampi(int v, int lo, int hi)   { return v < lo ? lo : (v > hi ? hi : v); }
inline float randf(float lo, float hi)       { return lo + (float)rand() / RAND_MAX * (hi - lo); }


// ============================================================
// WaterSurface — 1D 水面波动模拟
//
// 双缓冲 1D 波方程：
//   next[x] = [(cur[x-1] + cur[x+1]) / 2  -  prev[x]] * damping
//
// 接口：
//   update()             — 传播一帧
//   impact(cx, str, r)   — 在 cx 列施加强度 str、半径 r 的冲击
//   getHeight(x)          — 获取第 x 列当前高度偏移
//   draw(g, baseY)        — 绘制白色折线水面
// ============================================================
class WaterSurface {
public:
    WaterSurface() : m_curIdx(0) {
        m_buf[0].resize(WAVE_COLS, 0.0f);
        m_buf[1].resize(WAVE_COLS, 0.0f);
    }

    // ---- 波传播 ----
    void Update() {
        float *cur  = m_buf[m_curIdx].data();
        float *prev = m_buf[1 - m_curIdx].data();
        float *next = prev;  // 复用上一帧缓冲区写新结果

        for (int x = 1; x < WAVE_COLS - 1; x++) {
            next[x] = ((cur[x - 1] + cur[x + 1]) * 0.5f - prev[x]) * WAVE_DAMPING;

            // 微小值清零（防止无限 lingering）
            if (fabsf(next[x]) < 0.04f) next[x] = 0.0f;
        }
        // 边界衰减到 0
        next[0] = 0.0f;
        next[WAVE_COLS - 1] = 0.0f;

        m_curIdx = 1 - m_curIdx;
    }

    // ---- 施加冲击 ----
    void Impact(int cx, float strength, int radius) {
        float *cur = m_buf[m_curIdx].data();
        int r2 = radius * radius;
        for (int dx = -radius; dx <= radius; dx++) {
            int x = cx + dx;
            if (x < 0 || x >= WAVE_COLS) continue;
            float w = expf(-(float)(dx * dx) / (float)r2 * 2.5f);
            cur[x] += strength * w;
        }
    }

    // ---- 查询高度 ----
    float GetHeight(int x) const {
        if (x < 0 || x >= WAVE_COLS) return 0.0f;
        return m_buf[m_curIdx][x];
    }

    // ---- 渲染白色折线水面 ----
    void Draw(GameLib& g, int baseY) const {
        const float *cur = m_buf[m_curIdx].data();

        // 水面主体：白色折线
        for (int x = 0; x < WAVE_COLS - 1; x++) {
            int y0 = baseY + (int)cur[x];
            int y1 = baseY + (int)cur[x + 1];
            g.DrawLine(x, y0, x + 1, y1, COLOR_WHITE);
        }

        // 水面下方轻微填充（1px 宽淡蓝线，增强可见性）
        for (int x = 0; x < WAVE_COLS; x++) {
            int y = baseY + (int)cur[x];
            if (cur[x] < -1.0f) {
                int endY = baseY;
                if (y < endY) {
                    for (int py = y + 1; py <= endY; py++) {
                        g.SetPixel(x, py, COLOR_ARGB(40, 100, 180, 255));
                    }
                }
            }
        }
    }

private:
    std::vector<float> m_buf[2];
    int m_curIdx;
};


// ============================================================
// Drop — 单个水滴
//
// 状态机：FALLING → 撞击瞬间 → 移除
// 尾迹：存储最近 4 帧的 Y 坐标
// ============================================================
struct Drop {
    float x, y;          // 当前位置
    float vy;            // 垂直速度（向下为正）
    float radius;        // 水滴半径
    int   prevY[4];      // 尾迹历史（帧级）
    bool  alive;

    Drop() : x(0), y(0), vy(0), radius(4), alive(false) {
        for (int i = 0; i < 4; i++) prevY[i] = 0;
    }

    void Spawn(float sx, float sy, float r) {
        x = sx; y = sy; vy = 0; radius = r; alive = true;
        for (int i = 0; i < 4; i++) prevY[i] = (int)sy;
    }

    // 返回 true 表示本帧撞击水面
    bool Update(float dt, int waterY, float waveH) {
        if (!alive) return false;

        // 存储尾迹历史
        for (int i = 3; i > 0; i--) prevY[i] = prevY[i - 1];
        prevY[0] = (int)y;

        // 物理更新
        vy += GRAVITY * dt;
        y  += vy * dt;

        // 检测撞击水面
        float surfaceY = (float)waterY + waveH;
        if (y + radius >= surfaceY) {
            alive = false;
            return true;  // 撞击！
        }
        return false;
    }

    void Draw(GameLib& g) const {
        if (!alive) return;

        // 尾迹（从旧到新：越来越亮）
        for (int i = 3; i >= 0; i--) {
            float t = (float)(3 - i) / 3.0f;  // 0=旧, 1=新
            int alpha = (int)(60.0f + t * 120.0f);
            int r = (int)(radius * (0.4f + t * 0.6f));
            uint32_t c = COLOR_ARGB(clampi(alpha, 0, 255), 180, 210, 255);
            g.FillCircle((int)x, prevY[i], r, c);
        }

        // 水滴本体
        uint32_t bodyColor = COLOR_ARGB(220, 200, 225, 255);
        g.FillCircle((int)x, (int)y, (int)radius, bodyColor);

        // 高光点
        g.FillCircle((int)x - (int)radius/3, (int)y - (int)radius/3,
                     std::max(1, (int)radius / 3),
                     COLOR_ARGB(200, 240, 245, 255));
    }
};


// ============================================================
// Particle — 溅射粒子
//
// 撞击瞬间发射，受重力影响，生命期衰减
// ============================================================
struct Particle {
    float x, y;
    float vx, vy;
    float life;       // 当前剩余生命（秒）
    float maxLife;    // 总生命（秒）
    bool  alive;

    Particle() : x(0), y(0), vx(0), vy(0), life(0), maxLife(1), alive(false) {}

    void Spawn(float sx, float sy, float vx0, float vy0, float lifetime) {
        x = sx; y = sy; vx = vx0; vy = vy0;
        life = maxLife = lifetime; alive = true;
    }

    void Update(float dt) {
        if (!alive) return;
        vy += PARTICLE_G * dt;
        x  += vx * dt;
        y  += vy * dt;
        life -= dt;
        if (life <= 0.0f) alive = false;
    }

    void Draw(GameLib& g) const {
        if (!alive) return;
        float t = life / maxLife;  // 1→0

        // 颜色：白色→浅灰→深灰（模拟透明度衰减）
        int brightness;
        if (t > 0.5f) {
            brightness = 255;
        } else if (t > 0.15f) {
            brightness = 160 + (int)((t - 0.15f) / 0.35f * 95.0f);
        } else {
            brightness = (int)(t / 0.15f * 160.0f);
        }
        brightness = clampi(brightness, 0, 255);
        uint32_t c = COLOR_ARGB(255, brightness, brightness, brightness);

        // 粒子尺寸随生命衰减
        float size = 1.0f + t * 1.5f;
        int r = (int)size;
        if (r > 0) g.FillCircle((int)x, (int)y, r, c);
    }
};


// ============================================================
// ParticlePool — 固定容量粒子池
// ============================================================
class ParticlePool {
    static const int MAX_PARTICLES = 1200;
public:
    ParticlePool() {
        m_particles = new Particle[MAX_PARTICLES];
        m_count = 0;
    }
    ~ParticlePool() { delete[] m_particles; }

    // 发射一组粒子
    void Emit(float x, float y, int count) {
        for (int i = 0; i < count; i++) {
            if (m_count >= MAX_PARTICLES) break;

            // 随机初始速度：向上为主 + 水平散射
            float angle  = randf(-1.2f, 1.2f);            // 水平散射角
            float speed  = randf(60.0f, 200.0f);          // 初始速率
            float vx0    = sinf(angle) * speed;
            float vy0    = -cosf(angle) * speed * 0.8f;   // 偏向上
            float life   = randf(0.35f, 0.9f);            // 生命期

            m_particles[m_count].Spawn(x, y, vx0, vy0, life);
            m_count++;
        }
    }

    void Update(float dt) {
        // 压缩式清理：存活粒子前移
        int writeIdx = 0;
        for (int i = 0; i < m_count; i++) {
            m_particles[i].Update(dt);
            if (m_particles[i].alive) {
                if (writeIdx != i)
                    m_particles[writeIdx] = m_particles[i];
                writeIdx++;
            }
        }
        m_count = writeIdx;
    }

    void Draw(GameLib& g) const {
        for (int i = 0; i < m_count; i++) {
            m_particles[i].Draw(g);
        }
    }

    int Count() const { return m_count; }
    int Capacity() const { return MAX_PARTICLES; }

private:
    Particle *m_particles;
    int m_count;
};


// ============================================================
// DropManager — 水滴生命周期管理
// ============================================================
class DropManager {
    static const int MAX_DROPS = 20;
public:
    DropManager() : m_count(0), m_spawnTimer(0) {
        m_drops = new Drop[MAX_DROPS];
    }
    ~DropManager() { delete[] m_drops; }

    void Update(float dt, WaterSurface& water, ParticlePool& particles) {
        m_spawnTimer -= dt;

        // 自动生成水滴
        if (m_spawnTimer <= 0.0f && m_count < MAX_DROPS) {
            float sx = randf(40.0f, (float)(WIN_W - 40));
            float sy = randf(-35.0f, -5.0f);
            float r  = (float)(DROP_RADIUS_MIN + rand() % (DROP_RADIUS_MAX - DROP_RADIUS_MIN + 1));
            m_drops[m_count].Spawn(sx, sy, r);
            m_count++;
            m_spawnTimer = randf(DROP_SPAWN_MIN, DROP_SPAWN_MAX);
        }

        // 更新所有水滴
        for (int i = 0; i < m_count; i++) {
            Drop& d = m_drops[i];
            if (!d.alive) continue;

            int cx = clampi((int)d.x, 0, WAVE_COLS - 1);
            float wh = water.GetHeight(cx);  // 当前列的水面偏移
            bool hit = d.Update(dt, WATER_Y, wh);

            if (hit) {
                // 撞击水面：产生涟漪
                water.Impact(cx, -48.0f, 5);

                // 产生溅射粒子
                int count = SPLASH_COUNT_MIN + rand() % (SPLASH_COUNT_MAX - SPLASH_COUNT_MIN + 1);
                particles.Emit(d.x, (float)WATER_Y + wh, count);

                d.alive = false;
            }
        }

        // 压缩清理
        int w = 0;
        for (int i = 0; i < m_count; i++) {
            if (m_drops[i].alive) {
                if (w != i) m_drops[w] = m_drops[i];
                w++;
            }
        }
        m_count = w;
    }

    void Draw(GameLib& g) const {
        for (int i = 0; i < m_count; i++) {
            m_drops[i].Draw(g);
        }
    }

    int Count() const { return m_count; }

private:
    Drop  *m_drops;
    int    m_count;
    float  m_spawnTimer;
};


// ============================================================
// 简易 UI
// ============================================================
void DrawUI(GameLib& g, double fps, int drops, int particles) {
    g.DrawPrintfFont(8, 6, COLOR_ARGB(180, 200, 200, 200),
                     "Consolas", 14,
                     "FPS: %.0f  |  Drops: %d  |  Particles: %d",
                     fps, drops, particles);
    const char* tips[] = {
        "[CLICK] Splash ripple",
        "[SPACE] Toggle auto-spawn",
        "[R]     Reset water",
        "[ESC]   Exit",
    };
    for (int i = 0; i < 4; i++) {
        g.DrawPrintfFont(8, 26 + i * 18, COLOR_ARGB(110, 180, 180, 180),
                         "Consolas", 12, "%s", tips[i]);
    }
}


// ============================================================
// 主入口
// ============================================================
int main() {
    srand((unsigned int)time(NULL));

    GameLib game;
    if (game.Open(WIN_W, WIN_H, "Water Ripple - Drop Splash Demo", true) < 0) {
        return -1;
    }

    // ---- 子系统初始化 ----
    WaterSurface water;
    ParticlePool particles;
    DropManager  drops;
    bool autoSpawn = true;

    // ---- 主循环 ----
    while (!game.IsClosed()) {
        float dt = (float)game.GetDeltaTime();
        // 防止首帧 dt 过大
        if (dt > 0.1f) dt = 0.016f;

        // ---- 输入 ----
        if (game.IsKeyPressed(VK_ESCAPE)) break;
        if (game.IsKeyPressed(KEY_SPACE)) autoSpawn = !autoSpawn;
        if (game.IsKeyPressed('R')) water = WaterSurface();

        // 鼠标点击：在水面生成涟漪 + 溅射粒子
        if (game.IsMousePressed(0)) {
            int mx = game.GetMouseX();
            float sx = clampf((float)mx, 10.0f, (float)(WIN_W - 10));
            int cx = clampi((int)sx, 0, WAVE_COLS - 1);

            water.Impact(cx, -48.0f, 5);
            int cnt = SPLASH_COUNT_MIN + rand() % (SPLASH_COUNT_MAX - SPLASH_COUNT_MIN + 1);
            particles.Emit(sx, (float)WATER_Y + water.GetHeight(cx), cnt);
        }

        // ---- 物理更新 ----
        water.Update();

        if (autoSpawn) {
            drops.Update(dt, water, particles);
        }

        // 粒子总是更新（手动点击产生的粒子也需要更新）
        particles.Update(dt);

        // ---- 渲染 ----
        game.Clear(COLOR_BLACK);

        // 1. 参考基线（淡灰虚线，标注原始水面位置）
        for (int x = 0; x < WIN_W; x += 8) {
            game.SetPixel(x, WATER_Y, COLOR_ARGB(35, 100, 100, 100));
        }

        // 2. 粒子（底层）
        particles.Draw(game);

        // 3. 水滴（中层）
        drops.Draw(game);

        // 4. 水面线（顶层白色折线）
        water.Draw(game, WATER_Y);

        // 5. UI
        DrawUI(game, game.GetFPS(), drops.Count(), particles.Count());

        game.Update();
        game.WaitFrame(60);
    }

    return 0;
}
