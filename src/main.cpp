// PRISM — a neon twin-stick arena shooter. C++17 + SDL3.
//
// Gamepad: left stick move, right stick aim & fire, LB/RB/LT/RT bomb,
//          Start pause, A confirm, Back quit/title, Y toggle music.
// Keyboard/mouse: WASD move, mouse (hold LMB) or arrow keys fire,
//          Space / RMB bomb, Esc pause, Enter confirm, M music, F11 fullscreen.
//
// Options: --fullscreen  --no-rumble
// Testing: --autoplay (bot plays)  --shot FILE.bmp --shot-at SECONDS (save frame & exit)
//          Test modes never open the gamepad, so they never rumble it.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "audio.hpp"
#include "font.hpp"

// ---------------------------------------------------------------- math
constexpr int W = 1280, H = 720;
constexpr float PI = 3.14159265f, TAU = 6.2831853f;
constexpr float AX0 = 24, AY0 = 24, AX1 = W - 24, AY1 = H - 24;  // arena bounds
constexpr float STEP = 1.f / 120.f;

struct V2 {
    float x = 0, y = 0;
};
inline V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
inline V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
inline V2 operator-(V2 a) { return {-a.x, -a.y}; }
inline V2 operator*(V2 a, float k) { return {a.x * k, a.y * k}; }
inline V2 operator/(V2 a, float k) { return {a.x / k, a.y / k}; }
inline V2& operator+=(V2& a, V2 b) { a.x += b.x; a.y += b.y; return a; }
inline V2& operator-=(V2& a, V2 b) { a.x -= b.x; a.y -= b.y; return a; }
inline V2& operator*=(V2& a, float k) { a.x *= k; a.y *= k; return a; }
inline float dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
inline float len2(V2 a) { return a.x * a.x + a.y * a.y; }
inline float len(V2 a) { return std::sqrt(len2(a)); }
inline V2 norm(V2 a) { float l = len(a); return l > 1e-6f ? a / l : V2{0, 0}; }
inline V2 fromAng(float a) { return {std::cos(a), std::sin(a)}; }
inline V2 rot(V2 v, float a) { float c = std::cos(a), s = std::sin(a); return {v.x * c - v.y * s, v.x * s + v.y * c}; }
inline V2 perp(V2 v) { return {-v.y, v.x}; }
inline float angOf(V2 v) { return std::atan2(v.y, v.x); }
inline float wrapAng(float a) {
    while (a > PI) a -= TAU;
    while (a < -PI) a += TAU;
    return a;
}
inline float turnToward(float a, float target, float maxStep) {
    float d = std::clamp(wrapAng(target - a), -maxStep, maxStep);
    return a + d;
}
inline float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

static std::mt19937 rng{std::random_device{}()};
inline float frand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
inline int irand(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }

struct Col {
    float r = 1, g = 1, b = 1, a = 1;
};
inline Col operator*(Col c, float k) { return {c.r * k, c.g * k, c.b * k, c.a}; }
inline Col withA(Col c, float a) { c.a = a; return c; }
inline Col mix(Col a, Col b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
Col hsv(float h, float s, float v) {
    h = h - std::floor(h);
    float i = std::floor(h * 6), f = h * 6 - i;
    float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (int(i) % 6) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}
const Col WHITE{1, 1, 1, 1};

// ---------------------------------------------------------------- drawing
// Everything is batched into one textured-geometry call with additive blending.
// The texture is a soft radial dot: lines sample it across their width for an
// anti-aliased glowing core, dots sample the full disc.
struct Draw {
    SDL_Renderer* ren = nullptr;
    SDL_Texture* tex = nullptr;
    std::vector<SDL_Vertex> vtx;
    std::vector<int> idx;
    V2 off;

    void init(SDL_Renderer* r) {
        ren = r;
        const int N = 64;
        std::vector<uint8_t> px(N * N * 4);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                float dx = (x + 0.5f) / N * 2 - 1, dy = (y + 0.5f) / N * 2 - 1;
                float rr = std::sqrt(dx * dx + dy * dy);
                float a = std::pow(clamp01((1 - rr) / 0.6f), 1.6f);
                uint8_t* p = &px[(y * N + x) * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = uint8_t(a * 255);
            }
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, N, N);
        SDL_UpdateTexture(tex, nullptr, px.data(), N * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        vtx.reserve(1 << 16);
        idx.reserve(1 << 17);
    }
    void push(V2 p, Col c, float u, float v) {
        SDL_Vertex x;
        x.position = {p.x + off.x, p.y + off.y};
        x.color = {clamp01(c.r), clamp01(c.g), clamp01(c.b), clamp01(c.a)};
        x.tex_coord = {u, v};
        vtx.push_back(x);
    }
    void quad(V2 a, V2 b, V2 c, V2 d, Col col, const float* uv) {
        int base = int(vtx.size());
        push(a, col, uv[0], uv[1]);
        push(b, col, uv[2], uv[3]);
        push(c, col, uv[4], uv[5]);
        push(d, col, uv[6], uv[7]);
        const int q[6] = {0, 1, 2, 0, 2, 3};
        for (int i : q) idx.push_back(base + i);
    }
    void solid(V2 a, V2 b, V2 c, V2 d, Col col) {
        static const float uv[8] = {.5f, .5f, .5f, .5f, .5f, .5f, .5f, .5f};
        quad(a, b, c, d, col, uv);
    }
    void line(V2 a, V2 b, float w, Col c) {
        V2 d = b - a;
        float L = len(d);
        V2 dir = L > 1e-4f ? d / L : V2{1, 0};
        V2 n = perp(dir) * w;
        V2 e = dir * (w * 0.5f);
        a -= e;
        b += e;
        static const float uv[8] = {.5f, 0, .5f, 0, .5f, 1, .5f, 1};
        quad(a + n, b + n, b - n, a - n, c, uv);
    }
    void dot(V2 p, float r, Col c) {
        static const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
        quad({p.x - r, p.y - r}, {p.x + r, p.y - r}, {p.x + r, p.y + r}, {p.x - r, p.y + r}, c, uv);
    }
    void poly(const std::vector<V2>& pts, V2 pos, float ang, float sc, float w, Col c, bool closed = true) {
        float cs = std::cos(ang) * sc, sn = std::sin(ang) * sc;
        auto xf = [&](V2 p) { return V2{pos.x + p.x * cs - p.y * sn, pos.y + p.x * sn + p.y * cs}; };
        size_t n = pts.size();
        for (size_t i = 0; i + (closed ? 0 : 1) < n; ++i) line(xf(pts[i]), xf(pts[(i + 1) % n]), w, c);
    }
    void circle(V2 p, float r, float w, Col c, int seg = 40) {
        V2 prev = p + V2{r, 0};
        for (int i = 1; i <= seg; ++i) {
            V2 q = p + fromAng(TAU * i / seg) * r;
            line(prev, q, w, c);
            prev = q;
        }
    }
    void flush() {
        if (!idx.empty())
            SDL_RenderGeometry(ren, tex, vtx.data(), int(vtx.size()), idx.data(), int(idx.size()));
        vtx.clear();
        idx.clear();
    }
};

float textWidth(const std::string& s, float size) {
    return s.empty() ? 0 : (s.size() * 5.5f - 1.5f) * size / 6.f;
}
// pos = top-left (align 0), top-center (0.5) or top-right (1)
void drawText(Draw& dr, const std::string& s, V2 pos, float size, Col c, float align = 0, float w = 0) {
    if (w <= 0) w = std::max(1.1f, size * 0.065f);
    float u = size / 6.f;
    float x = pos.x - textWidth(s, size) * align;
    for (char ch : s) {
        const char* g = glyph(char(std::toupper((unsigned char)ch)));
        bool have = false;
        V2 prev;
        for (const char* p = g; *p;) {
            if (*p == ' ') { have = false; ++p; continue; }
            V2 q{x + (p[0] - '0') * u, pos.y + (p[1] - '0') * u};
            if (have) dr.line(prev, q, w, c);
            prev = q;
            have = true;
            p += 2;
        }
        x += 5.5f * u;
    }
}

std::string commas(long long v) {
    std::string s = std::to_string(v), out;
    int n = int(s.size());
    for (int i = 0; i < n; ++i) {
        out += s[i];
        if ((n - i - 1) % 3 == 0 && i != n - 1) out += ',';
    }
    return out;
}

// ---------------------------------------------------------------- shapes
std::vector<V2> ngon(int n, float r, float ph = 0) {
    std::vector<V2> v;
    for (int i = 0; i < n; ++i) v.push_back(fromAng(ph + TAU * i / n) * r);
    return v;
}
std::vector<V2> starShape(int n, float r1, float r2) {
    std::vector<V2> v;
    for (int i = 0; i < n * 2; ++i) v.push_back(fromAng(-PI / 2 + PI * i / n) * (i % 2 ? r2 : r1));
    return v;
}
const std::vector<V2> SH_PLAYER = {{17, 0}, {-11, 11}, {-5, 0}, {-11, -11}};
const std::vector<V2> SH_DRIFTER = ngon(3, 17);
const std::vector<V2> SH_DRIFTER_IN = ngon(3, 7, PI);
const std::vector<V2> SH_SEEKER = {{16, 0}, {0, 9}, {-12, 0}, {0, -9}};
const std::vector<V2> SH_WEAVER = ngon(6, 15);
const std::vector<V2> SH_WEAVER_IN = ngon(3, 8);
const std::vector<V2> SH_SPLITTER = starShape(5, 19, 8);
const std::vector<V2> SH_SHARD = {{10, 0}, {-7, 7}, {-4, 0}, {-7, -7}};
const std::vector<V2> SH_LANCER = {{24, 0}, {0, 6}, {-14, 0}, {0, -6}};

// ---------------------------------------------------------------- grid
// Spring-mass grid: every node is a displacement from its rest position,
// pulled back home and coupled to its neighbours, so pushes ripple outward.
struct Grid {
    int nx = 39, ny = 22;
    float sx = 0, sy = 0;
    std::vector<V2> d, v, acc;

    void init() {
        sx = (AX1 - AX0) / (nx - 1);
        sy = (AY1 - AY0) / (ny - 1);
        d.assign(nx * ny, {});
        v.assign(nx * ny, {});
        acc.assign(nx * ny, {});
    }
    V2 rest(int i, int j) const { return {AX0 + i * sx, AY0 + j * sy}; }
    void update(float dt) {
        const float KN = 700, KR = 40, DAMP = 3.2f;
        for (int j = 1; j < ny - 1; ++j)
            for (int i = 1; i < nx - 1; ++i) {
                int k = j * nx + i;
                V2 lap = d[k - 1] + d[k + 1] + d[k - nx] + d[k + nx] - d[k] * 4;
                acc[k] = lap * KN - d[k] * KR - v[k] * DAMP;
            }
        for (int j = 1; j < ny - 1; ++j)
            for (int i = 1; i < nx - 1; ++i) {
                int k = j * nx + i;
                v[k] += acc[k] * dt;
                d[k] += v[k] * dt;
                float l = len(d[k]);
                if (l > 50) d[k] *= 50 / l;
            }
    }
    template <class F>
    void around(V2 c, float R, F f) {
        int i0 = std::max(1, int((c.x - R - AX0) / sx)), i1 = std::min(nx - 2, int((c.x + R - AX0) / sx) + 1);
        int j0 = std::max(1, int((c.y - R - AY0) / sy)), j1 = std::min(ny - 2, int((c.y + R - AY0) / sy) + 1);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                int k = j * nx + i;
                V2 r = rest(i, j) + d[k] - c;
                float dist = len(r);
                if (dist < R) f(k, dist > 0.01f ? r / dist : V2{0, 0}, 1 - dist / R);
            }
    }
    // positive strength pushes outward, negative pulls inward (velocity kick)
    void impulse(V2 c, float s, float R) {
        around(c, R, [&](int k, V2 dir, float f) { v[k] += dir * (s * f * f); });
    }
    // sustained inward pull (acceleration)
    void pull(V2 c, float s, float R, float dt) {
        around(c, R, [&](int k, V2 dir, float f) { v[k] -= dir * (s * f * dt); });
    }
    void draw(Draw& dr, float t) {
        const Col cold{0.10f, 0.16f, 0.55f}, hot{0.55f, 0.75f, 1.0f};
        auto P = [&](int i, int j) { return rest(i, j) + d[j * nx + i]; };
        auto seg = [&](int i0, int j0, int i1, int j1, bool major) {
            float e = clamp01((len(d[j0 * nx + i0]) + len(d[j1 * nx + i1])) * 0.03f);
            float base = major ? 0.42f : 0.22f;
            dr.line(P(i0, j0), P(i1, j1), major ? 1.1f : 0.8f, mix(cold, hot, e) * (base + e * 0.7f));
        };
        (void)t;
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                if (i + 1 < nx) seg(i, j, i + 1, j, j % 4 == 0);
                if (j + 1 < ny) seg(i, j, i, j + 1, i % 4 == 0);
            }
    }
};

