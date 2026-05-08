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
const int WIN_W = 480;
const int WIN_H = 640;
const float PLAYER_SPEED = 320.f;

// ============================================================
// Power-up types
// ============================================================
enum PowerType { PUP_RED=0, PUP_BLUE, PUP_BOMB, PUP_LIFE, PUP_SCORE, PUP_COUNT };

// ============================================================
// Enemy types
// ============================================================
enum EnemyKind {
    E_SMALL=0,    // small fighter, straight down
    E_MEDIUM,     // sine-wave, medium HP
    E_LARGE,      // slow, tanky, fires spreads
    E_KAMIKAZE,   // fast, homes toward player
    E_CARRIER,    // slow, spawns small enemies
    E_BOSS,       // boss
};

// ============================================================
// Bullet (player)
// ============================================================
struct Bullet {
    float x, y, vx, vy;
    int   damage = 1;
    bool  alive  = false;
};

// ============================================================
// Enemy bullet
// ============================================================
struct EBullet {
    float x, y, vx, vy;
    bool  alive = false;
    int   color = 0;
};

// ============================================================
// Particle (explosion FX)
// ============================================================
struct Particle {
    float x, y, vx, vy;
    float life = 0;
    int   color = 0;
};

// ============================================================
// Enemy
// ============================================================
struct Enemy {
    float x, y, vx, vy;
    int   hp     = 1;
    int   kind   = E_SMALL;
    float timer  = 0;
    float fireCD= 0;    // time between shots
    float fireT = 0;     // elapsed since last shot
    bool  alive  = false;
    int   score  = 100;
    bool  entered= false; // has entered screen
};

// ============================================================
// Boss state
// ============================================================
struct Boss {
    float x, y, targetX, targetY;
    int   hp = 0;
    int   maxHp = 0;
    int   phase = 0;      // 0=enter, 1=attack1, 2=attack2, 3=enrage
    float phaseTimer = 0;
    float fireTimer = 0;
    float moveTimer = 0;
    bool  alive = false;
};

// ============================================================
// Power-up item
// ============================================================
struct PowerUp {
    float x, y, vy;
    int   type = PUP_RED;
    bool  alive = false;
};

// ============================================================
// Star (background)
// ============================================================
struct Star {
    float x, y, speed;
    int   bright;
};

// ============================================================
// Player
// ============================================================
struct Player {
    float x = WIN_W / 2.f;
    float y = 550.f;
    int   weaponType  = 0;  // 0=spread(red), 1=laser(blue)
    int   weaponLevel = 1;  // 1-4
    int   lives  = 3;
    int   bombs  = 3;
    float invuln = 0;       // invulnerability timer after hit
    bool  alive  = true;
    float fireCD= 0.12f;    // fire cooldown
    float fireT = 0;
};

// ============================================================
// Game state
// ============================================================
struct Game {
    Player player;
    std::vector<Bullet> bullets;
    std::vector<EBullet> eBullets;
    std::vector<Enemy> enemies;
    std::vector<PowerUp> powerUps;
    std::vector<Particle> particles;
    std::vector<Star> stars;
    Boss boss;

    int   score    = 0;
    int   stage    = 1;
    float stageTimer = 0;
    float spawnTimer = 0;
    float spawnRate  = 1.2f;  // seconds between enemy spawns
    int   enemiesKilled = 0;
    bool  bossActive = false;
    bool  bossDefeated = false;
    bool  gameOver  = false;
    bool  paused    = false;
    float bombTimer = 0;      // bomb FX duration
    float shakeTimer= 0;      // screen shake
    int   screenX   = 0;      // shake offset

    // Stage transition
    float transTimer = 0;
    bool  transitioning = false;
};

// ============================================================
// Initialize background stars
// ============================================================
void InitStars(Game& g) {
    g.stars.clear();
    for (int i = 0; i < 80; i++) {
        Star s;
        s.x = (float)(rand() % WIN_W);
        s.y = (float)(rand() % WIN_H);
        s.bright = 80 + rand() % 176;
        s.speed = 40.f + (float)(rand() % 120); // parallax layers
        g.stars.push_back(s);
    }
}

// ============================================================
// Spawn enemy
// ============================================================
void SpawnEnemy(Game& g, int kind, float x, float y) {
    Enemy e;
    e.x = x; e.y = y; e.kind = kind; e.alive = true;

    switch (kind) {
    case E_SMALL:
        e.hp = 1; e.score = 100;
        e.vy = 150.f + (float)(rand() % 80);
        e.fireCD = 2.5f + (float)(rand() % 200) / 100.f;
        break;
    case E_MEDIUM:
        e.hp = 3; e.score = 250;
        e.vy = 90.f;
        e.fireCD = 1.5f;
        break;
    case E_LARGE:
        e.hp = 8; e.score = 500;
        e.vy = 50.f;
        e.fireCD = 0.8f;
        break;
    case E_KAMIKAZE:
        e.hp = 2; e.score = 150;
        e.fireCD = 99.f; // never fires
        break;
    case E_CARRIER:
        e.hp = 12; e.score = 800;
        e.vy = 40.f;
        e.fireCD = 1.8f;
        break;
    }
    g.enemies.push_back(e);
}

