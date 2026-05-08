#define GAMELIB_IMPLEMENTATION
#include "../GameLib.h"
#include <vector>
#include <algorithm>
#include <math.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>

// ============================================================
// Math helpers
// ============================================================
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline float len2f(float x, float y)            { return x*x + y*y; }
inline float lenf(float x, float y)             { return sqrtf(x*x + y*y); }

// ============================================================
// Quadtree — stores (boidIdx, x, y) for split-redistribution
// ============================================================
class QuadTree {
public:
    static const int MAX_NODES = 8192;
    static const int CAPACITY  = 6;

    struct Rect { float x, y, w, h; };

    struct Entry { int idx; float x, y; };

    QuadTree(float x, float y, float w, float h) { Clear(x, y, w, h); }

    void Clear(float x, float y, float w, float h) {
        m_count = 1;
        auto& r = m_pool[0];
        r.x = x; r.y = y; r.w = w; r.h = h;
        r.entries.clear();
        for (int i = 0; i < 4; i++) r.child[i] = -1;
    }

    void Insert(int boidIdx, float bx, float by) {
        InsertNode(0, {boidIdx, bx, by});
    }

    // Query all entries within radius r of (qx, qy)
    void Query(float qx, float qy, float r, std::vector<int>& out) const {
        out.clear();
        QueryNode(0, qx, qy, r, out);
    }

    // Visualization: all node rectangles
    void CollectRects(std::vector<Rect>& out) const {
        out.clear();
        CollectNodeRects(0, out);
    }

    int NodeCount() const { return m_count; }

private:
    struct Node {
        float x, y, w, h;
        int   child[4];          // -1 = leaf
        std::vector<Entry> entries;
    };
    Node m_pool[MAX_NODES];
    int  m_count;

    int NewNode(float x, float y, float w, float h) {
        int i = m_count++;
        auto& n = m_pool[i];
        n.x = x; n.y = y; n.w = w; n.h = h;
        n.entries.clear();
        for (int c = 0; c < 4; c++) n.child[c] = -1;
        return i;
    }

    void Split(int nodeIdx) {
        auto& node = m_pool[nodeIdx];
        float hw = node.w / 2, hh = node.h / 2;
        node.child[0] = NewNode(node.x,      node.y,      hw, hh);
        node.child[1] = NewNode(node.x + hw, node.y,      hw, hh);
        node.child[2] = NewNode(node.x,      node.y + hh, hw, hh);
        node.child[3] = NewNode(node.x + hw, node.y + hh, hw, hh);

        // Redistribute all entries to children
        for (auto& e : node.entries) {
            int q = Quadrant(nodeIdx, e.x, e.y);
            m_pool[node.child[q]].entries.push_back(e);
        }
        node.entries.clear();
    }

    int Quadrant(int nodeIdx, float bx, float by) const {
        auto& n = m_pool[nodeIdx];
        float mx = n.x + n.w / 2, my = n.y + n.h / 2;
        return (by < my) ? ((bx < mx) ? 0 : 1) : ((bx < mx) ? 2 : 3);
    }

    void InsertNode(int nodeIdx, const Entry& e) {
        auto& node = m_pool[nodeIdx];

        if (node.child[0] == -1) {
            // Leaf
            node.entries.push_back(e);
            if ((int)node.entries.size() > CAPACITY && node.w > 4.f && node.h > 4.f)
                Split(nodeIdx);
        } else {
            // Internal: descend
            InsertNode(node.child[Quadrant(nodeIdx, e.x, e.y)], e);
        }
    }

    void QueryNode(int nodeIdx, float qx, float qy, float r, std::vector<int>& out) const {
        auto& node = m_pool[nodeIdx];

        // AABB-circle overlap test
        float cx = node.x + node.w/2, cy = node.y + node.h/2;
        float dx = fabsf(qx - cx) - node.w/2 - r;
        float dy = fabsf(qy - cy) - node.h/2 - r;
        if (dx > 0 || dy > 0) return;

        if (node.child[0] == -1) {
            for (auto& e : node.entries) out.push_back(e.idx);
        } else {
            for (int c = 0; c < 4; c++)
                QueryNode(node.child[c], qx, qy, r, out);
        }
    }