// ---------------------------------------------------------------- entities
enum EType { DRIFTER, SEEKER, WEAVER, SPLITTER, SHARD, LANCER, SINGULARITY, NUM_TYPES };
struct EnemyDef {
    float r;
    int hp;
    int score;
    Col col;
};
const EnemyDef DEFS[NUM_TYPES] = {
    {16, 1, 50, {1.0f, 0.72f, 0.15f}},    // drifter: amber triangle, wanders
    {13, 1, 75, {1.0f, 0.25f, 0.85f}},    // seeker: magenta kite, homes in
    {15, 1, 100, {0.3f, 1.0f, 0.45f}},    // weaver: green hex, dodges shots
    {18, 1, 100, {1.0f, 0.45f, 0.1f}},    // splitter: orange star, breaks into shards
    {9, 1, 25, {1.0f, 0.78f, 0.4f}},      // shard: spirals in
    {14, 2, 150, {1.0f, 0.18f, 0.22f}},   // lancer: red needle, aims then dashes
    {26, 16, 1000, {0.7f, 0.5f, 1.0f}},   // singularity: gravity well
};
constexpr float SPAWN_TIME = 0.6f;
constexpr float SING_RANGE = 380.f;

struct Enemy {
    EType type = DRIFTER;
    V2 p, v, dir{1, 0};
    float ang = 0, spin = 0, r = 16, t = 0, timer = 0, side = 1, hitFlash = 0;
    float spawnT = SPAWN_TIME;
    int hp = 1, state = 0, fed = 0;
    bool dead = false, charged = false;
    bool active() const { return !dead && spawnT <= 0; }
};
struct Bullet {
    V2 p, v;
    bool dead = false;
};
struct Particle {
    V2 p, v;
    Col c;
    float life, maxLife, w;
};
struct Floater {
    V2 p;
    std::string s;
    float t;
    Col c;
};

enum class Mode { Title, Play, Paused, GameOver };

// ---------------------------------------------------------------- game
struct Options {
    bool fullscreen = false, rumble = true, autoplay = false;
    std::string shot;
    float shotAt = 3.f;
};

struct Game {
    Options opt;
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture* scene = nullptr;
    SDL_Texture* bloom[4] = {};
    Draw dr;
    Audio audio;
    Grid grid;
    SDL_Gamepad* pad = nullptr;
    bool running = true;
    std::string prefPath;

    Mode mode = Mode::Title;
    float modeT = 0, clock = 0;

    // player
    V2 pp{W / 2.f, H / 2.f}, pv;
    float pang = -PI / 2, fireT = 0, invT = 0, deadT = 0;
    bool alive = true;

    int lives = 3, bombs = 3, streak = 0, mult = 1;
    long long score = 0, hiscore = 0, nextLife = 100000, nextBomb = 75000;
    bool newHigh = false;
    float gameT = 0, spawnCD = 0, eventCD = 0, timeScale = 1;

    std::vector<Enemy> enemies, pending;
    std::vector<Bullet> bullets;
    std::vector<Particle> parts;
    size_t partCursor = 0;
    std::vector<Floater> floaters;
    std::string banner;
    float bannerT = 0;
    Col bannerC;
    float shake = 0, flash = 0, bombRingT = -1;
    V2 bombRingP;
    float attractT = 0;

    // input
    V2 inMove, inAim, mouseGame;
    bool inFire = false, usingMouse = false, trigHeld[2] = {};
    float shotTimer = 0;