// ============================================================
// Spawn enemy bullet
// ============================================================
void SpawnEBullet(Game& g, float x, float y, float vx, float vy, int color) {
    EBullet b;
    b.x = x; b.y = y; b.vx = vx; b.vy = vy; b.alive = true; b.color = color;
    g.eBullets.push_back(b);
}

// ============================================================
// Fire enemy patterns
// ============================================================
void EnemyFire(Game& g, Enemy& e) {
    switch (e.kind) {
    case E_SMALL: {
        // Single aimed shot
        float dx = g.player.x - e.x;
        float dy = g.player.y - e.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist > 1) {
            float sp = 200.f;
            SpawnEBullet(g, e.x, e.y, dx/dist * sp, dy/dist * sp, COLOR_RGB(255, 200, 100));
        }
        break;
    }
    case E_MEDIUM: {
        // 3-way spread
        float dx = g.player.x - e.x;
        float dy = g.player.y - e.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist > 1) {
            float sp = 180.f;
            float ang = atan2f(dy, dx);
            for (int i = -1; i <= 1; i++) {
                float a = ang + i * 0.3f;
                SpawnEBullet(g, e.x, e.y, sp * cosf(a), sp * sinf(a), COLOR_RGB(255, 160, 80));
            }
        }
        break;
    }
    case E_LARGE: {
        // 5-way spread
        float sp = 160.f;
        for (int i = -2; i <= 2; i++) {
            float a = 1.57f + i * 0.35f; // downward fan
            SpawnEBullet(g, e.x, e.y, sp * cosf(a), sp * sinf(a), COLOR_RGB(255, 100, 100));
        }
        break;
    }
    case E_CARRIER: {
        // Random spray
        for (int i = 0; i < 3; i++) {
            float a = 1.2f + (float)(rand() % 100) * 0.75f / 100.f;
            float sp = 140.f + (float)(rand() % 100);
            SpawnEBullet(g, e.x, e.y, sp * cosf(a), sp * sinf(a), COLOR_RGB(255, 200, 200));
        }
        break;
    }
    }
}

// ============================================================
// Spawn particles
// ============================================================
void SpawnParticles(Game& g, float x, float y, int count, int color) {
    for (int i = 0; i < count; i++) {
        Particle p;
        p.x = x; p.y = y;
        float a = (float)(rand() % 628) / 100.f;
        float sp = 40.f + (float)(rand() % 200);
        p.vx = sp * cosf(a);
        p.vy = sp * sinf(a);
        p.life = 0.3f + (float)(rand() % 40) / 100.f;
        p.color = color;
        g.particles.push_back(p);
    }
}

// ============================================================
// Drop power-up
// ============================================================
void DropPowerUp(Game& g, float x, float y) {
    int r = rand() % 100;
    PowerUp p;
    p.x = x; p.y = y; p.vy = 80.f; p.alive = true;

    if      (r < 25) p.type = PUP_RED;
    else if (r < 45) p.type = PUP_BLUE;
    else if (r < 55) p.type = PUP_BOMB;
    else if (r < 60) p.type = PUP_LIFE;
    else if (r < 80) p.type = PUP_SCORE;
    else             return; // no drop

    g.powerUps.push_back(p);
}

// ============================================================
// Player fire
// ============================================================
void PlayerFire(Game& g) {
    Player& p = g.player;
    if (!p.alive || p.invuln > 0) return;

    if (p.weaponType == 0) {
        // Spread (red) — fan pattern
        int count = 1 + p.weaponLevel * 2; // 3, 5, 7, 9
        float spread = (count - 1) * 0.06f; // total spread angle increases
        float baseA = -1.57f; // straight up
        float sp = 500.f;
        for (int i = 0; i < count; i++) {
            float a = baseA + (i - (count-1)/2.f) * spread;
            Bullet b;
            b.x = p.x; b.y = p.y - 12;
            b.vx = sp * cosf(a);
            b.vy = sp * sinf(a);
            b.alive = true; b.damage = 1 + p.weaponLevel / 2;
            g.bullets.push_back(b);
        }
    } else {
        // Laser (blue) — concentrated beam
        Bullet b;
        b.x = p.x; b.y = p.y - 20;
        b.vx = 0; b.vy = -600.f;
        b.alive = true;
        b.damage = 2 + p.weaponLevel * 2; // high damage
        g.bullets.push_back(b);

        // Side lasers at higher levels
        if (p.weaponLevel >= 2) {
            Bullet b2;
            b2.x = p.x - 8; b2.y = p.y - 16;
            b2.vx = 0; b2.vy = -580.f;
            b2.alive = true; b2.damage = 1 + p.weaponLevel;
            g.bullets.push_back(b2);

            Bullet b3;
            b3.x = p.x + 8; b3.y = p.y - 16;
            b3.vx = 0; b3.vy = -580.f;
            b3.alive = true; b3.damage = 1 + p.weaponLevel;
            g.bullets.push_back(b3);
        }
    }
}