    void CollectNodeRects(int nodeIdx, std::vector<Rect>& out) const {
        auto& node = m_pool[nodeIdx];
        out.push_back({node.x, node.y, node.w, node.h});
        for (int c = 0; c < 4; c++)
            if (node.child[c] != -1)
                CollectNodeRects(node.child[c], out);
    }
};

// ============================================================
// Boid
// ============================================================
struct Boid { float x, y, vx, vy; };

// ============================================================
// Flock — manages boids + quadtree rebuild each frame
// ============================================================
class Flock {
public:
    static constexpr int MAX_BOIDS = 800;
    static constexpr int INIT_BOIDS = 250;

    float sepW    = 1.6f;
    float aliW    = 1.0f;
    float cohW    = 1.0f;
    float maxSpd  = 200.f;
    float maxForce= 600.f;
    float percept = 70.f;
    float separR  = 35.f;
    float margin  = 60.f;
    float edgeW   = 3.0f;

    Flock(int w, int h) : m_winW(w), m_winH(h), m_tree(0, 0, (float)w, (float)h) {
        Resize(INIT_BOIDS);
    }

    void Resize(int n) {
        if (n > MAX_BOIDS) n = MAX_BOIDS;
        if (n < 1) n = 1;
        m_count = n;
        for (int i = 0; i < m_count; i++) {
            auto& b = m_boids[i];
            b.x  = 100.f + (float)(rand() % (m_winW - 200));
            b.y  = 100.f + (float)(rand() % (m_winH - 200));
            float a = (float)(rand() % 628) / 100.f;
            float s = 80.f + (float)(rand() % 120);
            b.vx = s * cosf(a);
            b.vy = s * sinf(a);
        }
    }

    int  Count()     const { return m_count; }
    const Boid& Get(int i) const { return m_boids[i]; }
    const QuadTree& Tree() const { return m_tree; }
    int  TreeNodes() const { return m_tree.NodeCount(); }

    void Update(float dt) {
        // Rebuild quadtree
        m_tree.Clear(0, 0, (float)m_winW, (float)m_winH);
        for (int i = 0; i < m_count; i++)
            m_tree.Insert(i, m_boids[i].x, m_boids[i].y);

        for (int i = 0; i < m_count; i++) {
            auto& b = m_boids[i];

            // Query neighbors
            m_qbuf.clear();
            m_tree.Query(b.x, b.y, percept, m_qbuf);

            float sfx = 0, sfy = 0; // separation
            float afx = 0, afy = 0; // alignment
            float cfx = 0, cfy = 0; // cohesion
            int   sepCnt = 0, neiCnt = 0;

            for (int j : m_qbuf) {
                if (j == i) continue;
                auto& o = m_boids[j];
                float dx = b.x - o.x, dy = b.y - o.y;
                float d2 = dx*dx + dy*dy;

                if (d2 < separR * separR && d2 > 0.001f) {
                    float d = sqrtf(d2);
                    float force = (separR - d) / separR;
                    sfx += dx / d * force;
                    sfy += dy / d * force;
                    sepCnt++;
                }
                afx += o.vx; afy += o.vy;
                cfx += o.x;  cfy += o.y;
                neiCnt++;
            }

            float steerX = 0, steerY = 0;

            // Separation
            if (sepCnt > 0) { steerX += sfx / (float)sepCnt * sepW; steerY += sfy / (float)sepCnt * sepW; }

            // Alignment
            if (neiCnt > 0) {
                afx /= (float)neiCnt; afy /= (float)neiCnt;
                float mag = lenf(afx, afy);
                if (mag > 0.001f) {
                    steerX += (afx / mag * maxSpd - b.vx) * aliW;
                    steerY += (afy / mag * maxSpd - b.vy) * aliW;
                }
            }

            // Cohesion
            if (neiCnt > 0) {
                cfx /= (float)neiCnt; cfy /= (float)neiCnt;
                float dx = cfx - b.x, dy = cfy - b.y;
                float mag = lenf(dx, dy);
                if (mag > 0.001f) {
                    steerX += (dx / mag * maxSpd - b.vx) * cohW;
                    steerY += (dy / mag * maxSpd - b.vy) * cohW;
                }
            }

            // Edge avoidance
            if (b.x < margin)       steerX += edgeW * (margin - b.x) / margin * maxSpd;
            if (b.x > m_winW - margin) steerX -= edgeW * (b.x - (m_winW - margin)) / margin * maxSpd;
            if (b.y < margin)        steerY += edgeW * (margin - b.y) / margin * maxSpd;
            if (b.y > m_winH - margin) steerY -= edgeW * (b.y - (m_winH - margin)) / margin * maxSpd;

            // Clamp steering
            float sm = lenf(steerX, steerY);
            if (sm > maxForce) { steerX = steerX / sm * maxForce; steerY = steerY / sm * maxForce; }

            b.vx += steerX * dt;
            b.vy += steerY * dt;

            // Clamp speed
            float spd = lenf(b.vx, b.vy);
            if (spd > maxSpd) { b.vx = b.vx / spd * maxSpd; b.vy = b.vy / spd * maxSpd; }
            else if (spd < maxSpd * 0.3f) {
                float a = (float)(rand() % 628) / 100.f;
                b.vx += cosf(a) * maxSpd * 0.3f;
                b.vy += sinf(a) * maxSpd * 0.3f;
            }

            b.x += b.vx * dt;
            b.y += b.vy * dt;
            b.x = clampf(b.x, 0.f, (float)m_winW);
            b.y = clampf(b.y, 0.f, (float)m_winH);
        }
    }

private:
    Boid             m_boids[MAX_BOIDS];
    int              m_count = 0;
    int              m_winW, m_winH;
    QuadTree         m_tree;
    std::vector<int> m_qbuf;
};