    // ------------------------------------------------ setup
    bool init() {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return false;
        }
        if (!SDL_CreateWindowAndRenderer("PRISM", W, H, SDL_WINDOW_RESIZABLE, &win, &ren)) {
            SDL_Log("window failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetWindowMinimumSize(win, 640, 360);
        SDL_SetRenderVSync(ren, 1);
        if (opt.fullscreen) SDL_SetWindowFullscreen(win, true);

        scene = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, W, H);
        for (int i = 0; i < 4; ++i) {
            bloom[i] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, W >> (i + 1), H >> (i + 1));
            SDL_SetTextureScaleMode(bloom[i], SDL_SCALEMODE_LINEAR);
        }
        SDL_SetTextureScaleMode(scene, SDL_SCALEMODE_LINEAR);
        if (!scene || !bloom[3]) {
            SDL_Log("render targets unsupported: %s", SDL_GetError());
            return false;
        }
        dr.init(ren);
        grid.init();
        audio.init();
        audio.setMusicLevel(0.4f);

        if (char* p = SDL_GetPrefPath("prism", "prism")) {
            prefPath = p;
            SDL_free(p);
        }
        loadHigh();
        return true;
    }
    void shutdown() {
        audio.shutdown();
        if (pad) SDL_CloseGamepad(pad);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
    }
    void loadHigh() {
        if (prefPath.empty()) return;
        if (FILE* f = std::fopen((prefPath + "highscore.txt").c_str(), "r")) {
            if (std::fscanf(f, "%lld", &hiscore) != 1) hiscore = 0;
            std::fclose(f);
        }
    }
    void saveHigh() {
        if (prefPath.empty() || opt.autoplay) return;
        if (FILE* f = std::fopen((prefPath + "highscore.txt").c_str(), "w")) {
            std::fprintf(f, "%lld\n", hiscore);
            std::fclose(f);
        }
    }

    bool testMode() const { return opt.autoplay || !opt.shot.empty(); }
    void rumble(float lo, float hi, int ms) {
        if (pad && opt.rumble && !testMode())
            SDL_RumbleGamepad(pad, Uint16(clamp01(lo) * 65535), Uint16(clamp01(hi) * 65535), Uint32(ms));
    }
    float pan(float x) const { return (x - W / 2.f) / (W / 2.f) * 0.6f; }

    // ------------------------------------------------ particles & fx
    void emit(V2 p, V2 v, Col c, float life, float w = 2.f) {
        Particle np{p, v, c, life, life, w};
        const size_t CAP = 7000;
        if (parts.size() < CAP) parts.push_back(np);
        else parts[partCursor++ % CAP] = np;
    }
    void burst(V2 p, Col c, int n, float smin, float smax, float lmin = 0.45f, float lmax = 1.2f, float w = 2.f) {
        for (int i = 0; i < n; ++i) {
            float s = frand(smin, smax) * (frand(0, 1) < 0.15f ? 1.7f : 1.f);
            Col cc = mix(c, WHITE, frand(0, 0.45f));
            emit(p, fromAng(frand(0, TAU)) * s, cc, frand(lmin, lmax), w * frand(0.7f, 1.2f));
        }
    }
    void setBanner(const std::string& s, Col c) {
        banner = s;
        bannerT = 2.2f;
        bannerC = c;
    }

    // ------------------------------------------------ game flow
    void startGame() {
        mode = Mode::Play;
        modeT = 0;
        enemies.clear();
        pending.clear();
        bullets.clear();
        floaters.clear();
        pp = {W / 2.f, H / 2.f};
        pv = {};
        pang = -PI / 2;
        alive = true;
        invT = 2.f;
        lives = 3;
        bombs = 3;
        streak = 0;
        mult = 1;
        score = 0;
        newHigh = false;
        nextLife = 100000;
        nextBomb = 75000;
        gameT = 0;
        spawnCD = 1.2f;
        eventCD = 14.f;
        timeScale = 1;
        grid.impulse(pp, 900, 400);
        audio.setMusicLevel(1.f);
        audio.play(Sfx::ExtraLife, 0.6f);
        SDL_HideCursor();
    }
    void toTitle() {
        if (score > hiscore) hiscore = score;
        saveHigh();
        mode = Mode::Title;
        modeT = 0;
        enemies.clear();
        bullets.clear();
        audio.setMusicLevel(0.4f);
        SDL_ShowCursor();
    }
    void gameOver() {
        mode = Mode::GameOver;
        modeT = 0;
        if (score > hiscore) {
            hiscore = score;
            newHigh = true;
        }
        saveHigh();
        audio.setMusicLevel(0.25f);
        SDL_ShowCursor();
    }

    void addScore(long long pts) {
        score += pts * mult;
        while (score >= nextLife) {
            nextLife *= 2;
            ++lives;
            setBanner("EXTRA LIFE", {0.4f, 1, 0.5f});
            audio.play(Sfx::ExtraLife);
            rumble(0.2f, 0.4f, 200);
        }
        while (score >= nextBomb) {
            nextBomb += 150000;
            ++bombs;
            setBanner("EXTRA BOMB", {1, 0.8f, 0.3f});
            audio.play(Sfx::ExtraLife, 0.8f, 0, 1.25f);
        }
    }
    int multForStreak(int s) const {
        static const int th[] = {0, 20, 50, 100, 170, 260, 380, 530, 720, 1000};
        int m = 1;
        for (int i = 0; i < 10; ++i)
            if (s >= th[i]) m = i + 1;
        return m;
    }

    Enemy makeEnemy(EType t, V2 p) {
        Enemy e;
        e.type = t;
        e.p = p;
        e.r = DEFS[t].r;
        e.hp = DEFS[t].hp;
        e.ang = frand(0, TAU);
        e.spin = frand(1.5f, 3.f) * (irand(0, 1) ? 1 : -1);
        e.side = irand(0, 1) ? 1.f : -1.f;
        e.dir = fromAng(frand(0, TAU));
        if (t == LANCER) e.timer = frand(1.3f, 1.9f);
        return e;
    }
    void spawn(EType t, V2 p, bool quiet = false) {
        p.x = std::clamp(p.x, AX0 + 30, AX1 - 30);
        p.y = std::clamp(p.y, AY0 + 30, AY1 - 30);
        pending.push_back(makeEnemy(t, p));
        if (!quiet) audio.play(Sfx::Spawn, 0.7f, pan(p.x), frand(0.9f, 1.15f));
    }
    V2 safeSpot(float minDist = 280) {
        V2 p;
        for (int i = 0; i < 30; ++i) {
            p = {frand(AX0 + 40, AX1 - 40), frand(AY0 + 40, AY1 - 40)};
            if (len(p - pp) > minDist) break;
        }
        return p;
    }
    int countType(EType t) const {
        int n = 0;
        for (auto& e : enemies) n += (!e.dead && e.type == t);
        for (auto& e : pending) n += (e.type == t);
        return n;
    }

    void killEnemy(Enemy& e, bool scored) {
        if (e.dead) return;
        e.dead = true;
        const EnemyDef& d = DEFS[e.type];
        bool big = e.type == SINGULARITY;
        burst(e.p, d.col, big ? 220 : 42, 80, big ? 700 : 480, 0.5f, big ? 1.8f : 1.1f);
        grid.impulse(e.p, big ? 1800 : 380, big ? 420 : 130);
        audio.play(big ? Sfx::BigExplode : Sfx::Explode, big ? 1.f : 0.55f, pan(e.p.x), frand(0.85f, 1.2f));
        if (big) {
            shake = std::max(shake, 10.f);
            rumble(0.5f, 0.6f, 300);
        }
        if (!scored) return;
        ++streak;
        int m = multForStreak(streak);
        if (m > mult) {
            mult = m;
            setBanner("MULTIPLIER X" + std::to_string(mult), {0.4f, 0.9f, 1});
            audio.play(Sfx::MultUp);
        }
        addScore(d.score);
        if (big) floaters.push_back({e.p, "+" + commas((long long)d.score * mult), 1.4f, d.col});
        rumble(0.05f, 0.12f, 40);
        if (e.type == SPLITTER) {
            float a0 = frand(0, TAU);
            for (int i = 0; i < 3; ++i) {
                V2 dir = fromAng(a0 + TAU * i / 3);
                Enemy s = makeEnemy(SHARD, e.p + dir * 14);
                s.spawnT = 0;
                s.v = dir * 260;
                s.side = (i % 2) ? 1.f : -1.f;
                pending.push_back(s);
            }
        }
    }
    void burstSingularity(Enemy& s) {
        s.dead = true;
        burst(s.p, DEFS[SINGULARITY].col, 260, 100, 800, 0.6f, 1.8f);
        grid.impulse(s.p, 2200, 500);
        audio.play(Sfx::BigExplode, 1.f, pan(s.p.x));
        shake = std::max(shake, 12.f);
        for (int i = 0; i < 8; ++i) {
            Enemy k = makeEnemy(SEEKER, s.p + fromAng(TAU * i / 8) * 20);
            k.spawnT = 0;
            k.v = fromAng(TAU * i / 8) * 420;
            pending.push_back(k);
        }
    }
    void playerDie() {
        alive = false;
        deadT = 2.4f;
        timeScale = 0.25f;
        --lives;
        for (int i = 0; i < 5; ++i) burst(pp, hsv(i / 5.f, 0.6f, 1), 120, 100, 900, 0.8f, 2.2f, 2.5f);
        burst(pp, WHITE, 150, 50, 400, 0.5f, 1.4f, 3.f);
        grid.impulse(pp, 2600, 750);
        shake = 20;
        flash = 0.7f;
        audio.play(Sfx::Death, 1.f, pan(pp.x));
        rumble(1, 1, 700);
        for (auto& e : enemies) killEnemy(e, false);
        pending.clear();
        bullets.clear();
        streak = 0;
        mult = 1;
    }
    void useBomb() {
        if (mode != Mode::Play || !alive || bombs <= 0) return;
        --bombs;
        for (auto& e : enemies) killEnemy(e, false);
        pending.clear();
        grid.impulse(pp, 3000, 1400);
        burst(pp, {0.6f, 0.9f, 1}, 300, 300, 1400, 0.6f, 1.6f, 2.5f);
        bombRingT = 0;
        bombRingP = pp;
        shake = 14;
        flash = 0.5f;
        audio.play(Sfx::Bomb, 1.f);
        rumble(0.9f, 0.7f, 600);
        spawnCD = std::max(spawnCD, 1.f);
    }

    // ------------------------------------------------ director
    void spawnRandom() {
        float w[NUM_TYPES] = {};
        w[DRIFTER] = 10;
        if (gameT > 8) w[SEEKER] = 8;
        if (gameT > 25) w[WEAVER] = 6;
        if (gameT > 40) w[SPLITTER] = 5;
        if (gameT > 60) w[LANCER] = 4;
        int singCap = std::min(3, 1 + int(gameT / 150));
        if (gameT > 80 && countType(SINGULARITY) < singCap) w[SINGULARITY] = 1.2f;
        float total = 0;
        for (float x : w) total += x;
        float r = frand(0, total);
        int t = 0;
        for (; t < NUM_TYPES - 1; ++t) {
            if (r < w[t]) break;
            r -= w[t];
        }
        if (w[t] <= 0) t = DRIFTER;
        spawn(EType(t), safeSpot(t == SINGULARITY ? 350 : 280));
    }
    void spawnEvent() {
        std::vector<int> opts = {0, 1};
        if (gameT > 40) opts.push_back(3);
        if (gameT > 50) opts.push_back(2);
        int ev = opts[irand(0, int(opts.size()) - 1)];
        if (ev == 0) {  // corner swarm
            int k = std::min(8, 3 + int(gameT / 40));
            V2 corners[4] = {{AX0 + 60, AY0 + 60}, {AX1 - 60, AY0 + 60}, {AX0 + 60, AY1 - 60}, {AX1 - 60, AY1 - 60}};
            for (V2 c : corners) {
                if (len(c - pp) < 220) continue;
                for (int i = 0; i < k; ++i) spawn(SEEKER, c + V2{frand(-40, 40), frand(-40, 40)}, i > 0);
            }
        } else if (ev == 1) {  // ring around the player
            int n = std::min(28, 12 + int(gameT / 20));
            EType t = (gameT > 60 && irand(0, 1)) ? WEAVER : DRIFTER;
            float a0 = frand(0, TAU);
            for (int i = 0; i < n; ++i) spawn(t, pp + fromAng(a0 + TAU * i / n) * 300, i > 0);
        } else if (ev == 2) {  // lancer line
            int n = irand(6, 10);
            int edge = irand(0, 3);
            for (int i = 0; i < n; ++i) {
                float f = (i + 0.5f) / n;
                V2 p = edge == 0 ? V2{AX0 + 40, AY0 + f * (AY1 - AY0)}
                     : edge == 1 ? V2{AX1 - 40, AY0 + f * (AY1 - AY0)}
                     : edge == 2 ? V2{AX0 + f * (AX1 - AX0), AY0 + 40}
                                 : V2{AX0 + f * (AX1 - AX0), AY1 - 40};
                if (len(p - pp) > 160) spawn(LANCER, p, i > 0);
            }
        } else {  // splitter pack
            int n = irand(4, 6);
            for (int i = 0; i < n; ++i) spawn(SPLITTER, safeSpot(320), i > 0);
        }
    }
    void director(float dt) {
        if (enemies.size() > 240) return;
        spawnCD -= dt;
        if (spawnCD <= 0) {
            int n = std::min(7, 1 + int(gameT / 30));
            for (int i = 0; i < n; ++i) spawnRandom();
            spawnCD = std::max(0.3f, 1.25f - gameT * 0.006f) * frand(0.7f, 1.3f);
        }
        eventCD -= dt;
        if (eventCD <= 0 && gameT > 10) {
            spawnEvent();
            eventCD = frand(14, 22) - std::min(6.f, gameT / 60);
        }
    }

    // ------------------------------------------------ simulation
    void fire() {
        V2 d = norm(inAim);
        float a = angOf(d);
        int level = mult >= 6 ? 2 : mult >= 3 ? 1 : 0;
        auto shot = [&](float da, float offs) {
            V2 dir = fromAng(a + da + frand(-0.015f, 0.015f));
            bullets.push_back({pp + dir * 14 + perp(dir) * offs, dir * 1150});
        };
        if (level == 0) {
            shot(0, -5);
            shot(0, 5);
        } else if (level == 1) {
            shot(-0.09f, 0);
            shot(0, 0);
            shot(0.09f, 0);
        } else {
            shot(0, -5);
            shot(0, 5);
            shot(-0.14f, 0);
            shot(0.14f, 0);
        }
        audio.play(Sfx::Shoot, 0.45f, pan(pp.x), frand(0.94f, 1.06f));
    }

    void updatePlayer(float dt) {
        if (!alive) {
            deadT -= dt;
            if (deadT <= 0) {
                if (lives <= 0) {
                    gameOver();
                } else {
                    alive = true;
                    pp = {W / 2.f, H / 2.f};
                    pv = {};
                    invT = 2.5f;
                    spawnCD = 1.5f;
                    grid.impulse(pp, -900, 300);
                    audio.play(Sfx::ExtraLife, 0.5f, 0, 0.8f);
                }
            }
            return;
        }
        V2 target = inMove * 430;
        pv += (target - pv) * (1 - std::exp(-dt * 14));
        pp += pv * dt;
        pp.x = std::clamp(pp.x, AX0 + 12, AX1 - 12);
        pp.y = std::clamp(pp.y, AY0 + 12, AY1 - 12);
        if (len(inMove) > 0.1f) {
            pang = turnToward(pang, angOf(inMove), dt * 18);
            if (frand(0, 1) < 0.7f) {
                V2 back = fromAng(pang + PI + frand(-0.35f, 0.35f));
                emit(pp + back * 9, back * frand(120, 280) + pv * 0.3f, mix({1, 0.5f, 0.1f}, {1, 0.9f, 0.4f}, frand(0, 1)),
                     frand(0.2f, 0.4f), 1.6f);
            }
        }
        invT -= dt;
        fireT -= dt;
        if (inFire && len(inAim) > 0.01f && fireT <= 0) {
            fire();
            fireT += mult >= 6 ? 0.07f : 0.082f;
            if (fireT < 0) fireT = 0;
        }
        if (fireT < 0) fireT = 0;
    }

    void updateBullets(float dt) {
        for (auto& b : bullets) {
            b.p += b.v * dt;
            if (b.p.x < AX0 || b.p.x > AX1 || b.p.y < AY0 || b.p.y > AY1) {
                b.dead = true;
                b.p.x = std::clamp(b.p.x, AX0, AX1);
                b.p.y = std::clamp(b.p.y, AY0, AY1);
                for (int i = 0; i < 5; ++i)
                    emit(b.p, rot(-norm(b.v), frand(-1.2f, 1.2f)) * frand(80, 260), {1, 0.85f, 0.5f}, frand(0.15f, 0.35f), 1.4f);
                grid.impulse(b.p, 90, 60);
            }
        }
    }

    void bounce(Enemy& e) {
        float r = e.r;
        if (e.p.x < AX0 + r) { e.p.x = AX0 + r; e.v.x = std::fabs(e.v.x); e.dir.x = std::fabs(e.dir.x); }
        if (e.p.x > AX1 - r) { e.p.x = AX1 - r; e.v.x = -std::fabs(e.v.x); e.dir.x = -std::fabs(e.dir.x); }
        if (e.p.y < AY0 + r) { e.p.y = AY0 + r; e.v.y = std::fabs(e.v.y); e.dir.y = std::fabs(e.dir.y); }
        if (e.p.y > AY1 - r) { e.p.y = AY1 - r; e.v.y = -std::fabs(e.v.y); e.dir.y = -std::fabs(e.dir.y); }
    }
    void steer(Enemy& e, V2 desired, float rate, float dt) { e.v += (desired - e.v) * clamp01(rate * dt); }

    void updateEnemies(float dt) {
        V2 target = alive ? pp : V2{W / 2.f, H / 2.f};
        for (auto& e : enemies) {
            if (e.dead) continue;
            e.hitFlash -= dt;
            e.t += dt;
            if (e.spawnT > 0) {
                e.spawnT -= dt;
                continue;
            }
            V2 to = target - e.p;
            float dist = len(to);
            V2 toN = dist > 0.01f ? to / dist : V2{1, 0};
            switch (e.type) {
                case DRIFTER: {
                    e.dir = rot(e.dir, frand(-2.f, 2.f) * dt);
                    steer(e, norm(e.dir * 0.75f + toN * 0.35f) * 115, 2, dt);
                    e.ang += e.spin * dt;
                } break;
                case SEEKER: {
                    steer(e, toN * 265, 2.6f, dt);
                    e.ang = angOf(e.v);
                } break;
                case WEAVER: {
                    V2 dodge;
                    for (auto& b : bullets) {
                        V2 rel = e.p - b.p;
                        if (len2(rel) > 170 * 170) continue;
                        V2 bd = norm(b.v);
                        float along = dot(rel, bd);
                        if (along <= 0) continue;
                        V2 lat = rel - bd * along;
                        float ld = len(lat);
                        if (ld < 48) dodge += (ld > 0.5f ? lat / ld : perp(bd)) * (1 - ld / 48);
                    }
                    V2 des = toN * 210 + dodge * 900;
                    if (len(des) > 380) des = norm(des) * 380;
                    steer(e, des, 7, dt);
                    e.ang += e.spin * 0.6f * dt;
                } break;
                case SPLITTER: {
                    steer(e, toN * 125, 1.5f, dt);
                    e.ang += e.spin * 0.8f * dt;
                } break;
                case SHARD: {
                    float off = e.side * 1.15f * clamp01(dist / 450);
                    steer(e, rot(toN, off) * 300, 3.2f, dt);
                    e.ang = angOf(e.v);
                } break;
                case LANCER: {
                    if (e.state == 0) {
                        e.timer -= dt;
                        if (e.timer > 0.55f) e.ang = turnToward(e.ang, angOf(to), dt * 2.8f);
                        e.v *= std::exp(-dt * 4);
                        if (!e.charged && e.timer < 0.7f) {
                            e.charged = true;
                            audio.play(Sfx::Charge, 0.8f, pan(e.p.x));
                        }
                        if (e.timer <= 0) {
                            e.state = 1;
                            e.v = fromAng(e.ang) * 950;
                        }
                    } else {
                        V2 np = e.p + e.v * dt;
                        if (np.x < AX0 + e.r || np.x > AX1 - e.r || np.y < AY0 + e.r || np.y > AY1 - e.r) {
                            e.state = 0;
                            e.timer = frand(1.1f, 1.7f);
                            e.charged = false;
                            bounce(e);
                            for (int i = 0; i < 14; ++i)
                                emit(e.p, rot(-norm(e.v), frand(-1.3f, 1.3f)) * frand(100, 400), DEFS[LANCER].col, frand(0.2f, 0.5f));
                            grid.impulse(e.p, 500, 140);
                            e.v = {};
                        }
                    }
                } break;
                case SINGULARITY: {
                    e.v *= std::exp(-dt * 1.5f);
                    e.r = DEFS[SINGULARITY].r + e.fed * 1.6f;
                    grid.pull(e.p, 1700 + e.fed * 120, SING_RANGE, dt);
                    if (frand(0, 1) < 0.6f) {
                        float a = frand(0, TAU), rr = frand(90, 220);
                        emit(e.p + fromAng(a) * rr, perp(fromAng(a)) * 90, mix(DEFS[SINGULARITY].col, {0.4f, 0.8f, 1}, frand(0, 1)),
                             frand(0.6f, 1.1f), 1.5f);
                    }
                    // pull other things in
                    for (auto& o : enemies) {
                        if (&o == &e || !o.active() || o.type == SINGULARITY) continue;
                        V2 r = e.p - o.p;
                        float d = len(r);
                        if (d > SING_RANGE) continue;
                        if (d < e.r) {
                            o.dead = true;
                            ++e.fed;
                            burst(o.p, DEFS[o.type].col, 12, 40, 160, 0.2f, 0.5f);
                            audio.play(Sfx::Absorb, 0.7f, pan(e.p.x));
                            continue;
                        }
                        o.v += r / d * (520 * (1 - d / SING_RANGE) * dt);
                    }
                    for (auto& b : bullets) {
                        V2 r = e.p - b.p;
                        float d = len(r);
                        if (d < SING_RANGE && d > 1) b.v += r / d * (2400 * (1 - d / SING_RANGE) * dt);
                    }
                    if (alive) {
                        V2 r = e.p - pp;
                        float d = len(r);
                        if (d < SING_RANGE && d > 1) pp += r / d * (150 * (1 - d / SING_RANGE) * dt);
                    }
                    if (e.fed >= 10) burstSingularity(e);
                } break;
                default: break;
            }
            e.p += e.v * dt;
            if (e.type != LANCER || e.state == 0) bounce(e);
        }
        // soft separation so packs don't collapse into one shape
        for (size_t i = 0; i < enemies.size(); ++i) {
            Enemy& a = enemies[i];
            if (!a.active() || a.type == SINGULARITY) continue;
            for (size_t j = i + 1; j < enemies.size(); ++j) {
                Enemy& b = enemies[j];
                if (!b.active() || b.type == SINGULARITY) continue;
                V2 d = a.p - b.p;
                float rr = (a.r + b.r) * 0.8f, l2 = len2(d);
                if (l2 >= rr * rr || l2 < 1e-4f) continue;
                float l = std::sqrt(l2);
                V2 push = d / l * ((rr - l) * 0.5f);
                a.p += push;
                b.p -= push;
            }
        }
    }

    void collisions() {
        for (auto& b : bullets) {
            if (b.dead) continue;
            for (auto& e : enemies) {
                if (!e.active()) continue;
                float rr = e.r + 4;
                if (len2(e.p - b.p) > rr * rr) continue;
                b.dead = true;
                if (--e.hp <= 0) {
                    killEnemy(e, true);
                } else {
                    e.hitFlash = 0.07f;
                    burst(b.p, DEFS[e.type].col, 6, 60, 220, 0.15f, 0.35f);
                    audio.play(Sfx::Hit, 0.5f, pan(e.p.x), frand(0.9f, 1.2f));
                    if (e.type == SINGULARITY) {
                        grid.impulse(e.p, 300, 160);
                        e.v += norm(b.v) * 18;
                    }
                }
                break;
            }
        }
        if (alive && invT <= 0) {
            for (auto& e : enemies) {
                if (!e.active()) continue;
                float rr = e.r * 0.75f + 9;
                if (len2(e.p - pp) < rr * rr) {
                    playerDie();
                    break;
                }
            }
        }
    }

    void updateParticles(float dt) {
        float drag = std::exp(-dt * 2.6f);
        for (size_t i = 0; i < parts.size();) {
            Particle& p = parts[i];
            p.life -= dt;
            if (p.life <= 0) {
                parts[i] = parts.back();
                parts.pop_back();
                continue;
            }
            for (auto& e : enemies) {
                if (e.type != SINGULARITY || !e.active()) continue;
                V2 r = e.p - p.p;
                float d2 = len2(r);
                if (d2 < SING_RANGE * SING_RANGE && d2 > 1) {
                    float d = std::sqrt(d2);
                    p.v += r / d * (1600 * (1 - d / SING_RANGE) * dt);
                    if (d < 10) p.life = 0;
                }
            }
            p.v *= drag;
            p.p += p.v * dt;
            if (p.p.x < AX0) { p.p.x = AX0; p.v.x = std::fabs(p.v.x) * 0.6f; }
            if (p.p.x > AX1) { p.p.x = AX1; p.v.x = -std::fabs(p.v.x) * 0.6f; }
            if (p.p.y < AY0) { p.p.y = AY0; p.v.y = std::fabs(p.v.y) * 0.6f; }
            if (p.p.y > AY1) { p.p.y = AY1; p.v.y = -std::fabs(p.v.y) * 0.6f; }
            ++i;
        }
    }

    void step(float dt) {
        modeT += dt;
        clock += dt;
        shake *= std::exp(-dt * 6);
        flash = std::max(0.f, flash - dt * 1.6f);
        bannerT -= dt;
        if (bombRingT >= 0) {
            bombRingT += dt;
            if (bombRingT > 0.7f) bombRingT = -1;
        }
        if (mode == Mode::Paused) return;

        timeScale = std::min(1.f, timeScale + dt * 0.6f);
        float sdt = dt * timeScale;
        grid.update(sdt);
        updateParticles(sdt);
        for (auto& f : floaters) {
            f.t -= dt;
            f.p.y -= 30 * dt;
        }
        floaters.erase(std::remove_if(floaters.begin(), floaters.end(), [](const Floater& f) { return f.t <= 0; }),
                       floaters.end());

        if (mode != Mode::Play) {  // title / game over: ambient fireworks
            attractT -= dt;
            if (attractT <= 0) {
                attractT = frand(0.35f, 0.9f);
                V2 p{frand(AX0 + 80, AX1 - 80), frand(AY0 + 80, AY1 - 80)};
                burst(p, hsv(frand(0, 1), 0.7f, 1), 50, 60, 380, 0.5f, 1.2f);
                grid.impulse(p, 500, 180);
            }
            return;
        }

        if (opt.autoplay) autopilot();
        if (alive) gameT += sdt;
        updatePlayer(sdt);
        if (mode != Mode::Play) return;
        updateBullets(sdt);
        updateEnemies(sdt);
        collisions();
        if (alive) director(sdt);

        bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return b.dead; }), bullets.end());
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.dead; }), enemies.end());
        for (auto& e : pending) enemies.push_back(e);
        pending.clear();
    }

    // Test bot: orbit the arena, flee close threats, shoot the nearest enemy.
    void autopilot() {
        const Enemy* nearest = nullptr;
        float best = 1e9f;
        V2 flee;
        for (auto& e : enemies) {
            if (e.dead) continue;
            V2 r = pp - e.p;
            float d = len(r);
            if (d < best) { best = d; nearest = &e; }
            if (d < 160 && d > 0.1f) flee += r / d * (1 - d / 160) * (e.type == SINGULARITY ? 3.f : 1.f);
        }
        V2 c{W / 2.f, H / 2.f};
        V2 orbit = perp(norm(pp - c)) * 0.6f + (c - pp) / 700.f;
        inMove = norm(orbit + flee * 2.5f);
        inAim = nearest ? norm(nearest->p - pp) : fromAng(clock * 2);
        inFire = nearest != nullptr;
        if (best < 40 && bombs > 0 && invT <= 0) useBomb();
    }

    // ------------------------------------------------ input
    SDL_FRect letterbox() const {
        int ow = W, oh = H;
        SDL_GetRenderOutputSize(ren, &ow, &oh);
        float s = std::min(ow / float(W), oh / float(H));
        float w = W * s, h = H * s;
        return {(ow - w) / 2, (oh - h) / 2, w, h};
    }
    void openPad(SDL_JoystickID id) {
        if (pad || testMode()) return;
        pad = SDL_OpenGamepad(id);
        if (pad) SDL_Log("gamepad: %s", SDL_GetGamepadName(pad));
    }
    static V2 deadzone(float x, float y, float dz) {
        V2 v{x, y};
        float m = len(v);
        if (m < dz) return {};
        return v / m * std::min(1.f, (m - dz) / (1 - dz));
    }
    void pollInput() {
        if (opt.autoplay) return;
        inMove = {};
        inAim = {};
        inFire = false;
        if (pad) {
            auto ax = [&](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(pad, a) / 32767.f; };
            inMove = deadzone(ax(SDL_GAMEPAD_AXIS_LEFTX), ax(SDL_GAMEPAD_AXIS_LEFTY), 0.2f);
            V2 aim = deadzone(ax(SDL_GAMEPAD_AXIS_RIGHTX), ax(SDL_GAMEPAD_AXIS_RIGHTY), 0.25f);
            if (len(aim) > 0.3f) {
                inAim = aim;
                inFire = true;
                usingMouse = false;
            }
            if (len(inMove) > 0) usingMouse = false;
        }
        const bool* k = SDL_GetKeyboardState(nullptr);
        V2 km{float(k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A]), float(k[SDL_SCANCODE_S] - k[SDL_SCANCODE_W])};
        if (len(km) > 0) inMove = norm(km);
        V2 ka{float(k[SDL_SCANCODE_RIGHT] - k[SDL_SCANCODE_LEFT]), float(k[SDL_SCANCODE_DOWN] - k[SDL_SCANCODE_UP])};
        if (len(ka) > 0) {
            inAim = norm(ka);
            inFire = true;
        }
        float mx, my;
        SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
        int ww, wh, ow, oh;
        SDL_GetWindowSize(win, &ww, &wh);
        SDL_GetRenderOutputSize(ren, &ow, &oh);
        SDL_FRect lb = letterbox();
        float px = mx * ow / float(std::max(1, ww)), py = my * oh / float(std::max(1, wh));
        mouseGame = {(px - lb.x) * W / lb.w, (py - lb.y) * H / lb.h};
        if (mb & SDL_BUTTON_LMASK) {
            usingMouse = true;
            inAim = norm(mouseGame - pp);
            inFire = true;
        }
    }
    void confirm() {
        if (mode == Mode::Title) startGame();
        else if (mode == Mode::GameOver && modeT > 1.2f) startGame();
        else if (mode == Mode::Paused) { mode = Mode::Play; SDL_HideCursor(); }
    }
    void pauseToggle() {
        if (mode == Mode::Play) { mode = Mode::Paused; SDL_ShowCursor(); }
        else if (mode == Mode::Paused) { mode = Mode::Play; SDL_HideCursor(); }
        else confirm();
    }
    void back() {
        if (mode == Mode::Title) running = false;
        else if (mode == Mode::Paused || mode == Mode::GameOver) toTitle();
    }
    void handle(const SDL_Event& ev) {
        switch (ev.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_GAMEPAD_ADDED: openPad(ev.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (pad && SDL_GetGamepadID(pad) == ev.gdevice.which) {
                    SDL_CloseGamepad(pad);
                    pad = nullptr;
                    if (mode == Mode::Play) pauseToggle();
                    int n = 0;
                    if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
                        if (n > 0) openPad(ids[0]);
                        SDL_free(ids);
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (opt.autoplay) break;
                switch (ev.gbutton.button) {
                    case SDL_GAMEPAD_BUTTON_SOUTH: confirm(); break;
                    case SDL_GAMEPAD_BUTTON_START: pauseToggle(); break;
                    case SDL_GAMEPAD_BUTTON_BACK:
                    case SDL_GAMEPAD_BUTTON_EAST: back(); break;
                    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
                    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: useBomb(); break;
                    case SDL_GAMEPAD_BUTTON_NORTH: audio.toggleMusic(); break;
                    default: break;
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                int which = ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 0 : ev.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? 1 : -1;
                if (which < 0) break;
                if (ev.gaxis.value > 16000 && !trigHeld[which]) {
                    trigHeld[which] = true;
                    if (!opt.autoplay) useBomb();
                } else if (ev.gaxis.value < 8000) {
                    trigHeld[which] = false;
                }
            } break;
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.repeat) break;
                switch (ev.key.key) {
                    case SDLK_ESCAPE:
                        if (mode == Mode::Title || mode == Mode::GameOver) back();
                        else pauseToggle();
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (ev.key.mod & SDL_KMOD_ALT) toggleFullscreen();
                        else confirm();
                        break;
                    case SDLK_SPACE:
                        if (mode == Mode::Play) useBomb();
                        else confirm();
                        break;
                    case SDLK_Q:
                        if (mode == Mode::Paused) toTitle();
                        break;
                    case SDLK_M: audio.toggleMusic(); break;
                    case SDLK_F11: toggleFullscreen(); break;
                    default: break;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button == SDL_BUTTON_RIGHT) useBomb();
                break;
            case SDL_EVENT_MOUSE_MOTION: usingMouse = true; break;
            default: break;
        }
    }
    void toggleFullscreen() {
        bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN;
        SDL_SetWindowFullscreen(win, !fs);
    }

    // ------------------------------------------------ rendering
    void drawEnemy(const Enemy& e) {
        const EnemyDef& d = DEFS[e.type];
        Col c = d.col;
        float sc = 1;
        if (e.spawnT > 0) {
            float k = e.spawnT / SPAWN_TIME;
            sc = 1 + 2.2f * k;
            c = withA(c, (1 - k) * (0.6f + 0.4f * std::sin(e.t * 50)));
        }
        if (e.hitFlash > 0) c = mix(c, WHITE, 0.7f);
        switch (e.type) {
            case DRIFTER:
                dr.poly(SH_DRIFTER, e.p, e.ang, sc, 2.4f, c);
                dr.poly(SH_DRIFTER_IN, e.p, -e.ang, sc, 1.6f, c * 0.7f);
                break;
            case SEEKER:
                dr.poly(SH_SEEKER, e.p, e.ang, sc, 2.4f, c);
                dr.line(e.p + rot({-12, 0}, e.ang) * sc, e.p + rot({16, 0}, e.ang) * sc, 1.4f, c * 0.6f);
                break;
            case WEAVER:
                dr.poly(SH_WEAVER, e.p, e.ang, sc, 2.4f, c);
                dr.poly(SH_WEAVER_IN, e.p, -e.ang * 1.7f, sc, 1.6f, c * 0.8f);
                break;
            case SPLITTER:
                dr.poly(SH_SPLITTER, e.p, e.ang, sc, 2.4f, c);
                dr.poly(ngon(5, 5, e.ang), e.p, 0, sc, 1.4f, c * 0.7f);
                break;
            case SHARD: dr.poly(SH_SHARD, e.p, e.ang, sc, 2.f, c); break;
            case LANCER: {
                dr.poly(SH_LANCER, e.p, e.ang, sc, 2.4f, c);
                if (e.spawnT <= 0 && e.state == 0 && e.timer < 0.9f) {
                    V2 dir = fromAng(e.ang);
                    float tx = dir.x > 0 ? (AX1 - e.p.x) / dir.x : dir.x < 0 ? (AX0 - e.p.x) / dir.x : 1e9f;
                    float ty = dir.y > 0 ? (AY1 - e.p.y) / dir.y : dir.y < 0 ? (AY0 - e.p.y) / dir.y : 1e9f;
                    float L = std::min(tx, ty);
                    float k = 1 - e.timer / 0.9f;
                    for (float s = 26; s < L - 6; s += 26)
                        dr.line(e.p + dir * s, e.p + dir * std::min(L, s + 12), 1.3f, c * (0.15f + 0.5f * k));
                }
            } break;
            case SINGULARITY: {
                float pulse = 1 + 0.07f * std::sin(e.t * 6 + e.fed);
                float R = e.r * pulse * sc;
                Col ring = e.hitFlash > 0 ? WHITE : c;
                dr.circle(e.p, R, 2.6f, ring, 36);
                dr.circle(e.p, R * 0.68f, 1.8f, mix(ring, {0.4f, 0.8f, 1}, 0.5f), 28);
                dr.poly(ngon(3, R * 0.45f), e.p, e.t * 2.5f, 1, 1.8f, mix(ring, WHITE, 0.3f));
                dr.poly(ngon(3, R * 0.45f), e.p, -e.t * 1.7f + PI, 1, 1.2f, ring * 0.6f);
                if (e.spawnT <= 0) dr.circle(e.p, SING_RANGE * (0.96f + 0.04f * std::sin(e.t * 2)), 1.f, c * 0.07f, 64);
            } break;
            default: break;
        }
    }

    void drawPrism(V2 c, float s, float t) {
        // rotating 3D triangular prism, the game's emblem
        V2 tri[3] = {{0, -1}, {0.866f, 0.5f}, {-0.866f, 0.5f}};
        float ay = t * 0.9f, ax = 0.35f + 0.2f * std::sin(t * 0.6f);
        auto proj = [&](float x, float y, float z) {
            float x1 = x * std::cos(ay) + z * std::sin(ay), z1 = -x * std::sin(ay) + z * std::cos(ay);
            float y1 = y * std::cos(ax) - z1 * std::sin(ax), z2 = y * std::sin(ax) + z1 * std::cos(ax);
            float k = 3.2f / (3.2f + z2);
            return V2{c.x + x1 * s * k, c.y + y1 * s * k};
        };
        V2 a[3], b[3];
        for (int i = 0; i < 3; ++i) {
            a[i] = proj(tri[i].y * 0.9f, tri[i].x * 0.9f - 0.0f, -1.1f);
            b[i] = proj(tri[i].y * 0.9f, tri[i].x * 0.9f, 1.1f);
        }
        for (int i = 0; i < 3; ++i) {
            Col ci = hsv(t * 0.08f + i / 3.f, 0.6f, 1);
            dr.line(a[i], a[(i + 1) % 3], 2.4f, ci);
            dr.line(b[i], b[(i + 1) % 3], 2.4f, ci);
            dr.line(a[i], b[i], 2.f, ci * 0.8f);
        }
    }

    void drawHUD() {
        drawText(dr, commas(score), {40, 36}, 26, {0.85f, 0.95f, 1}, 0);
        drawText(dr, "X" + std::to_string(mult), {40, 72}, 18, {0.4f, 0.9f, 1}, 0);
        drawText(dr, "HI " + commas(std::max(hiscore, score)), {W / 2.f, 38}, 15, {0.5f, 0.6f, 0.9f}, 0.5f);
        auto icons = [&](int n, float y, Col c, bool ship) {
            int shown = n > 5 ? 1 : n;
            for (int i = 0; i < shown; ++i) {
                V2 p{W - 52.f - i * 30, y};
                if (ship) dr.poly(SH_PLAYER, p, -PI / 2, 0.75f, 1.7f, c);
                else dr.circle(p, 8, 1.7f, c, 20);
            }
            if (n > 5) drawText(dr, std::to_string(n) + "X", {W - 72.f, y - 8}, 16, c, 1);
        };
        icons(lives, 50, {0.85f, 0.95f, 1}, true);
        icons(bombs, 84, {1, 0.75f, 0.3f}, false);
        if (bannerT > 0) {
            float a = clamp01(bannerT / 0.4f) * clamp01((2.2f - bannerT) / 0.15f);
            drawText(dr, banner, {W / 2.f, 150}, 30, withA(bannerC, a), 0.5f);
        }
    }

    void drawWorld() {
        float t = clock;
        grid.draw(dr, t);
        Col border = mix({0.3f, 0.5f, 1}, {0.7f, 0.9f, 1}, 0.5f + 0.5f * std::sin(t * 1.3f));
        V2 c0{AX0, AY0}, c1{AX1, AY0}, c2{AX1, AY1}, c3{AX0, AY1};
        dr.line(c0, c1, 2.4f, border);
        dr.line(c1, c2, 2.4f, border);
        dr.line(c2, c3, 2.4f, border);
        dr.line(c3, c0, 2.4f, border);

        for (auto& p : parts) {
            float k = p.life / p.maxLife;
            V2 tail = p.v * 0.028f;
            if (len2(tail) < 4) tail = norm(p.v) * 2;
            dr.line(p.p - tail, p.p, p.w * (0.5f + 0.5f * k), p.c * k);
        }
        for (auto& b : bullets) dr.line(b.p - b.v * 0.013f, b.p, 2.4f, {1, 0.92f, 0.55f});
        for (auto& e : enemies)
            if (!e.dead) drawEnemy(e);

        if ((mode == Mode::Play || mode == Mode::Paused) && alive) {
            bool blink = invT > 0 && std::fmod(clock, 0.16f) < 0.08f;
            if (!blink) {
                dr.poly(SH_PLAYER, pp, pang, 1, 2.6f, {0.9f, 0.97f, 1});
                dr.poly(SH_PLAYER, pp, pang, 0.45f, 1.4f, {0.4f, 0.8f, 1});
            }
            if (usingMouse && !opt.autoplay && mode == Mode::Play) {
                V2 m = mouseGame;
                dr.circle(m, 9, 1.4f, {0.6f, 0.9f, 1, 0.8f}, 16);
                dr.line(m - V2{14, 0}, m - V2{6, 0}, 1.2f, {0.6f, 0.9f, 1, 0.8f});
                dr.line(m + V2{14, 0}, m + V2{6, 0}, 1.2f, {0.6f, 0.9f, 1, 0.8f});
            }
        }
        if (bombRingT >= 0) {
            float k = bombRingT / 0.7f;
            dr.circle(bombRingP, 30 + 1500 * k, 6 * (1 - k) + 1, Col{0.6f, 0.9f, 1} * (1 - k), 96);
        }
        for (auto& f : floaters) drawText(dr, f.s, f.p, 16, withA(f.c, clamp01(f.t)), 0.5f);
    }

    void drawOverlay() {
        float t = clock;
        if (mode == Mode::Title) {
            drawPrism({W / 2.f, 168}, 70, t);
            const std::string title = "PRISM";
            float size = 104, x = W / 2.f - textWidth(title, size) / 2;
            for (size_t i = 0; i < title.size(); ++i) {
                drawText(dr, std::string(1, title[i]), {x, 270}, size, hsv(t * 0.07f + i * 0.12f, 0.55f, 1), 0, 4.5f);
                x += 5.5f * size / 6;
            }
            drawText(dr, "A NEON ARENA SHOOTER", {W / 2.f, 398}, 16, {0.5f, 0.6f, 0.9f}, 0.5f);
            float a = 0.55f + 0.45f * std::sin(t * 4);
            drawText(dr, pad ? "PRESS A TO START" : "PRESS ENTER TO START", {W / 2.f, 460}, 24, withA({1, 1, 1}, a), 0.5f);
            drawText(dr, "HIGH SCORE  " + commas(hiscore), {W / 2.f, 508}, 16, {1, 0.8f, 0.35f}, 0.5f);
            Col dim{0.45f, 0.55f, 0.8f};
            drawText(dr, "LEFT STICK MOVE    RIGHT STICK FIRE    BUMPERS/TRIGGERS BOMB    START PAUSE    Y MUSIC",
                     {W / 2.f, 584}, 11, dim, 0.5f);
            drawText(dr, "KEYS: WASD MOVE    MOUSE/ARROWS FIRE    SPACE/RMB BOMB    ESC PAUSE    M MUSIC    F11 FULLSCREEN",
                     {W / 2.f, 608}, 11, dim, 0.5f);
            std::string padName = pad ? SDL_GetGamepadName(pad) : "";
            drawText(dr, pad ? "CONTROLLER: " + padName : "NO CONTROLLER FOUND - KEYBOARD AND MOUSE", {W / 2.f, 650}, 11,
                     pad ? Col{0.4f, 1, 0.5f} : Col{1, 0.5f, 0.4f}, 0.5f);
        } else if (mode == Mode::Paused) {
            dr.solid({0, 0}, {W, 0}, {W, H}, {0, H}, {0.02f, 0.03f, 0.08f, 1});
            drawText(dr, "PAUSED", {W / 2.f, 280}, 60, {0.8f, 0.9f, 1}, 0.5f, 3.5f);
            drawText(dr, pad ? "A RESUME    BACK QUIT TO TITLE" : "ESC RESUME    Q QUIT TO TITLE", {W / 2.f, 380}, 18,
                     {0.5f, 0.6f, 0.9f}, 0.5f);
        } else if (mode == Mode::GameOver) {
            drawText(dr, "GAME OVER", {W / 2.f, 230}, 72, {1, 0.35f, 0.45f}, 0.5f, 4.f);
            drawText(dr, commas(score), {W / 2.f, 350}, 40, {0.9f, 0.95f, 1}, 0.5f, 2.6f);
            if (newHigh)
                drawText(dr, "NEW HIGH SCORE!", {W / 2.f, 412}, 22, hsv(t * 0.5f, 0.6f, 1), 0.5f);
            else
                drawText(dr, "HIGH SCORE  " + commas(hiscore), {W / 2.f, 412}, 18, {1, 0.8f, 0.35f}, 0.5f);
            if (modeT > 1.2f)
                drawText(dr, pad ? "A PLAY AGAIN    BACK TITLE" : "ENTER PLAY AGAIN    ESC TITLE", {W / 2.f, 490}, 20,
                         withA({1, 1, 1}, 0.6f + 0.4f * std::sin(t * 4)), 0.5f);
        }
    }

    void render() {
        dr.off = shake > 0.3f ? V2{frand(-shake, shake), frand(-shake, shake)} : V2{};
        SDL_SetRenderTarget(ren, scene);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        drawWorld();
        if (mode != Mode::Paused) {
            dr.off = {};
            if (mode == Mode::Play) drawHUD();
            drawOverlay();
        }
        if (flash > 0) dr.solid({0, 0}, {W, 0}, {W, H}, {0, H}, withA({0.8f, 0.9f, 1}, flash * 0.35f));
        dr.flush();
        if (mode == Mode::Paused) {  // dim world (overlay drawn crisp after bloom)
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 170);
            SDL_RenderFillRect(ren, nullptr);
            dr.off = {};
            drawOverlay();
            dr.flush();
        }

        // bloom: halve down the chain, then add each level back up
        SDL_SetTextureBlendMode(scene, SDL_BLENDMODE_NONE);
        SDL_Texture* src = scene;
        for (int i = 0; i < 4; ++i) {
            SDL_SetRenderTarget(ren, bloom[i]);
            SDL_SetTextureBlendMode(src, SDL_BLENDMODE_NONE);
            SDL_SetTextureColorMod(src, 255, 255, 255);
            SDL_RenderTexture(ren, src, nullptr, nullptr);
            src = bloom[i];
        }
        for (int i = 3; i > 0; --i) {
            SDL_SetRenderTarget(ren, bloom[i - 1]);
            SDL_SetTextureBlendMode(bloom[i], SDL_BLENDMODE_ADD);
            SDL_SetTextureColorMod(bloom[i], 200, 200, 200);
            SDL_RenderTexture(ren, bloom[i], nullptr, nullptr);
        }

        SDL_SetRenderTarget(ren, nullptr);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_FRect dst = letterbox();
        SDL_RenderTexture(ren, scene, nullptr, &dst);
        SDL_SetTextureBlendMode(bloom[0], SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(bloom[0], 190, 190, 190);
        SDL_RenderTexture(ren, bloom[0], nullptr, &dst);

        if (!opt.shot.empty() && clock >= opt.shotAt) {
            if (SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr)) {
                SDL_SaveBMP(s, opt.shot.c_str());
                SDL_DestroySurface(s);
                SDL_Log("saved %s (enemies=%zu parts=%zu score=%lld)", opt.shot.c_str(), enemies.size(), parts.size(), score);
            }
            running = false;
        }
        SDL_RenderPresent(ren);
    }

    // ------------------------------------------------ main loop
    void run() {
        int n = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
            if (n > 0) openPad(ids[0]);
            SDL_free(ids);
        }
        if (opt.autoplay) startGame();
        Uint64 last = SDL_GetPerformanceCounter();
        double acc = 0;
        while (running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) handle(ev);
            Uint64 now = SDL_GetPerformanceCounter();
            double dt = double(now - last) / SDL_GetPerformanceFrequency();
            last = now;
            acc += std::min(dt, 0.1);
            pollInput();
            while (acc >= STEP) {
                step(STEP);
                acc -= STEP;
            }
            if (opt.autoplay && mode == Mode::GameOver && modeT > 2) startGame();
            render();
        }
        if (score > hiscore) hiscore = score;
        saveHigh();
    }
};

int main(int argc, char** argv) {
    Game g;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--fullscreen" || a == "-f") g.opt.fullscreen = true;
        else if (a == "--no-rumble") g.opt.rumble = false;
        else if (a == "--autoplay") g.opt.autoplay = true;
        else if (a == "--shot" && i + 1 < argc) g.opt.shot = argv[++i];
        else if (a == "--shot-at" && i + 1 < argc) g.opt.shotAt = float(std::atof(argv[++i]));
        else {
            std::printf("usage: %s [--fullscreen] [--no-rumble] [--autoplay] [--shot FILE.bmp --shot-at SEC]\n", argv[0]);
            return a == "--help" || a == "-h" ? 0 : 1;
        }
    }
    if (!g.init()) return 1;
    g.run();
    g.shutdown();
    return 0;
}