// ============================================================
// Use bomb (screen clear)
// ============================================================
void UseBomb(Game& g) {
    if (g.player.bombs <= 0) return;
    g.player.bombs--;
    g.bombTimer = 1.0f;
    g.shakeTimer = 0.3f;

    // Destroy all enemy bullets
    g.eBullets.clear();
    // Damage all enemies on screen
    for (auto& e : g.enemies) {
        if (!e.alive) continue;
        e.hp -= 5;
        if (e.hp <= 0) {
            e.alive = false;
            g.score += e.score;
            SpawnParticles(g, e.x, e.y, 8, COLOR_RGB(255, 255, 100));
            DropPowerUp(g, e.x, e.y);
            g.enemiesKilled++;
        }
    }
    // Damage boss
    if (g.boss.alive) g.boss.hp -= 15;
    // Particles
    SpawnParticles(g, g.player.x, g.player.y, 40, COLOR_RGB(255, 255, 200));
}

// ============================================================
// Spawn boss
// ============================================================
void SpawnBoss(Game& g) {
    g.boss.alive = true;
    g.boss.x = WIN_W / 2.f;
    g.boss.y = -80;
    g.boss.targetX = WIN_W / 2.f;
    g.boss.targetY = 100;
    g.boss.hp = 80 + g.stage * 30;
    g.boss.maxHp = g.boss.hp;
    g.boss.phase = 0;
    g.boss.phaseTimer = 0;
    g.boss.fireTimer = 0;
    g.boss.moveTimer = 0;
    g.bossActive = true;
    g.enemies.clear();
    g.eBullets.clear();
}

// ============================================================
// Update boss
// ============================================================
void UpdateBoss(Game& g, float dt) {
    Boss& b = g.boss;
    if (!b.alive) return;

    b.phaseTimer += dt;
    b.fireTimer += dt;
    b.moveTimer += dt;

    // Phase transitions
    if (b.phase == 0 && b.phaseTimer > 2.f) {
        b.phase = 1; b.phaseTimer = 0;
    } else if (b.phase == 1 && b.phaseTimer > 8.f) {
        b.phase = 2; b.phaseTimer = 0;
    } else if (b.phase == 2 && b.phaseTimer > 10.f) {
        b.phase = 3; b.phaseTimer = 0;
    }

    // Movement
    if (b.phase == 0) {
        b.y += (b.targetY - b.y) * 2.f * dt;
    } else {
        b.moveTimer += dt;
        b.x = WIN_W/2 + sinf(b.moveTimer * 0.8f) * 140.f;
        b.y = 100 + sinf(b.moveTimer * 0.5f) * 40.f;
    }

    // Fire patterns
    float cd = (b.phase == 3) ? 0.15f : (b.phase == 2) ? 0.25f : 0.4f;
    if (b.fireTimer >= cd) {
        b.fireTimer = 0;
        switch (b.phase) {
        case 0: break; // entering, no fire
        case 1: // Aimed bursts
            for (int i = 0; i < 3; i++) {
                float dx = g.player.x - b.x;
                float dy = g.player.y - b.y;
                float dist = sqrtf(dx*dx + dy*dy);
                if (dist > 1) {
                    float sp = 220.f;
                    float a = atan2f(dy, dx) + (i-1) * 0.2f;
                    SpawnEBullet(g, b.x, b.y, sp*cosf(a), sp*sinf(a), COLOR_RGB(255, 120, 80));
                }
            }
            break;
        case 2: // Circular barrage
            for (int i = 0; i < 12; i++) {
                float a = b.fireTimer * 3.f + i * 0.523f;
                float sp = 150.f;
                SpawnEBullet(g, b.x, b.y, sp*cosf(a), sp*sinf(a), COLOR_RGB(255, 180, 100));
            }
            break;
        case 3: // Dense random spray (enrage)
            for (int i = 0; i < 5; i++) {
                float a = 1.0f + (float)(rand() % 150) * 0.75f / 100.f;
                float sp = 180.f + (float)(rand() % 120);
                SpawnEBullet(g, b.x, b.y, sp*cosf(a), sp*sinf(a), COLOR_RGB(255, 60, 60));
            }
            break;
        }
    }

    // Check boss death
    if (b.hp <= 0) {
        b.alive = false;
        g.bossActive = false;
        g.bossDefeated = true;
        g.score += 5000;
        SpawnParticles(g, b.x, b.y, 60, COLOR_RGB(255, 200, 50));
        SpawnParticles(g, b.x, b.y, 40, COLOR_RGB(255, 100, 30));
        g.shakeTimer = 1.0f;
    }
}

