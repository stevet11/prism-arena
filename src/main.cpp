// PRISM ARENA — a neon twin-stick arena shooter. C++17 + SDL3.
// A port of the Python/pygame prism-arena, plus music and stereo sound.
//
// Gamepad: left stick move, right stick aim & fire, RB/LB/RT/LT bomb,
//          Start pause/start, A start/continue, Back quit to title (from pause) / quit (from title),
//          Y toggle music.
// Keyboard/mouse: WASD move, mouse (hold LMB) or arrow keys to aim & fire,
//          Space/RMB bomb, Esc pause, Enter start, M music, F11 fullscreen, F3 fps.
//
// Options: --fullscreen / -f  --no-rumble (or PRISM_NO_RUMBLE=1)
// Testing: --autoplay (bot plays)  --shot FILE.bmp --shot-at SECONDS (save frame & exit)
//          Test modes never open the gamepad, so they never rumble it.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    void arc(V2 p, float r, float a0, float a1, float w, Col c, int seg = 12) {
        V2 prev = p + fromAng(a0) * r;
        for (int i = 1; i <= seg; ++i) {
            V2 q = p + fromAng(a0 + (a1 - a0) * i / seg) * r;
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

// ---------------------------------------------------------------- shapes & colours
std::vector<V2> ngon(int n, float r, float ph = 0) {
    std::vector<V2> v;
    for (int i = 0; i < n; ++i) v.push_back(fromAng(ph + TAU * i / n) * r);
    return v;
}
std::vector<V2> star(int n, float r1, float r2, float ph = 0) {
    std::vector<V2> v;
    for (int i = 0; i < n * 2; ++i) v.push_back(fromAng(ph + i * TAU / (n * 2)) * (i % 2 == 0 ? r1 : r2));
    return v;
}
inline Col rgb(int r, int g, int b) { return {r / 255.f, g / 255.f, b / 255.f, 1}; }
inline Col hsvDeg(float h, float s, float v) { return hsv(h / 360.f, s, v); }
inline float deg(float d) { return d * PI / 180.f; }

const std::vector<V2> SH_PLAYER = {{19, 0}, {-7, -13}, {-14, -7}, {-5, 0}, {-14, 7}, {-7, 13}};
const std::vector<V2> SH_PLAYER_IN = {{9, 0}, {-3, -5}, {-3, 5}};
constexpr float PLAYER_R = 11, PLAYER_SPEED = 430, BULLET_SPEED = 980;

// ---------------------------------------------------------------- grid
// Spring-mass grid: every node is a displacement from its rest position,
// pulled back home and coupled to its neighbours, so pushes ripple outward.
struct Grid {
    int nx = 39, ny = 22;  // ~32 px spacing
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
        for (int j = 1; j < ny - 1; ++j)
            for (int i = 1; i < nx - 1; ++i) {
                int k = j * nx + i;
                V2 lap = d[k - 1] + d[k + 1] + d[k - nx] + d[k + nx] - d[k] * 4;
                acc[k] = lap * 240 - d[k] * 14 - v[k] * 3.2f;
            }
        for (int j = 1; j < ny - 1; ++j)
            for (int i = 1; i < nx - 1; ++i) {
                int k = j * nx + i;
                v[k] += acc[k] * dt;
                d[k] += v[k] * dt;
                v[k].x = std::clamp(v[k].x, -2500.f, 2500.f);
                v[k].y = std::clamp(v[k].y, -2500.f, 2500.f);
            }
    }
    // f(node, unit vector from c to node, 1 - dist/R)
    template <class F>
    void around(V2 c, float R, F f) {
        int i0 = std::max(1, int((c.x - R - AX0) / sx)), i1 = std::min(nx - 2, int((c.x + R - AX0) / sx) + 1);
        int j0 = std::max(1, int((c.y - R - AY0) / sy)), j1 = std::min(ny - 2, int((c.y + R - AY0) / sy) + 1);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                int k = j * nx + i;
                V2 r = rest(i, j) + d[k] - c;
                float dist = len(r) + 1;
                if (dist < R) f(k, r / dist, 1 - dist / R);
            }
    }
    // positive strength pushes points away, negative pulls them in (velocity kick)
    void impulse(V2 c, float s, float R) {
        around(c, R, [&](int k, V2 dir, float f) { v[k] += dir * (s * f * f); });
    }
    // bullets drag the grid along with them
    void pushBullet(V2 p, V2 bv, float dt) {
        around(p, 40, [&](int k, V2, float f) { v[k] += bv * (f * (2 - f) * 0.9f * dt); });
    }
    void draw(Draw& dr, float t) {
        float hue = 225 + 15 * std::sin(t * 0.2f);
        Col minor = hsvDeg(hue, 0.8f, 0.22f), major = hsvDeg(hue, 0.7f, 0.42f);
        auto P = [&](int i, int j) { return rest(i, j) + d[j * nx + i]; };
        auto lineCol = [&](bool isMajor, float hot) {
            Col c = isMajor ? major : minor;
            return hot > 6 ? c * (1 + std::min(2.f, hot / 25)) : c;
        };
        for (int j = 0; j < ny; ++j) {
            float hot = 0;
            for (int i = 0; i < nx; ++i) hot = std::max(hot, len(d[j * nx + i]));
            Col c = lineCol(j % 4 == 0, hot);
            for (int i = 0; i + 1 < nx; ++i) dr.line(P(i, j), P(i + 1, j), j % 4 == 0 ? 1.2f : 0.8f, c);
        }
        for (int i = 0; i < nx; ++i) {
            float hot = 0;
            for (int j = 0; j < ny; ++j) hot = std::max(hot, len(d[j * nx + i]));
            Col c = lineCol(i % 4 == 0, hot);
            for (int j = 0; j + 1 < ny; ++j) dr.line(P(i, j), P(i, j + 1), i % 4 == 0 ? 1.2f : 0.8f, c);
        }
    }
};

