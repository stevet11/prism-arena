// Procedural audio: synthesized sound effects + a small sequenced music loop.
#pragma once
#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

enum class Sfx : int {
    Shoot, Ping, Explode, BigExplode, Spawn, Bomb, Death, Bonus, Charge, Count
};

class Audio {
public:
    bool init();
    void shutdown();
    // vol 0..1, pan -1..1, pitch multiplier
    void play(Sfx s, float vol = 1.f, float pan = 0.f, float pitch = 1.f);
    void setMusicLevel(float lvl) { musicTarget.store(lvl, std::memory_order_relaxed); }
    void toggleMusic() { musicOn.store(!musicOn.load()); }
    bool musicEnabled() const { return musicOn.load(); }
    bool ok() const { return stream != nullptr; }

private:
    struct Voice {
        int id = -1;
        double pos = 0, inc = 1;
        float l = 0, r = 0;
        uint32_t age = 0;
    };
    static void SDLCALL callback(void* ud, SDL_AudioStream* s, int additional, int total);
    void mix(float* out, int frames);
    float musicSample();
    float noise();

    SDL_AudioStream* stream = nullptr;
    SDL_Mutex* mtx = nullptr;
    std::vector<float> bufs[(int)Sfx::Count];
    std::array<Voice, 48> voices{};
    uint32_t ageCounter = 0;
    std::atomic<float> musicTarget{0.f};
    std::atomic<bool> musicOn{true};

    // music synth state (audio thread only)
    float musicLevel = 0;
    double mClock = 0;
    int mStep = -1;
    float bassPh = 0, bassF = 55, bassEnv = 0, bassLp = 0;
    float arpPh = 0, arpF = 440, arpEnv = 0;
    float padPh[3] = {}, padF[3] = {110, 130.8f, 164.8f};
    float kickPh = 0, kickEnv = 0, hatEnv = 0, hatPrev = 0;
    uint32_t seed = 0x9E3779B9u;
};