// ============================================================
// Next stage
// ============================================================
void NextStage(Game& g) {
    g.stage++;
    g.enemies.clear();
    g.eBullets.clear();
    g.bullets.clear();
    g.powerUps.clear();
    g.boss.alive = false;
    g.bossActive = false;
    g.bossDefeated = false;
    g.stageTimer = 0;
    g.spawnTimer = 0;
    g.enemiesKilled = 0;
    g.spawnRate = (1.2f - g.stage * 0.08f);
    if (g.spawnRate < 0.25f) g.spawnRate = 0.25f;
    g.transitioning = false;
    g.transTimer = 0;
    g.player.invuln = 2.f; // invulnerability at start of stage
}

// ============================================================
// Rendering
// ============================================================
void DrawPlayer(GameLib& gl, const Player& p) {
    if (!p.alive) return;
    if (p.invuln > 0 && ((int)(p.invuln * 10) % 2)) return; // blink

    int cx = (int)p.x, cy = (int)p.y;

    // Engine glow
    gl.FillTriangle(cx - 8, cy + 10, cx + 8, cy + 10, cx, cy + 22, COLOR_RGB(255, 180, 40));
    gl.FillTriangle(cx - 5, cy + 10, cx + 5, cy + 10, cx, cy + 18, COLOR_RGB(255, 255, 200));

    // Body
    uint32_t bodyColor = (p.weaponType == 0) ? COLOR_RGB(220, 50, 50) : COLOR_RGB(50, 120, 240);
    gl.FillTriangle(cx - 14, cy + 8, cx + 14, cy + 8, cx, cy - 16, bodyColor);

    // Cockpit
    gl.FillTriangle(cx - 5, cy + 2, cx + 5, cy + 2, cx, cy - 8, COLOR_RGB(180, 220, 255));

    // Wing accents
    uint32_t accent = (p.weaponType == 0) ? COLOR_RGB(255, 140, 80) : COLOR_RGB(120, 180, 255);
    gl.FillRect(cx - 14, cy + 3, 6, 6, accent);
    gl.FillRect(cx + 8, cy + 3, 6, 6, accent);
}

void DrawBullet(GameLib& gl, const Bullet& b) {
    if (!b.alive) return;
    int cx = (int)b.x, cy = (int)b.y;
    // Red bullets (spread) are circles, blue bullets (laser) are lines
    uint32_t color = (b.vx == 0) ? COLOR_RGB(100, 180, 255) : COLOR_RGB(255, 200, 50);
    gl.FillCircle(cx, cy, 3, color);
    gl.FillCircle(cx, cy, 2, COLOR_WHITE);
}

void DrawEBullet(GameLib& gl, const EBullet& b) {
    if (!b.alive) return;
    int cx = (int)b.x, cy = (int)b.y;
    gl.FillCircle(cx, cy, 4, (uint32_t)b.color);
    gl.FillCircle(cx, cy, 2, COLOR_RGB(255, 255, 200));
}

void DrawEnemy(GameLib& gl, const Enemy& e) {
    if (!e.alive) return;
    int cx = (int)e.x, cy = (int)e.y;

    switch (e.kind) {
    case E_SMALL: {
        // Small diamond
        gl.FillTriangle(cx, cy-10, cx-9, cy+5, cx+9, cy+5, COLOR_RGB(180, 180, 200));
        gl.FillTriangle(cx, cy-10, cx-4, cy+5, cx+4, cy+5, COLOR_RGB(220, 220, 240));
        break;
    }
    case E_MEDIUM: {
        // Medium hexagon
        gl.FillCircle(cx, cy, 12, COLOR_RGB(200, 120, 60));
        gl.FillCircle(cx, cy, 9, COLOR_RGB(240, 160, 80));
        gl.FillCircle(cx, cy, 4, COLOR_RGB(255, 200, 120));
        break;
    }
    case E_LARGE: {
        // Large heavy ship
        gl.FillRect(cx-20, cy-14, 40, 28, COLOR_RGB(140, 100, 60));
        gl.FillRect(cx-18, cy-12, 36, 10, COLOR_RGB(180, 140, 80));
        gl.FillRect(cx-6, cy+8, 12, 10, COLOR_RGB(255, 200, 100)); // engine
        break;
    }
    case E_KAMIKAZE: {
        // Fast sharp triangle
        gl.FillTriangle(cx, cy-14, cx-8, cy+8, cx+8, cy+8, COLOR_RGB(255, 80, 60));
        gl.FillTriangle(cx, cy-14, cx-3, cy+8, cx+3, cy+8, COLOR_RGB(255, 160, 100));
        break;
    }
    case E_CARRIER: {
        // Large carrier
        gl.FillRect(cx-26, cy-20, 52, 40, COLOR_RGB(100, 120, 160));
        gl.FillRect(cx-22, cy-16, 44, 18, COLOR_RGB(140, 160, 200));
        gl.FillRect(cx-10, cy+14, 20, 10, COLOR_RGB(200, 220, 255));
        // Turrets
        gl.FillCircle(cx-18, cy-6, 5, COLOR_RGB(80, 90, 140));
        gl.FillCircle(cx+18, cy-6, 5, COLOR_RGB(80, 90, 140));
        break;
    }
    }
}