// ---------------------------------------------------------------- entities
enum EType { DRIFTER, SEEKER, WEAVER, SPLITTER, SHARD, LANCER, SINGULARITY, NUM_TYPES };
struct EnemyDef {
    Col col;
    float r;
    int hp, score;
    float spawnTime;
    std::vector<std::vector<V2>> shapes;
};
const EnemyDef DEFS[NUM_TYPES] = {
    {rgb(60, 200, 255), 15, 1, 25, 0.7f, {ngon(6, 16), ngon(3, 7)}},                                   // drifter
    {rgb(255, 150, 40), 13, 1, 50, 0.7f, {{{15, 0}, {-10, -12}, {-10, 12}}, {{4, 0}, {-5, -5}, {-5, 5}}}},  // seeker
    {rgb(130, 255, 70), 14, 1, 100, 0.7f, {{{-13, -13}, {13, 13}, {13, -13}, {-13, 13}}}},              // weaver
    {rgb(255, 60, 220), 18, 2, 100, 0.7f, {ngon(5, 19), ngon(5, 8, PI)}},                               // splitter
    {rgb(255, 130, 245), 9, 1, 30, 0.0f, {{{10, 0}, {-6, -7}, {-6, 7}}}},                               // shard
    {rgb(255, 235, 60), 15, 1, 150, 0.7f, {star(4, 19, 6)}},                                            // lancer
    {rgb(255, 50, 70), 22, 16, 500, 1.0f, {}},                                                          // singularity
};
enum LancerState { L_DRIFT, L_AIM, L_DASH };

struct Enemy {
    EType type = DRIFTER;
    V2 p, v, aim{1, 0};
    float ang = 0, spawn = 0, flash = 0, t = 0, speed = 1, spin = 0, timer = 0, age = 0;
    int hp = 1, dir = 1, state = L_DRIFT;
    bool dead = false;
    bool spawning() const { return spawn > 0; }
    float r() const { return DEFS[type].r; }
};
struct Bullet {
    V2 p, v;
    bool dead = false;
};
struct Particle {
    V2 p, v;
    Col c;
    float life, maxLife;
};
struct Floater {
    V2 p;
    std::string s;
    Col c;
    float life;
};
struct Banner {
    std::string s;
    Col c;
    float life;
};
struct Wave {
    V2 c;
    float r;
};

enum class Mode { Title, Play, Paused, GameOver };

// ---------------------------------------------------------------- game
struct Options {
    bool fullscreen = false, rumble = true, autoplay = false;
    std::string shot;
    float shotAt = 3.f;
};

struct Game {
    static constexpr int MULT_STEPS[9] = {10, 25, 50, 100, 175, 275, 400, 600, 850};
    static constexpr long long WEAPON_STEPS[3] = {25000, 90000, 250000};

    Options opt;
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture *scene = nullptr, *frame = nullptr;
    SDL_Texture* bloom[4] = {};
    Draw dr;
    Audio audio;
    Grid grid;
    SDL_Gamepad* pad = nullptr;
    bool running = true;
    std::string prefPath;

    Mode mode = Mode::Title;
    float clock = 0, titleTimer = 0, shake = 0;
    int sfxUsed[(int)Sfx::Count] = {};
    bool showFps = false;
    float fps = 0, fpsT = 0;
    int fpsFrames = 0;

    // player
    V2 pp, pv;
    float pang = 0, invuln = 0, cooldown = 0, respawn = 0;
    bool alive = true;

    long long score = 0, hiscore = 0, nextLife = 75000, nextBomb = 100000;
    int lives = 3, bombs = 3, streak = 0, weapon = 0;
    float gameT = 0, spawnTimer = 0, eventTimer = 0, gameoverTimer = 0;
    bool newHigh = false;

    std::vector<Enemy> enemies, pending;
    std::vector<Bullet> bullets;
    std::vector<Particle> parts;
    size_t partCursor = 0;
    std::vector<Floater> floaters;
    std::vector<Banner> banners;
    std::vector<Wave> waves;

    // input
    V2 inMove, inAim;
    bool trigHeld = false;