// ============================================================
// Rendering
// ============================================================
void DrawBoid(GameLib& gl, const Boid& b) {
    int cx = (int)b.x, cy = (int)b.y;
    float ang = atan2f(b.vy, b.vx);
    float cs = cosf(ang), sn = sinf(ang);

    int tipX = (int)(cx + cs * 8);
    int tipY = (int)(cy + sn * 8);
    int lX   = (int)(cx - cs * 5 - sn * 5);
    int lY   = (int)(cy - sn * 5 + cs * 5);
    int rX   = (int)(cx - cs * 5 + sn * 5);
    int rY   = (int)(cy - sn * 5 - cs * 5);

    gl.FillTriangle(tipX, tipY, lX, lY, rX, rY, COLOR_RGB(210, 200, 170));
    int mX = (int)(cx - cs * 2), mY = (int)(cy - sn * 2);
    gl.FillTriangle(tipX, tipY, mX, mY, rX, rY, COLOR_RGB(255, 240, 200));
    gl.FillCircle(cx + (int)(cs * 4), cy + (int)(sn * 4), 1, COLOR_RGB(40, 30, 20));
}

void DrawQuadTree(GameLib& gl, const QuadTree& tree) {
    std::vector<QuadTree::Rect> rects;
    tree.CollectRects(rects);
    for (const auto& r : rects)
        gl.DrawRect((int)r.x, (int)r.y, (int)r.w, (int)r.h, COLOR_RGB(40, 60, 40));
}

void DrawHUD(GameLib& gl, int n, int nodes, int fps,
             const Flock& f, bool showTree, bool showRadius, int sel) {
    char buf[128];
    int y = 6;
    uint32_t tc = COLOR_RGB(200, 200, 200);

    snprintf(buf, sizeof(buf), "Boids: %d  |  FPS: %d  |  QT nodes: %d", n, fps, nodes);
    gl.DrawTextFont(10, y, buf, tc, 16); y += 20;

    snprintf(buf, sizeof(buf), "Sep:%.1f  Ali:%.1f  Coh:%.1f  Spd:%.0f  R:%d",
             f.sepW, f.aliW, f.cohW, f.maxSpd, (int)f.percept);
    gl.DrawTextFont(10, y, buf, tc, 14); y += 18;

    snprintf(buf, sizeof(buf), "[G]树 %s  [R]感知圈 %s  [+/-]数量  [1-7]调参  [SPC]暂停  [Tab]选鸟",
             showTree ? "ON" : "OFF", showRadius ? "ON" : "OFF");
    gl.DrawTextFont(10, y, buf, tc, 14);
}