void DrawBoss(GameLib& gl, const Boss& b) {
    if (!b.alive) return;
    int cx = (int)b.x, cy = (int)b.y;

    // Large boss body
    gl.FillRect(cx-35, cy-30, 70, 60, COLOR_RGB(120, 40, 40));
    gl.FillRect(cx-30, cy-25, 60, 36, COLOR_RGB(180, 60, 60));
    gl.FillRect(cx-25, cy-20, 50, 16, COLOR_RGB(220, 80, 80));

    // Cockpit
    gl.FillRect(cx-8, cy-10, 16, 10, COLOR_RGB(255, 200, 100));

    // Wings
    gl.FillRect(cx-45, cy-10, 20, 14, COLOR_RGB(100, 30, 30));
    gl.FillRect(cx+25, cy-10, 20, 14, COLOR_RGB(100, 30, 30));

    // Turrets
    int tColor = (b.phase >= 3) ? COLOR_RGB(255, 100, 100) : COLOR_RGB(200, 150, 80);
    gl.FillCircle(cx-30, cy-20, 7, tColor);
    gl.FillCircle(cx+30, cy-20, 7, tColor);
    gl.FillCircle(cx, cy-26, 5, tColor);

    // HP bar
    int barW = 120, barH = 8;
    int barX = cx - barW/2, barY = cy - 48;
    gl.FillRect(barX, barY, barW, barH, COLOR_RGB(40, 20, 20));
    float hpRatio = (float)b.hp / b.maxHp;
    if (hpRatio > 0) {
        uint32_t hpColor = (hpRatio > 0.4f) ? COLOR_RGB(255, 60, 60)
                         : (hpRatio > 0.15f) ? COLOR_RGB(255, 180, 40)
                         : COLOR_RGB(255, 60, 60);
        gl.FillRect(barX, barY, (int)(barW * hpRatio), barH, hpColor);
    }
}

void DrawPowerUp(GameLib& gl, const PowerUp& p) {
    if (!p.alive) return;
    int cx = (int)p.x, cy = (int)p.y;

    uint32_t colors[] = {
        COLOR_RGB(255, 60, 60),   // RED - spread weapon
        COLOR_RGB(60, 120, 255),  // BLUE - laser weapon
        COLOR_RGB(255, 200, 50),  // BOMB
        COLOR_RGB(80, 255, 80),   // LIFE
        COLOR_RGB(255, 220, 100), // SCORE
    };
    const char* labels[] = { "W", "W", "B", "1UP", "S" };
    const char* extra[]  = { "红", "蓝", "", "", "" };

    gl.FillCircle(cx, cy, 10, colors[p.type]);
    gl.DrawCircle(cx, cy, 10, COLOR_WHITE);
    int tw = gl.GetTextWidthFont(labels[p.type], 16) / 2;
    gl.DrawTextFont(cx - tw, cy - 7, labels[p.type], COLOR_WHITE, 16);

    // Sub label for weapons
    if (p.type <= PUP_BLUE) {
        int sw = gl.GetTextWidthFont(extra[p.type], 12) / 2;
        gl.DrawTextFont(cx - sw, cy - 18, extra[p.type], COLOR_WHITE, 12);
    }
}