    // ------------------------------------------------ setup
    bool init() {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return false;
        }
        if (!SDL_CreateWindowAndRenderer("Prism Arena", W, H, SDL_WINDOW_RESIZABLE, &win, &ren)) {
            SDL_Log("window failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetWindowMinimumSize(win, 640, 360);
        SDL_SetRenderVSync(ren, 1);
        if (opt.fullscreen) SDL_SetWindowFullscreen(win, true);

        scene = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, W, H);
        frame = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, W, H);
        for (int i = 0; i < 4; ++i) {
            bloom[i] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, W >> (i + 1), H >> (i + 1));
            SDL_SetTextureScaleMode(bloom[i], SDL_SCALEMODE_LINEAR);
        }
        if (!scene || !frame || !bloom[3]) {
            SDL_Log("render targets unsupported: %s", SDL_GetError());
            return false;
        }
        SDL_SetTextureScaleMode(scene, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureScaleMode(frame, SDL_SCALEMODE_LINEAR);
        dr.init(ren);
        grid.init();
        audio.init();
        audio.setMusicLevel(0.4f);

        if (char* p = SDL_GetPrefPath("prism", "prism")) {
            prefPath = p;
            SDL_free(p);
        }
        loadHigh();
        reset();
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
    // play a sound at most `limit` times per simulation step, panned by x
    void sfx(Sfx s, int limit, float x = W / 2.f) {
        if (sfxUsed[(int)s] >= limit) return;
        ++sfxUsed[(int)s];
        audio.play(s, 1.f, (x - W / 2.f) / (W / 2.f) * 0.6f);
    }

    // ------------------------------------------------ particles
    // n particles from p; angle < -100 means all directions, otherwise a cone of `spread`
    void emit(V2 p, int n, Col c, float s0 = 80, float s1 = 480, float l0 = 0.5f, float l1 = 1.3f,
              float angle = -1000, float spread = TAU, float var = 45) {
        const size_t CAP = 4000;
        float vr = var / 255.f;
        for (int i = 0; i < n; ++i) {
            float a = angle < -100 ? frand(0, TAU) : angle + (frand(0, 1) - 0.5f) * spread;
            float lf = frand(l0, l1);
            Col cc{clamp01(c.r + frand(-vr, vr)), clamp01(c.g + frand(-vr, vr)), clamp01(c.b + frand(-vr, vr)), 1};
            Particle np{p, fromAng(a) * frand(s0, s1), cc, lf, lf};
            if (parts.size() < CAP) parts.push_back(np);
            else parts[partCursor++ % CAP] = np;
        }
    }
    void updateParticles(float dt, const std::vector<V2>& attractors) {
        float drag = std::exp(-2.4f * dt);
        for (size_t i = 0; i < parts.size();) {
            Particle& p = parts[i];
            p.v *= drag;
            for (V2 a : attractors) {
                V2 d = a - p.p;
                if (len2(d) < 260 * 260) {
                    float dist = len(d) + 8;
                    p.v += d / dist * (900 / dist * dt * 60);
                }
            }
            p.p += p.v * dt;
            if (p.p.x < AX0) { p.p.x = AX0; p.v.x *= -0.5f; }
            if (p.p.x > AX1) { p.p.x = AX1; p.v.x *= -0.5f; }
            if (p.p.y < AY0) { p.p.y = AY0; p.v.y *= -0.5f; }
            if (p.p.y > AY1) { p.p.y = AY1; p.v.y *= -0.5f; }
            p.life -= dt;
            if (p.life <= 0) {
                parts[i] = parts.back();
                parts.pop_back();
                continue;
            }
            ++i;
        }
    }

    // ------------------------------------------------ game flow
    void reset() {
        pp = {W / 2.f, H / 2.f};
        pv = {};
        pang = -PI / 2;
        alive = true;
        invuln = 2;
        cooldown = 0;
        respawn = 0;
        bullets.clear();
        enemies.clear();
        pending.clear();
        floaters.clear();
        banners.clear();
        waves.clear();
        score = 0;
        lives = 3;
        bombs = 3;
        streak = 0;
        gameT = 0;
        spawnTimer = 1.5f;
        eventTimer = 18;
        nextLife = 75000;
        nextBomb = 100000;
        weapon = 0;
        gameoverTimer = 0;
        newHigh = false;
        parts.clear();
    }
    void startGame() {
        reset();
        mode = Mode::Play;
        grid.impulse({W / 2.f, H / 2.f}, 1500, 600);
        sfx(Sfx::Bomb, 1);
        rumble(0.3f, 0.5f, 200);
        audio.setMusicLevel(1.f);
    }
    void toTitle() {
        mode = Mode::Title;
        enemies.clear();
        pending.clear();
        bullets.clear();
        waves.clear();
        audio.setMusicLevel(0.4f);
    }
    void confirm() {
        if (mode == Mode::Title) startGame();
        else if (mode == Mode::Paused) mode = Mode::Play;
        else if (mode == Mode::GameOver && gameoverTimer <= -1) toTitle();
    }

    // ------------------------------------------------ scoring
    int multiplier() const {
        int m = 1;
        for (int s : MULT_STEPS) m += streak >= s;
        return m;
    }
    void banner(const std::string& s, Col c) { banners.push_back({s, c, 2.f}); }
    void addScore(long long pts) {
        score += pts;
        while (score >= nextLife) {
            nextLife += 75000;
            ++lives;
            banner("EXTRA LIFE", rgb(120, 255, 160));
            sfx(Sfx::Bonus, 1);
        }
        while (score >= nextBomb) {
            nextBomb += 100000;
            ++bombs;
            banner("+1 BOMB", rgb(255, 200, 90));
            sfx(Sfx::Bonus, 1);
        }
        while (weapon < 3 && score >= WEAPON_STEPS[weapon]) {
            ++weapon;
            banner("WEAPON UPGRADE", rgb(120, 220, 255));
            sfx(Sfx::Bonus, 1);
        }
    }

    // ------------------------------------------------ events of the world
    Enemy makeEnemy(EType t, V2 p) {
        const EnemyDef& d = DEFS[t];
        Enemy e;
        e.type = t;
        e.p = p;
        e.ang = frand(0, TAU);
        e.spawn = d.spawnTime;
        e.hp = d.hp;
        e.t = frand(0, 10);
        e.speed = 1 + std::min(0.6f, gameT / 300);
        switch (t) {
            case DRIFTER:
                e.v = fromAng(frand(0, TAU)) * (frand(70, 110) * e.speed);
                e.spin = (irand(0, 1) ? 1 : -1) * frand(1, 2);
                break;
            case SHARD: e.dir = irand(0, 1) ? 1 : -1; break;
            case LANCER: e.timer = frand(1, 2.5f); break;
            case SINGULARITY: e.v = fromAng(frand(0, TAU)) * 25; break;
            default: break;
        }
        if (e.spawn > 0) sfx(Sfx::Spawn, 1, p.x);
        return e;
    }
    void spawn(EType t, V2 p) {
        if (enemies.size() + pending.size() < 200) pending.push_back(makeEnemy(t, p));
    }
    void killEnemy(Enemy& e, bool scored = true, bool chain = true) {
        if (e.dead) return;
        e.dead = true;
        const EnemyDef& d = DEFS[e.type];
        if (scored) {
            int prev = multiplier();
            ++streak;
            long long pts = (long long)d.score * multiplier();
            addScore(pts);
            if (floaters.size() < 40) floaters.push_back({e.p, std::to_string(pts), d.col, 0.8f});
            if (multiplier() > prev) banner("MULTIPLIER X" + std::to_string(multiplier()), WHITE);
        }
        int n = d.r > 12 ? 70 : 35;
        emit(e.p, n, d.col);
        emit(e.p, n / 4, WHITE, 40, 200, 0.2f, 0.5f);
        grid.impulse(e.p, 380, 110);
        sfx(Sfx::Explode, 3, e.p.x);
        shake = std::max(shake, 2.5f);
        if (!chain) return;
        if (e.type == SPLITTER) {
            for (int i = 0; i < 3; ++i) {
                Enemy s = makeEnemy(SHARD, e.p);
                s.v = fromAng(deg(i * 120 + frand(0, 120))) * 320;
                pending.push_back(s);
            }
        } else if (e.type == SINGULARITY) {
            bigExplosion(e.p, d.col, 260, 1100);
        }
    }
    void bigExplosion(V2 p, Col c, float radius, float strength) {
        emit(p, 350, c, 100, 900, 0.7f, 2.0f);
        emit(p, 150, WHITE, 50, 500, 0.4f, 1.2f);
        grid.impulse(p, strength, radius);
        shake = std::max(shake, 12.f);
        sfx(Sfx::BigExplode, 1, p.x);
        rumble(0.7f, 0.9f, 350);
    }
    // an ignored singularity collapses into a ring of shards
    void burstSingularity(Enemy& s) {
        if (s.dead) return;
        killEnemy(s, false, false);
        bigExplosion(s.p, DEFS[SINGULARITY].col, 220, 900);
        for (int i = 0; i < 8; ++i) {
            Enemy sh = makeEnemy(SHARD, s.p);
            sh.v = fromAng(deg(i * 45.f)) * 380;
            pending.push_back(sh);
        }
    }
    void hitEnemy(Enemy& e, const Bullet& b) {
        if (e.type == SINGULARITY) e.v += norm(b.v) * 6;
        e.flash = 0.06f;
        if (--e.hp <= 0) {
            killEnemy(e);
        } else {
            emit(b.p, 6, DEFS[e.type].col, 60, 220, 0.2f, 0.5f);
            sfx(Sfx::Ping, 2, e.p.x);
        }
    }
    void playerDie() {
        alive = false;
        respawn = 2.4f;
        emit(pp, 500, WHITE, 100, 1200, 0.8f, 2.4f);
        emit(pp, 300, rgb(255, 140, 40), 50, 700, 0.6f, 1.8f);
        grid.impulse(pp, 1800, 480);
        shake = 22;
        rumble(1, 1, 800);
        sfx(Sfx::Death, 1, pp.x);
        for (auto& e : enemies) killEnemy(e, false, false);
        for (auto& e : pending) killEnemy(e, false, false);
        bullets.clear();
        streak = 0;
        if (--lives <= 0) gameoverTimer = 2.5f;
    }
    void useBomb() {
        if (bombs <= 0 || !alive || mode != Mode::Play) return;
        --bombs;
        waves.push_back({pp, 0});
        grid.impulse(pp, 2600, 900);
        emit(pp, 400, WHITE, 600, 1400, 0.5f, 1.2f);
        shake = 16;
        rumble(0.8f, 1, 600);
        sfx(Sfx::Bomb, 1);
        invuln = std::max(invuln, 1.f);
        spawnTimer = std::max(spawnTimer, 1.5f);
    }

    // ------------------------------------------------ spawning
    V2 spawnPoint(float minDist = 260) {
        for (int i = 0; i < 30; ++i) {
            V2 p{frand(AX0 + 40, AX1 - 40), frand(AY0 + 40, AY1 - 40)};
            if (len(p - pp) > minDist) return p;
        }
        return pp.x > W / 2.f ? V2{AX0 + 40, AY0 + 40} : V2{AX1 - 40, AY1 - 40};
    }
    int countType(EType t) const {
        int n = 0;
        for (auto& e : enemies) n += (!e.dead && e.type == t);
        for (auto& e : pending) n += (!e.dead && e.type == t);
        return n;
    }
    void updateSpawner(float dt) {
        if (!waves.empty() || !alive) return;
        float t = gameT;
        spawnTimer -= dt;
        if (spawnTimer <= 0) {
            spawnTimer = std::max(0.3f, 1.5f - t * 0.0075f) * frand(0.7f, 1.3f);
            float w[NUM_TYPES] = {};
            w[DRIFTER] = 4;
            w[SEEKER] = t > 8 ? 4 : 0;
            if (t > 25) w[WEAVER] = 3;
            if (t > 45) w[SPLITTER] = 2;
            if (t > 70) w[LANCER] = 2;
            if (t > 95 && countType(SINGULARITY) < 1 + int(t > 200)) w[SINGULARITY] = 0.35f;
            float total = 0;
            for (float x : w) total += x;
            float r = frand(0, total);
            int kind = DRIFTER;
            for (int k = 0; k < NUM_TYPES; ++k) {
                if (w[k] <= 0) continue;
                kind = k;
                if (r < w[k]) break;
                r -= w[k];
            }
            int count = kind == SINGULARITY ? 1 : 1 + irand(0, 1 + int(t / 35));
            V2 p = spawnPoint();
            for (int i = 0; i < count; ++i) spawn(EType(kind), p + V2{frand(-30, 30), frand(-30, 30)});
        }
        eventTimer -= dt;
        if (eventTimer <= 0) {
            eventTimer = frand(14, 20);
            spawnEvent();
        }
    }
    void spawnEvent() {
        float t = gameT;
        int choice = irand(0, t > 80 ? 3 : 2);
        if (choice == 0) {  // corners
            int n = 3 + int(t / 30);
            EType kind = (t < 60 || frand(0, 1) < 0.6f) ? SEEKER : WEAVER;
            V2 corners[4] = {{AX0 + 40, AY0 + 40}, {AX1 - 40, AY0 + 40}, {AX0 + 40, AY1 - 40}, {AX1 - 40, AY1 - 40}};
            for (V2 c : corners) {
                if (len(c - pp) < 200) continue;
                for (int i = 0; i < n; ++i) spawn(kind, c + V2{frand(-25, 25), frand(-25, 25)});
            }
        } else if (choice == 1) {  // ring around the player
            int n = 12 + int(t / 20);
            for (int i = 0; i < n; ++i) {
                V2 p = pp + fromAng(i * TAU / n) * 320;
                p.x = std::clamp(p.x, AX0 + 30, AX1 - 30);
                p.y = std::clamp(p.y, AY0 + 30, AY1 - 30);
                spawn(t < 50 ? DRIFTER : SEEKER, p);
            }
        } else if (choice == 2) {  // wall along one side
            int n = 10 + int(t / 25);
            int side = irand(0, 3);
            for (int i = 0; i < n; ++i) {
                float f = (i + 0.5f) / n;
                V2 p = side == 0   ? V2{AX0 + f * (AX1 - AX0), AY0 + 30}
                       : side == 1 ? V2{AX0 + f * (AX1 - AX0), AY1 - 30}
                       : side == 2 ? V2{AX0 + 30, AY0 + f * (AY1 - AY0)}
                                   : V2{AX1 - 30, AY0 + f * (AY1 - AY0)};
                if (len(p - pp) > 150) spawn(t > 30 ? WEAVER : DRIFTER, p);
            }
        } else {  // lancers
            for (int i = 0; i < 4; ++i) spawn(LANCER, spawnPoint(350));
        }
        sfx(Sfx::Charge, 1);
    }

    // ------------------------------------------------ simulation
    void fire(V2 aim) {
        struct Shot { float off, lat; };
        static const std::vector<Shot> patterns[4] = {
            {{0, -5}, {0, 5}},
            {{-0.07f, 0}, {0, 0}, {0.07f, 0}},
            {{-0.12f, -4}, {-0.04f, 0}, {0.04f, 0}, {0.12f, 4}},
            {{-0.22f, 0}, {-0.11f, 0}, {0, 0}, {0.11f, 0}, {0.22f, 0}},
        };
        static const float cooldowns[4] = {0.095f, 0.09f, 0.08f, 0.075f};
        float ang = angOf(aim);
        V2 fwd = fromAng(ang), side = perp(fwd);
        for (const Shot& s : patterns[weapon]) {
            V2 d = fromAng(ang + s.off + frand(-0.015f, 0.015f));
            bullets.push_back({pp + fwd * 16 + side * s.lat, d * BULLET_SPEED + pv * 0.25f});
        }
        cooldown = cooldowns[weapon];
        sfx(Sfx::Shoot, 1, pp.x);
    }

    void keepIn(Enemy& e) {
        float r = e.r();
        if (e.p.x < AX0 + r) { e.p.x = AX0 + r; e.v.x = std::fabs(e.v.x); }
        else if (e.p.x > AX1 - r) { e.p.x = AX1 - r; e.v.x = -std::fabs(e.v.x); }
        if (e.p.y < AY0 + r) { e.p.y = AY0 + r; e.v.y = std::fabs(e.v.y); }
        else if (e.p.y > AY1 - r) { e.p.y = AY1 - r; e.v.y = -std::fabs(e.v.y); }
    }
    void seek(Enemy& e, float speed, float turn, float dt) {
        if (alive) {
            V2 to = pp - e.p;
            if (len2(to) > 1) e.v += (norm(to) * speed - e.v) * std::min(1.f, turn * dt);
        } else {
            e.v *= std::exp(-2 * dt);
        }
    }
    void enemyAI(Enemy& e, float dt) {
        switch (e.type) {
            case DRIFTER: {  // wanders, drifting gently toward you
                e.ang += e.spin * dt;
                if (alive && len2(pp - e.p) > 1) e.v += norm(pp - e.p) * (12 * dt);
                float cap = 130 * e.speed;
                if (len(e.v) > cap) e.v = norm(e.v) * cap;
            } break;
            case SEEKER: {  // homes in
                seek(e, 215 * e.speed, 2.6f, dt);
                if (len2(e.v) > 1) e.ang = angOf(e.v);
            } break;
            case WEAVER: {  // zig-zags toward you and sidesteps bullets
                e.ang += dt * 2.5f;
                V2 desired;
                if (alive && len2(pp - e.p) > 1) {
                    V2 fwd = norm(pp - e.p);
                    desired = fwd * (150 * e.speed) + perp(fwd) * (std::sin(e.t * 4) * 140);
                }
                for (auto& b : bullets) {
                    V2 d = e.p - b.p;
                    if (len2(d) >= 120 * 120) continue;
                    V2 bv = norm(b.v);
                    if (dot(d, bv) <= 0) continue;
                    float side = d.x * bv.y - d.y * bv.x;
                    if (std::fabs(side) < 34) {
                        desired += (side >= 0 ? V2{bv.y, -bv.x} : V2{-bv.y, bv.x}) * 420;
                        break;
                    }
                }
                e.v += (desired - e.v) * std::min(1.f, 6 * dt);
            } break;
            case SPLITTER: {
                seek(e, 105 * e.speed, 1.5f, dt);
                e.ang += dt * 1.2f;
            } break;
            case SHARD: {  // spirals in
                if (alive && len2(pp - e.p) > 1) {
                    V2 f = norm(pp - e.p);
                    V2 desired = f * (230 * e.speed) + perp(f) * (160.f * e.dir);
                    e.v += (desired - e.v) * std::min(1.f, 2.5f * dt);
                }
                if (len2(e.v) > 1) e.ang = angOf(e.v);
            } break;
            case LANCER: {  // lines up a shot, then dashes in a straight line
                e.timer -= dt;
                if (e.state == L_DRIFT) {
                    seek(e, 70 * e.speed, 1, dt);
                    e.ang += dt * 2;
                    if (e.timer <= 0 && alive) {
                        e.state = L_AIM;
                        e.timer = 0.65f;
                        sfx(Sfx::Charge, 1, e.p.x);
                    }
                } else if (e.state == L_AIM) {
                    e.v *= std::exp(-8 * dt);
                    if (alive && len2(pp - e.p) > 1) e.aim = norm(pp - e.p);
                    e.ang += dt * 16;
                    if (e.timer <= 0) {
                        e.state = L_DASH;
                        e.timer = 0.5f;
                        e.v = e.aim * (720 * e.speed);
                    }
                } else {
                    emit(e.p, 2, DEFS[LANCER].col, 10, 60, 0.2f, 0.4f);
                    if (e.timer <= 0) {
                        e.state = L_DRIFT;
                        e.timer = frand(1.6f, 2.8f);
                        e.v *= 0.2f;
                    }
                }
            } break;
            case SINGULARITY: {  // gravity well; bursts if ignored too long
                e.age += dt;
                e.v *= std::exp(-0.5f * dt);
                if (e.age > 22) burstSingularity(e);
            } break;
            default: break;
        }
    }

    void updatePlay(float dt) {
        gameT += dt;
        if (opt.autoplay) autopilot();
        std::vector<V2> sings;
        for (auto& e : enemies)
            if (e.type == SINGULARITY && !e.dead && !e.spawning()) sings.push_back(e.p);

        if (alive) {
            pv += (inMove * PLAYER_SPEED - pv) * std::min(1.f, 14 * dt);
            for (V2 s : sings) {
                V2 d = s - pp;
                float dist = len(d);
                if (dist > 1 && dist < 420) pv += d / dist * (52000 / std::max(dist, 50.f) * dt);
            }
            pp += pv * dt;
            pp.x = std::clamp(pp.x, AX0 + PLAYER_R, AX1 - PLAYER_R);
            pp.y = std::clamp(pp.y, AY0 + PLAYER_R, AY1 - PLAYER_R);
            if (len2(inMove) > 0.01f) {
                pang += wrapAng(angOf(inMove) - pang) * std::min(1.f, 16 * dt);
                float back = pang + PI;
                emit(pp + fromAng(back) * 12, 2, rgb(255, 160, 60), 80, 220, 0.15f, 0.35f, back, 0.6f, 30);
            }
            invuln = std::max(0.f, invuln - dt);
            cooldown -= dt;
            if (len2(inAim) > 0.01f && cooldown <= 0) fire(inAim);
        } else if (lives > 0) {
            respawn -= dt;
            if (respawn <= 0) {
                pp = {W / 2.f, H / 2.f};
                pv = {};
                pang = -PI / 2;
                alive = true;
                invuln = 2;
                cooldown = 0;
                grid.impulse(pp, -900, 300);
                spawnTimer = 1.5f;
            }
        } else {
            gameoverTimer -= dt;
            if (gameoverTimer <= 0) {
                mode = Mode::GameOver;
                audio.setMusicLevel(0.25f);
                if (score > hiscore) {
                    hiscore = score;
                    newHigh = true;
                    saveHigh();
                }
            }
        }

        // bullets: bent by singularities, sparks where they hit the wall
        for (auto& b : bullets) {
            for (V2 s : sings) {
                V2 d = s - b.p;
                float d2 = len2(d);
                if (d2 > 1 && d2 < 220 * 220) b.v += d * (3.2e6f / (d2 * std::sqrt(d2)) * dt);
            }
            b.p += b.v * dt;
            if (b.p.x < AX0 || b.p.x >= AX1 || b.p.y < AY0 || b.p.y >= AY1) {
                b.dead = true;
                V2 c{std::clamp(b.p.x, AX0, AX1), std::clamp(b.p.y, AY0, AY1)};
                emit(c, 5, rgb(255, 220, 120), 60, 240, 0.15f, 0.4f);
            }
        }
        for (auto& b : bullets) grid.pushBullet(b.p, b.v, dt);

        // enemies
        for (size_t i = 0; i < enemies.size(); ++i) {
            Enemy& e = enemies[i];
            if (e.dead) continue;
            e.t += dt;
            if (e.spawn > 0) {
                e.spawn -= dt;
                continue;
            }
            e.flash = std::max(0.f, e.flash - dt);
            enemyAI(e, dt);
            e.p += e.v * dt;
            keepIn(e);
        }

        // bullet vs enemy
        for (auto& b : bullets) {
            if (b.dead) continue;
            for (auto& e : enemies) {
                if (e.dead || e.spawning()) continue;
                float r = e.r() + 5;
                if (len2(e.p - b.p) < r * r) {
                    b.dead = true;
                    hitEnemy(e, b);
                    break;
                }
            }
        }

        // enemy vs player
        if (alive && invuln <= 0) {
            for (auto& e : enemies) {
                if (e.dead || e.spawning()) continue;
                float r = e.r() + PLAYER_R - 3;
                if (len2(e.p - pp) < r * r) {
                    playerDie();
                    break;
                }
            }
        }

        // bomb shockwaves sweep the arena, scoring everything they touch
        for (auto& w : waves) {
            w.r += 1600 * dt;
            for (auto& e : enemies)
                if (!e.dead && len(e.p - w.c) < w.r) killEnemy(e, true, false);
        }
        waves.erase(std::remove_if(waves.begin(), waves.end(), [](const Wave& w) { return w.r >= 1600; }), waves.end());

        bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return b.dead; }), bullets.end());
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.dead; }), enemies.end());

        for (V2 s : sings) grid.impulse(s, -1500 * dt, 170);
        updateSpawner(dt);
        for (auto& e : pending)
            if (!e.dead) enemies.push_back(e);
        pending.clear();
        updateFx(dt, sings);
    }

    void updateFx(float dt, const std::vector<V2>& attractors = {}) {
        updateParticles(dt, attractors);
        grid.update(dt);
        for (auto& f : floaters) {
            f.p.y -= 30 * dt;
            f.life -= dt;
        }
        floaters.erase(std::remove_if(floaters.begin(), floaters.end(), [](const Floater& f) { return f.life <= 0; }),
                       floaters.end());
        for (auto& b : banners) b.life -= dt;
        banners.erase(std::remove_if(banners.begin(), banners.end(), [](const Banner& b) { return b.life <= 0; }),
                      banners.end());
        shake *= std::exp(-6 * dt);
    }

    void updateTitle(float dt) {
        titleTimer -= dt;
        if (titleTimer <= 0) {
            titleTimer = frand(0.4f, 1.0f);
            V2 p{frand(AX0 + 60, AX1 - 60), frand(AY0 + 60, AY1 - 60)};
            static const EType kinds[5] = {DRIFTER, SEEKER, WEAVER, SPLITTER, LANCER};
            emit(p, 120, DEFS[kinds[irand(0, 4)]].col, 100, 600);
            grid.impulse(p, 700, 160);
        }
        updateFx(dt);
    }

    void step(float dt) {
        clock += dt;
        std::fill(std::begin(sfxUsed), std::end(sfxUsed), 0);
        switch (mode) {
            case Mode::Play: updatePlay(dt); break;
            case Mode::Title: updateTitle(dt); break;
            case Mode::GameOver:
                gameoverTimer -= dt;
                updateFx(dt);
                break;
            case Mode::Paused: break;
        }
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
        inAim = nearest ? norm(nearest->p - pp) : V2{};
        if (best < 40 && bombs > 0 && invuln <= 0) useBomb();
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
        float trig = 0;
        if (pad) {
            auto ax = [&](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(pad, a) / 32767.f; };
            inMove = deadzone(ax(SDL_GAMEPAD_AXIS_LEFTX), ax(SDL_GAMEPAD_AXIS_LEFTY), 0.22f);
            inAim = deadzone(ax(SDL_GAMEPAD_AXIS_RIGHTX), ax(SDL_GAMEPAD_AXIS_RIGHTY), 0.3f);
            trig = std::max(ax(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), ax(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        }
        if (trig > 0.6f && !trigHeld) useBomb();  // trigger edge -> bomb
        trigHeld = trig > 0.6f;

        const bool* k = SDL_GetKeyboardState(nullptr);
        V2 km{float(k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A]), float(k[SDL_SCANCODE_S] - k[SDL_SCANCODE_W])};
        if (len2(km) > 0) inMove = norm(km);
        V2 ka{float(k[SDL_SCANCODE_RIGHT] - k[SDL_SCANCODE_LEFT]), float(k[SDL_SCANCODE_DOWN] - k[SDL_SCANCODE_UP])};
        if (len2(ka) > 0) inAim = norm(ka);
        float mx, my;
        if (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK) {
            int ww, wh, ow, oh;
            SDL_GetWindowSize(win, &ww, &wh);
            SDL_GetRenderOutputSize(ren, &ow, &oh);
            SDL_FRect lb = letterbox();
            float px = mx * ow / float(std::max(1, ww)), py = my * oh / float(std::max(1, wh));
            V2 d = V2{(px - lb.x) * W / lb.w, (py - lb.y) * H / lb.h} - pp;
            if (len2(d) > 4) inAim = norm(d);
        }
    }
    void handle(const SDL_Event& ev) {
        switch (ev.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_GAMEPAD_ADDED: openPad(ev.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (pad && SDL_GetGamepadID(pad) == ev.gdevice.which) {
                    SDL_CloseGamepad(pad);
                    pad = nullptr;
                    if (mode == Mode::Play) mode = Mode::Paused;
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
                    case SDL_GAMEPAD_BUTTON_START:
                        if (mode == Mode::Play) mode = Mode::Paused;
                        else if (mode == Mode::Paused) mode = Mode::Play;
                        else confirm();
                        break;
                    case SDL_GAMEPAD_BUTTON_BACK:
                        if (mode == Mode::Title) running = false;
                        else if (mode == Mode::Paused || mode == Mode::GameOver) toTitle();
                        break;
                    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
                    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: useBomb(); break;
                    case SDL_GAMEPAD_BUTTON_NORTH: audio.toggleMusic(); break;
                    default: break;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.repeat) break;
                switch (ev.key.key) {
                    case SDLK_ESCAPE:
                        if (mode == Mode::Title) running = false;
                        else if (mode == Mode::Play) mode = Mode::Paused;
                        else if (mode == Mode::Paused) mode = Mode::Play;
                        else toTitle();
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (ev.key.mod & SDL_KMOD_ALT) toggleFullscreen();
                        else confirm();
                        break;
                    case SDLK_Q:
                        if (mode == Mode::Paused) toTitle();
                        break;
                    case SDLK_SPACE: useBomb(); break;
                    case SDLK_M: audio.toggleMusic(); break;
                    case SDLK_F3: showFps = !showFps; break;
                    case SDLK_F11: toggleFullscreen(); break;
                    default: break;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button == SDL_BUTTON_RIGHT) useBomb();
                break;
            default: break;
        }
    }
    void toggleFullscreen() {
        bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN;
        SDL_SetWindowFullscreen(win, !fs);
    }

    // ------------------------------------------------ rendering
    // text centred on (x, cy)
    void textC(const std::string& s, float cy, float size, Col c, float x = W / 2.f) {
        drawText(dr, s, {x, cy - size / 2}, size, c, 0.5f);
    }

    void drawEnemy(const Enemy& e) {
        const EnemyDef& d = DEFS[e.type];
        Col col = e.flash > 0 ? WHITE : d.col;
        if (e.type == SINGULARITY) {
            if (e.spawn > 0) {
                float k = e.spawn / d.spawnTime;
                dr.circle(e.p, d.r * (1 + 3 * k), 1.6f, d.col * (1 - k));
                return;
            }
            float pulse = 1 + 0.08f * std::sin(clock * 6 + e.t);
            float urgency = std::max(0.f, (e.age - 14) / 8);
            if (urgency > 0 && int(clock * (6 + 20 * urgency)) % 2 == 0) col = rgb(255, 200, 200);
            dr.circle(e.p, d.r * pulse, 2.4f, col);
            for (int i = 0; i < 3; ++i) {
                float r = d.r * (0.35f + 0.2f * i) * pulse;
                float a0 = clock * (3 - i) * (i % 2 ? 1 : -1);
                for (int j = 0; j < 3; ++j) dr.arc(e.p, r, a0 + j * TAU / 3, a0 + j * TAU / 3 + 1.2f, 1.6f, col * 0.8f);
            }
            dr.arc(e.p, d.r + 7, 0, TAU * e.hp / d.hp, 0.9f, d.col * 0.5f, 48);  // health ring
            return;
        }
        float sc = 1;
        if (e.spawn > 0) {
            float k = e.spawn / d.spawnTime;
            sc = 1 + 2.2f * k;
            col = d.col * ((1 - k) * (0.6f + 0.4f * std::sin(clock * 40)));
        }
        if (e.type == LANCER && e.state == L_AIM && e.spawn <= 0) {
            float k = 1 - e.timer / 0.65f;
            dr.line(e.p, e.p + e.aim * 900, 0.8f, d.col * (0.15f + 0.35f * k));
        }
        for (auto& s : d.shapes) dr.poly(s, e.p, e.ang, sc, 2.f, col);
    }

    void drawWorld() {
        grid.draw(dr, clock);
        Col border = hsvDeg(220 + 40 * std::sin(clock * 0.5f), 0.6f, 0.9f);
        V2 c0{AX0 - 3, AY0 - 3}, c1{AX1 + 3, AY0 - 3}, c2{AX1 + 3, AY1 + 3}, c3{AX0 - 3, AY1 + 3};
        dr.line(c0, c1, 1.4f, border);
        dr.line(c1, c2, 1.4f, border);
        dr.line(c2, c3, 1.4f, border);
        dr.line(c3, c0, 1.4f, border);

        for (auto& p : parts) {
            float f = clamp01(p.life / p.maxLife);
            dr.line(p.p, p.p - p.v * 0.035f, 1.4f, p.c * (f * (2 - f)));
        }
        if (mode == Mode::Title) return;
        for (auto& e : enemies)
            if (!e.dead) drawEnemy(e);
        for (auto& b : bullets) {
            V2 d = norm(b.v) * 16;
            dr.line(b.p, b.p - d, 2.f, rgb(255, 235, 150));
            dr.line(b.p, b.p - d * 0.4f, 1.4f, WHITE);
        }
        if (alive && !(invuln > 0 && int(clock * 16) % 2 == 0)) {
            dr.poly(SH_PLAYER, pp, pang, 1, 2.6f, rgb(235, 245, 255));
            dr.poly(SH_PLAYER_IN, pp, pang, 1, 1.8f, rgb(120, 220, 255));
        }
        for (auto& w : waves) dr.circle(w.c, w.r, 3.f, rgb(200, 230, 255) * (1 - w.r / 1600), 96);
        for (auto& f : floaters) textC(f.s, f.p.y, 11, f.c * std::min(1.f, f.life * 2), f.p.x);
    }

    void drawHUD() {
        drawText(dr, commas(score), {40, 42}, 26, WHITE);
        drawText(dr, "X" + std::to_string(multiplier()), {42, 82}, 16, rgb(255, 220, 120));
        drawText(dr, "HI " + commas(std::max(hiscore, score)), {W - 40.f, 44}, 16, rgb(180, 200, 255), 1);
        for (int i = 0; i < std::min(lives, 8); ++i)
            dr.poly(SH_PLAYER, {W / 2.f - 120 + i * 30, 52}, -PI / 2, 0.75f, 1.6f, rgb(235, 245, 255));
        for (int i = 0; i < std::min(bombs, 8); ++i) {
            V2 c{W / 2.f + 60 + i * 26, 52};
            dr.circle(c, 9, 1.4f, rgb(255, 200, 90), 24);
            dr.dot(c, 4.5f, rgb(255, 200, 90));
        }
        size_t first = banners.size() > 3 ? banners.size() - 3 : 0;
        for (size_t i = first; i < banners.size(); ++i)
            textC(banners[i].s, H / 2.f - 140 + (i - first) * 50, 24, banners[i].c * std::min(1.f, banners[i].life * 1.5f));
    }

    void drawOverlay() {
        float t = clock;
        if (mode == Mode::Title) {
            textC("PRISM ARENA", H / 2.f - 130, 64, hsvDeg(t * 40, 0.6f, 1));
            if (int(t * 2) % 2 == 0) textC("PRESS  A  OR  START", H / 2.f + 10, 24, WHITE);
            Col help = rgb(170, 190, 230);
            textC("LEFT STICK  MOVE     RIGHT STICK  AIM + FIRE     RB / LB / TRIGGERS  BOMB     START  PAUSE     Y  MUSIC",
                  H / 2.f + 110, 11, help);
            textC("KEYBOARD: WASD MOVE  -  MOUSE OR ARROWS AIM + FIRE  -  SPACE BOMB  -  ESC PAUSE  -  M MUSIC  -  F11 FULLSCREEN",
                  H / 2.f + 140, 11, help);
            if (pad)
                textC(std::string("CONTROLLER: ") + SDL_GetGamepadName(pad), H - 90.f, 11, rgb(120, 255, 160));
            else
                textC("NO CONTROLLER DETECTED - KEYBOARD / MOUSE", H - 90.f, 11, rgb(255, 150, 120));
            textC("HIGH SCORE  " + commas(hiscore), H - 130.f, 16, rgb(255, 220, 120));
        } else if (mode == Mode::Paused) {
            textC("PAUSED", H / 2.f - 40, 64, WHITE);
            textC("START / A RESUME     BACK / Q QUIT TO TITLE", H / 2.f + 50, 16, rgb(190, 200, 230));
        } else if (mode == Mode::GameOver) {
            textC("GAME OVER", H / 2.f - 80, 64, rgb(255, 80, 100));
            textC(commas(score), H / 2.f + 10, 26, WHITE);
            if (newHigh && int(t * 3) % 2 == 0) textC("NEW HIGH SCORE!", H / 2.f + 65, 26, rgb(255, 220, 120));
            if (gameoverTimer <= -1) textC("PRESS A TO CONTINUE", H / 2.f + 130, 16, rgb(190, 200, 230));
        }
    }

    void render() {
        // world (shaken) -> scene; the title glows, so it goes in the scene too
        dr.off = shake > 0.3f ? V2{frand(-shake, shake), frand(-shake, shake)} : V2{};
        SDL_SetRenderTarget(ren, scene);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        drawWorld();
        dr.off = {};
        if (mode == Mode::Title) textC("PRISM ARENA", H / 2.f - 130, 64, hsvDeg(clock * 40, 0.6f, 1));
        dr.flush();

        // bloom: halve down the chain, then add each level back up
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

        // frame = scene + glow, then crisp HUD and overlays on top
        SDL_SetRenderTarget(ren, frame);
        SDL_SetTextureBlendMode(scene, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(ren, scene, nullptr, nullptr);
        SDL_SetTextureBlendMode(bloom[0], SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(bloom[0], 190, 190, 190);
        SDL_RenderTexture(ren, bloom[0], nullptr, nullptr);
        if (mode == Mode::Paused) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
            SDL_RenderFillRect(ren, nullptr);
        }
        if (mode != Mode::Title) drawHUD();
        drawOverlay();
        if (showFps) drawText(dr, std::to_string(int(fps + 0.5f)) + " FPS", {W - 10.f, H - 18.f}, 11, rgb(120, 255, 120), 1);
        dr.flush();

        SDL_SetRenderTarget(ren, nullptr);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_FRect dst = letterbox();
        SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(ren, frame, nullptr, &dst);

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
            fpsT += float(dt);
            if (++fpsFrames, fpsT >= 0.5f) {
                fps = fpsFrames / fpsT;
                fpsT = 0;
                fpsFrames = 0;
            }
            pollInput();
            while (acc >= STEP) {
                step(STEP);
                acc -= STEP;
            }
            if (opt.autoplay && mode == Mode::GameOver && gameoverTimer < -2) startGame();
            render();
        }
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
    if (std::getenv("PRISM_NO_RUMBLE")) g.opt.rumble = false;
    if (!g.init()) return 1;
    g.run();
    g.shutdown();
    return 0;
}