void DrawRadius(GameLib& gl, const Flock& f, int sel) {
    if (sel < 0 || sel >= f.Count()) return;
    auto& b = f.Get(sel);
    gl.DrawCircle((int)b.x, (int)b.y, (int)f.separR, COLOR_RGB(255, 80, 80));
    gl.DrawCircle((int)b.x, (int)b.y, (int)f.percept, COLOR_RGB(80, 160, 80));
}

// ============================================================
// Main
// ============================================================
int main() {
    const int W = 900, H = 640;
    GameLib gl;
    gl.Open(W, H, "Boids Flocking + Quadtree", true);

    Flock flock(W, H);
    srand((unsigned)time(nullptr));

    bool  showTree   = false;
    bool  showRadius = false;
    bool  paused     = false;
    int   selBoid    = 0;
    float lastTime   = gl.GetTime();
    int   frameCount = 0, fps = 0;
    float fpsTimer   = 0;
    float totalChecks= 0;

    while (!gl.IsClosed()) {
        float now = gl.GetTime();
        float dt  = now - lastTime;
        lastTime  = now;
        if (dt > 0.05f) dt = 0.016f;

        frameCount++;
        fpsTimer += dt;
        if (fpsTimer >= 1.f) { fps = frameCount; frameCount = 0; fpsTimer = 0; }

        // --- Input ---
        if (gl.IsKeyPressed(KEY_G))         showTree   = !showTree;
        if (gl.IsKeyPressed(KEY_R))         showRadius = !showRadius;
        if (gl.IsKeyPressed(KEY_SPACE))     paused     = !paused;
        if (gl.IsKeyPressed(KEY_ADD))       flock.Resize(flock.Count() + 50);
        if (gl.IsKeyPressed(KEY_SUBTRACT))  flock.Resize(flock.Count() - 50);
        if (gl.IsKeyPressed(KEY_TAB))       selBoid = (selBoid + 1) % flock.Count();

        // Parameter tuning
        if (gl.IsKeyPressed(KEY_1)) flock.sepW += 0.2f;
        if (gl.IsKeyPressed(KEY_2)) flock.aliW += 0.2f;
        if (gl.IsKeyPressed(KEY_3)) flock.cohW += 0.2f;
        if (gl.IsKeyPressed(KEY_Q)) flock.sepW -= 0.2f;
        if (gl.IsKeyPressed(KEY_W)) flock.aliW -= 0.2f;
        if (gl.IsKeyPressed(KEY_E)) flock.cohW -= 0.2f;
        if (gl.IsKeyPressed(KEY_4)) flock.maxSpd += 20.f;
        if (gl.IsKeyPressed(KEY_5)) flock.maxSpd -= 20.f;
        if (gl.IsKeyPressed(KEY_6)) { flock.percept += 5.f; if (flock.percept > 200) flock.percept = 200; }
        if (gl.IsKeyPressed(KEY_7)) { flock.percept -= 5.f; if (flock.percept < 10)  flock.percept = 10; }

        // --- Update ---
        if (!paused)
            flock.Update(dt);

        // --- Render ---
        gl.Clear(COLOR_RGB(18, 20, 28));

        for (int i = 0; i < flock.Count(); i++)
            DrawBoid(gl, flock.Get(i));

        if (showTree)
            DrawQuadTree(gl, flock.Tree());

        if (showRadius)
            DrawRadius(gl, flock, selBoid);

        if (showRadius) {
            auto& b = flock.Get(selBoid);
            gl.DrawCircle((int)b.x, (int)b.y, 12, COLOR_RGB(255, 255, 100));
        }

        DrawHUD(gl, flock.Count(), flock.TreeNodes(), fps,
                flock, showTree, showRadius, selBoid);

        int n = flock.Count();
        totalChecks = (float)n * (n - 1) / 2;
        char buf[64];
        snprintf(buf, sizeof(buf), "O(n^2)=%.0f pairs | QT queries: ~%.0f per frame",
                 totalChecks, (float)n * 6.f);
        gl.DrawTextFont(10, H - 22, buf, COLOR_RGB(140, 140, 140), 14);

        gl.Update();
        gl.WaitFrame(60);
    }

    return 0;
}