void DrawHUD(GameLib& gl, const Game& g) {
    // Top bar
    gl.FillRect(0, 0, WIN_W, 36, COLOR_RGB(0, 0, 0));

    char buf[64];
    int fs = 16;

    // Score
    snprintf(buf, sizeof(buf), "SCORE:%d", g.score);
    gl.DrawTextFont(8, 4, buf, COLOR_RGB(255, 220, 80), fs);

    // Lives
    snprintf(buf, sizeof(buf), "LIFE:%d", g.player.lives);
    gl.DrawTextFont(180, 4, buf, COLOR_RGB(255, 100, 100), fs);

    // Bombs
    snprintf(buf, sizeof(buf), "BOMB:%d", g.player.bombs);
    gl.DrawTextFont(300, 4, buf, COLOR_RGB(255, 200, 60), fs);

    // Weapon indicator
    const char* wpn = (g.player.weaponType == 0) ? "RED" : "LASER";
    uint32_t wc = (g.player.weaponType == 0) ? COLOR_RGB(255, 100, 80) : COLOR_RGB(80, 160, 255);
    snprintf(buf, sizeof(buf), "%s Lv.%d", wpn, g.player.weaponLevel);
    gl.DrawTextFont(400, 4, buf, wc, fs);

    // Stage
    snprintf(buf, sizeof(buf), "STAGE %d", g.stage);
    gl.DrawTextFont(195, 22, buf, COLOR_RGB(180, 200, 220), 14);

    // Bomb FX
    if (g.bombTimer > 0) {
        gl.FillRect(0, 0, WIN_W, WIN_H, COLOR_RGB(255, 255, 200));
    }
}

void DrawStars(GameLib& gl, const std::vector<Star>& stars) {
    for (const auto& s : stars) {
        int c = s.bright;
        gl.SetPixel((int)s.x, (int)s.y, COLOR_RGB(c, c, c + 20));
    }
}

// ============================================================
// Main
// ============================================================
int main() {
    GameLib gl;
    gl.Open(WIN_W, WIN_H, "Raiden", true);

    Game g;
    srand((unsigned)time(nullptr));
    InitStars(g);
    g.stageTimer = 3.f; // brief delay before enemies start

    float lastTime = gl.GetTime();

    while (!gl.IsClosed()) {
        float now = gl.GetTime();
        float dt = now - lastTime;
        lastTime = now;
        if (dt > 0.05f) dt = 0.016f;

        // --- Input ---
        if (!g.gameOver) {
            Player& p = g.player;
            if (gl.IsKeyDown(KEY_LEFT)  || gl.IsKeyDown(KEY_A)) p.x -= PLAYER_SPEED * dt;
            if (gl.IsKeyDown(KEY_RIGHT) || gl.IsKeyDown(KEY_D)) p.x += PLAYER_SPEED * dt;
            if (gl.IsKeyDown(KEY_UP)    || gl.IsKeyDown(KEY_W)) p.y -= PLAYER_SPEED * dt;
            if (gl.IsKeyDown(KEY_DOWN)  || gl.IsKeyDown(KEY_S)) p.y += PLAYER_SPEED * dt;

            // Clamp
            if (p.x < 20) p.x = 20;
            if (p.x > WIN_W - 20) p.x = WIN_W - 20;
            if (p.y < 60) p.y = 60;
            if (p.y > WIN_H - 30) p.y = WIN_H - 30;

            // Fire
            p.fireT += dt;
            if (gl.IsKeyDown(KEY_SPACE) && p.fireT >= p.fireCD) {
                p.fireT = 0;
                PlayerFire(g);
            }

            // Bomb
            if (gl.IsKeyPressed(KEY_B) || gl.IsKeyPressed(KEY_Z)) {
                UseBomb(g);
            }

            // Pause
            if (gl.IsKeyPressed(KEY_P))
                g.paused = !g.paused;
        }

        // Restart
        if (g.gameOver && gl.IsKeyPressed(KEY_SPACE)) {
            g = Game();
            srand((unsigned)time(nullptr));
            InitStars(g);
            g.stageTimer = 2.f;
            lastTime = gl.GetTime();
        }

        // --- Update ---
        if (!g.paused && !g.gameOver) {
            // Stage transition
            if (g.transitioning) {
                g.transTimer -= dt;
                if (g.transTimer <= 0) NextStage(g);
            }

            // Stage timer
            g.stageTimer += dt;

            // Screen shake
            if (g.shakeTimer > 0) {
                g.shakeTimer -= dt;
                g.screenX = (rand() % 8) - 4;
            } else {
                g.screenX = 0;
            }

            // Bomb timer
            if (g.bombTimer > 0) g.bombTimer -= dt;

            // Player invulnerability
            if (g.player.invuln > 0) g.player.invuln -= dt;

            // Scroll stars
            for (auto& s : g.stars) {
                s.y += s.speed * dt;
                if (s.y > WIN_H) { s.y -= WIN_H; s.x = (float)(rand() % WIN_W); }
            }

            // Update bullets (player)
            for (auto& b : g.bullets) {
                if (!b.alive) continue;
                b.x += b.vx * dt;
                b.y += b.vy * dt;
                if (b.x < 0 || b.x > WIN_W || b.y < -20 || b.y > WIN_H) b.alive = false;
            }
            g.bullets.erase(std::remove_if(g.bullets.begin(), g.bullets.end(),
                [](const Bullet& b) { return !b.alive; }), g.bullets.end());

            // Update enemy bullets
            for (auto& b : g.eBullets) {
                if (!b.alive) continue;
                b.x += b.vx * dt;
                b.y += b.vy * dt;
                if (b.x < -20 || b.x > WIN_W + 20 || b.y > WIN_H + 20 || b.y < -40) b.alive = false;
            }
            g.eBullets.erase(std::remove_if(g.eBullets.begin(), g.eBullets.end(),
                [](const EBullet& b) { return !b.alive; }), g.eBullets.end());

            // Update particles
            for (auto& p : g.particles) {
                p.x += p.vx * dt; p.y += p.vy * dt;
                p.life -= dt;
            }
            g.particles.erase(std::remove_if(g.particles.begin(), g.particles.end(),
                [](const Particle& p) { return p.life <= 0; }), g.particles.end());

            // Update power-ups
            for (auto& p : g.powerUps) {
                if (!p.alive) continue;
                p.y += p.vy * dt;
                if (p.y > WIN_H + 40) p.alive = false;
            }
            g.powerUps.erase(std::remove_if(g.powerUps.begin(), g.powerUps.end(),
                [](const PowerUp& p) { return !p.alive; }), g.powerUps.end());

            // Update enemies
            for (auto& e : g.enemies) {
                if (!e.alive) continue;

                // Mark as entered screen
                if (e.y > 0) e.entered = true;

                // Movement
                switch (e.kind) {
                case E_SMALL:
                    e.y += e.vy * dt;
                    break;
                case E_MEDIUM:
                    e.y += e.vy * dt;
                    e.x += sinf(e.timer * 3.f) * 100.f * dt;
                    break;
                case E_LARGE:
                    e.y += e.vy * dt;
                    break;
                case E_KAMIKAZE: {
                    // Home toward player
                    float dx = g.player.x - e.x;
                    float dy = g.player.y - e.y;
                    float dist = sqrtf(dx*dx + dy*dy);
                    if (dist > 1) {
                        float sp = 250.f;
                        e.x += dx / dist * sp * dt;
                        e.y += dy / dist * sp * dt;
                    }
                    break;
                }
                case E_CARRIER:
                    e.y += e.vy * dt;
                    e.x += sinf(e.timer * 1.5f) * 60.f * dt;
                    break;
                }

                e.timer += dt;
                e.fireT += dt;

                // Fire
                if (e.fireT >= e.fireCD && e.entered) {
                    e.fireT = 0;
                    EnemyFire(g, e);
                }

                // Remove if off screen
                if (e.y > WIN_H + 60) e.alive = false;
            }
            g.enemies.erase(std::remove_if(g.enemies.begin(), g.enemies.end(),
                [](const Enemy& e) { return !e.alive; }), g.enemies.end());

            // Spawn enemies
            if (!g.bossActive && !g.transitioning && g.stageTimer > 2.f) {
                g.spawnTimer += dt;
                if (g.spawnTimer >= g.spawnRate) {
                    g.spawnTimer = 0;
                    int r = rand() % 100;
                    int kind;
                    float x = 40.f + (float)(rand() % (WIN_W - 80));
                    if      (r < 40) kind = E_SMALL;
                    else if (r < 65) kind = E_MEDIUM;
                    else if (r < 82) kind = E_LARGE;
                    else if (r < 90) kind = E_KAMIKAZE;
                    else             kind = E_CARRIER;
                    SpawnEnemy(g, kind, x, -30.f);
                }

                // Trigger boss after enough kills
                if (g.enemiesKilled >= 20 + g.stage * 5 && !g.bossActive) {
                    SpawnBoss(g);
                }
            }

            // Update boss
            if (g.bossActive) {
                UpdateBoss(g, dt);
                if (g.bossDefeated) {
                    g.transitioning = true;
                    g.transTimer = 3.f;
                }
            }

            // Collision: player bullets vs enemies
            for (auto& b : g.bullets) {
                if (!b.alive) continue;
                for (auto& e : g.enemies) {
                    if (!e.alive) continue;
                    float ER = (e.kind == E_CARRIER) ? 30.f : (e.kind == E_LARGE) ? 20.f : 12.f;
                    float dx = b.x - e.x, dy = b.y - e.y;
                    if (dx*dx + dy*dy < ER*ER) {
                        b.alive = false;
                        e.hp -= b.damage;
                        if (e.hp <= 0) {
                            e.alive = false;
                            g.score += e.score;
                            SpawnParticles(g, e.x, e.y, 6, COLOR_RGB(255, 160, 60));
                            DropPowerUp(g, e.x, e.y);
                            g.enemiesKilled++;
                        }
                        break;
                    }
                }
                // vs boss
                if (b.alive && g.boss.alive) {
                    float dx = b.x - g.boss.x, dy = b.y - g.boss.y;
                    if (dx*dx + dy*dy < 2500.f) { // 50^2
                        b.alive = false;
                        g.boss.hp -= b.damage;
                        SpawnParticles(g, b.x, b.y, 2, COLOR_RGB(255, 200, 100));
                    }
                }
            }

            // Collision: enemy bullets vs player
            if (g.player.alive && g.player.invuln <= 0) {
                for (auto& b : g.eBullets) {
                    if (!b.alive) continue;
                    float dx = b.x - g.player.x, dy = b.y - g.player.y;
                    if (dx*dx + dy*dy < 144.f) { // 12^2
                        b.alive = false;
                        g.player.lives--;
                        g.player.invuln = 2.f;
                        g.player.weaponLevel--;
                        if (g.player.weaponLevel < 1) g.player.weaponLevel = 1;
                        SpawnParticles(g, g.player.x, g.player.y, 12, COLOR_RGB(255, 255, 255));
                        g.shakeTimer = 0.2f;
                        if (g.player.lives <= 0) {
                            g.player.alive = false;
                            g.gameOver = true;
                            SpawnParticles(g, g.player.x, g.player.y, 30, COLOR_RGB(255, 100, 50));
                        }
                        break;
                    }
                }
            }

            // Collision: player vs power-ups
            for (auto& p : g.powerUps) {
                if (!p.alive) continue;
                float dx = p.x - g.player.x, dy = p.y - g.player.y;
                if (dx*dx + dy*dy < 400.f) { // 20^2
                    p.alive = false;
                    switch (p.type) {
                    case PUP_RED:
                        if (g.player.weaponType == 0)
                            g.player.weaponLevel++;
                        else
                            g.player.weaponLevel = 1;
                        g.player.weaponType = 0;
                        if (g.player.weaponLevel > 5) g.player.weaponLevel = 5;
                        break;
                    case PUP_BLUE:
                        if (g.player.weaponType == 1)
                            g.player.weaponLevel++;
                        else
                            g.player.weaponLevel = 1;
                        g.player.weaponType = 1;
                        if (g.player.weaponLevel > 5) g.player.weaponLevel = 5;
                        break;
                    case PUP_BOMB:
                        g.player.bombs++;
                        if (g.player.bombs > 9) g.player.bombs = 9;
                        break;
                    case PUP_LIFE:
                        g.player.lives++;
                        break;
                    case PUP_SCORE:
                        g.score += 500;
                        break;
                    }
                    SpawnParticles(g, p.x, p.y, 4, COLOR_RGB(200, 255, 200));
                    gl.PlayBeep(880, 60, 1, 50);
                }
            }
        }

        // --- Render ---
        gl.Clear(COLOR_RGB(5, 5, 20));
        DrawStars(gl, g.stars);

        // Draw game objects
        for (const auto& b : g.bullets) DrawBullet(gl, b);
        for (const auto& e : g.enemies) DrawEnemy(gl, e);
        if (g.boss.alive) DrawBoss(gl, g.boss);
        for (const auto& p : g.powerUps) DrawPowerUp(gl, p);
        for (const auto& b : g.eBullets) DrawEBullet(gl, b);
        DrawPlayer(gl, g.player);

        // Particles
        for (const auto& p : g.particles) {
            int alpha = (int)(p.life * 255);
            if (alpha > 255) alpha = 255;
            int r = (p.color >> 16) & 0xFF;
            int gg = (p.color >> 8) & 0xFF;
            int bb = p.color & 0xFF;
            gl.FillCircle((int)p.x, (int)p.y, 2, COLOR_RGB(
                (r * alpha) / 255, (gg * alpha) / 255, (bb * alpha) / 255));
        }

        DrawHUD(gl, g);

        // Game over overlay
        if (g.gameOver) {
            int tw = gl.GetTextWidthFont("GAME OVER", 36);
            gl.DrawTextFont((WIN_W - tw)/2, WIN_H/2 - 30, "GAME OVER", COLOR_RGB(255, 60, 60), 36);
            int sw = gl.GetTextWidthFont("按空格键重新开始", 18);
            gl.DrawTextFont((WIN_W - sw)/2, WIN_H/2 + 20, "按空格键重新开始", COLOR_RGB(200, 200, 200), 18);
        } else if (g.paused) {
            int tw = gl.GetTextWidthFont("PAUSED", 32);
            gl.DrawTextFont((WIN_W - tw)/2, WIN_H/2 - 20, "PAUSED", COLOR_WHITE, 32);
        } else if (g.transitioning) {
            char buf[32];
            snprintf(buf, sizeof(buf), "STAGE %d CLEAR!", g.stage);
            int tw = gl.GetTextWidthFont(buf, 28);
            gl.DrawTextFont((WIN_W - tw)/2, WIN_H/2 - 20, buf, COLOR_RGB(255, 220, 80), 28);
        }

        gl.Update();
        gl.WaitFrame(60);
    }

    return 0;
}
