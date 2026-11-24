#include "audio.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr int SR = 44100;
constexpr float PI = 3.14159265f;
constexpr float TAU = 6.2831853f;
constexpr double BPM = 124.0;

uint32_t gSeed = 0x2545F491u;
float rnd() {
    gSeed ^= gSeed << 13;
    gSeed ^= gSeed >> 17;
    gSeed ^= gSeed << 5;
    return (gSeed / 4294967296.0f) * 2.f - 1.f;
}

template <class F>
std::vector<float> synth(float dur, F f) {
    std::vector<float> b(size_t(dur * SR) + 2);
    for (size_t i = 0; i < b.size(); ++i) b[i] = f(float(i) / SR, dur);
    b.back() = 0;
    return b;
}

const int kLimit[(int)Sfx::Count] = {3, 6, 6, 3, 4, 2, 1, 1, 1, 3, 3};
}  // namespace

float Audio::noise() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return (seed / 4294967296.0f) * 2.f - 1.f;
}

bool Audio::init() {
    using S = Sfx;
    bufs[(int)S::Shoot] = synth(0.07f, [ph = 0.f](float t, float d) mutable {
        float f = 260 + 1200 * std::exp(-t * 45);
        ph += f / SR;
        float s = 0.6f * std::sin(TAU * ph) + 0.25f * (std::sin(TAU * ph) > 0 ? 1.f : -1.f);
        float k = 1 - t / d;
        return s * k * k * 0.22f;
    });
    bufs[(int)S::Hit] = synth(0.12f, [ph = 0.f, lp = 0.f](float t, float d) mutable {
        lp += 0.5f * (rnd() - lp);
        ph += (700 * std::exp(-t * 20) + 180) / SR;
        return (lp * 0.35f * std::exp(-t * 40) + std::sin(TAU * ph) * 0.25f * (1 - t / d));
    });
    bufs[(int)S::Explode] = synth(0.65f, [ph = 0.f, lp = 0.f](float t, float) mutable {
        float c = 0.04f + 0.35f * std::exp(-t * 6);
        lp += c * (rnd() - lp);
        ph += (38 + 90 * std::exp(-t * 16)) / SR;
        return (lp * 1.4f * std::exp(-t * 5.5f) + std::sin(TAU * ph) * 0.6f * std::exp(-t * 9)) * 0.45f;
    });
    bufs[(int)S::BigExplode] = synth(1.6f, [ph = 0.f, lp = 0.f, lp2 = 0.f](float t, float) mutable {
        float c = 0.02f + 0.25f * std::exp(-t * 3);
        lp += c * (rnd() - lp);
        lp2 += c * (lp - lp2);
        ph += (30 + 70 * std::exp(-t * 6)) / SR;
        return (lp2 * 2.5f * std::exp(-t * 2.4f) + std::sin(TAU * ph) * 0.7f * std::exp(-t * 3.5f)) * 0.5f;
    });
    bufs[(int)S::Spawn] = synth(0.2f, [ph = 0.f](float t, float d) mutable {
        float k = t / d;
        ph += (220 + 900 * k * k) / SR;
        return std::sin(TAU * ph) * std::sin(PI * k) * 0.09f;
    });
    bufs[(int)S::Bomb] = synth(2.4f, [ph = 0.f, lp = 0.f, lp2 = 0.f](float t, float) mutable {
        float c = 0.01f + 0.15f * std::exp(-t * 1.6f);
        lp += c * (rnd() - lp);
        lp2 += c * (lp - lp2);
        ph += (28 + 60 * std::exp(-t * 2.5f)) / SR;
        return (lp2 * 3.f * std::exp(-t * 1.5f) + std::sin(TAU * ph) * 0.8f * std::exp(-t * 1.8f)) * 0.55f;
    });
    bufs[(int)S::Death] = synth(1.8f, [ph = 0.f, lp = 0.f](float t, float) mutable {
        float f = 340 * std::exp(-t * 2.2f) + 28;
        ph += f / SR;
        float saw = 2 * (ph - std::floor(ph)) - 1;
        lp += 0.08f * (rnd() - lp);
        return (saw * 0.35f + lp * 1.2f) * std::exp(-t * 1.9f) * 0.5f;
    });
    bufs[(int)S::ExtraLife] = synth(0.75f, [ph = 0.f](float t, float) mutable {
        static const float notes[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
        int i = std::min(3, int(t / 0.11f));
        ph += notes[i] / SR;
        float s = std::sin(TAU * ph) + 0.3f * std::sin(TAU * ph * 2);
        return s * std::exp(-(t - i * 0.11f) * (i == 3 ? 5.f : 14.f)) * 0.16f;
    });
    bufs[(int)S::MultUp] = synth(0.22f, [ph = 0.f](float t, float d) mutable {
        ph += (t < 0.07f ? 880.f : 1318.5f) / SR;
        return std::sin(TAU * ph) * (1 - t / d) * 0.15f;
    });
    bufs[(int)S::Charge] = synth(0.65f, [ph = 0.f](float t, float d) mutable {
        float k = t / d;
        ph += (140 + 760 * k * k) / SR;
        float saw = 2 * (ph - std::floor(ph)) - 1;
        return saw * k * (1 - std::pow(k, 8.f)) * 0.07f;
    });
    bufs[(int)S::Absorb] = synth(0.22f, [ph = 0.f](float t, float d) mutable {
        ph += (950 * std::exp(-t * 9) + 120) / SR;
        return std::sin(TAU * ph) * (1 - t / d) * 0.16f;
    });

    mtx = SDL_CreateMutex();
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, SR};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this);
    if (!stream) {
        SDL_Log("audio disabled: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream);
    return true;
}

void Audio::shutdown() {
    if (stream) SDL_DestroyAudioStream(stream);
    stream = nullptr;
    if (mtx) SDL_DestroyMutex(mtx);
    mtx = nullptr;
}

void Audio::play(Sfx s, float vol, float pan, float pitch) {
    if (!stream) return;
    int id = (int)s;
    pan = std::clamp(pan, -1.f, 1.f);
    SDL_LockMutex(mtx);
    int same = 0, oldestSame = -1, oldestAny = -1, freeSlot = -1;
    for (int i = 0; i < (int)voices.size(); ++i) {
        Voice& v = voices[i];
        if (v.id < 0) {
            if (freeSlot < 0) freeSlot = i;
            continue;
        }
        if (oldestAny < 0 || v.age < voices[oldestAny].age) oldestAny = i;
        if (v.id == id) {
            ++same;
            if (oldestSame < 0 || v.age < voices[oldestSame].age) oldestSame = i;
        }
    }
    int slot = same >= kLimit[id] ? oldestSame : (freeSlot >= 0 ? freeSlot : oldestAny);
    Voice& v = voices[slot];
    v.id = id;
    v.pos = 0;
    v.inc = pitch;
    v.l = vol * std::sqrt(0.5f * (1 - pan));
    v.r = vol * std::sqrt(0.5f * (1 + pan));
    v.age = ++ageCounter;
    SDL_UnlockMutex(mtx);
}

void SDLCALL Audio::callback(void* ud, SDL_AudioStream* s, int additional, int) {
    auto* a = static_cast<Audio*>(ud);
    float buf[2048];
    int frames = additional / int(sizeof(float) * 2);
    while (frames > 0) {
        int n = std::min(frames, 1024);
        a->mix(buf, n);
        SDL_PutAudioStreamData(s, buf, n * int(sizeof(float) * 2));
        frames -= n;
    }
}

float Audio::musicSample() {
    float target = musicOn.load(std::memory_order_relaxed) ? musicTarget.load(std::memory_order_relaxed) : 0.f;
    musicLevel += (target - musicLevel) * 0.00006f;
    if (musicLevel < 0.0005f && target <= 0.f) return 0.f;

    static const float roots[4] = {220.f, 174.61f, 261.63f, 196.f};  // Am F C G
    static const bool minor[4] = {true, false, false, false};
    static const int bassPat[16] = {0, -1, 12, -1, 0, 0, 12, -1, 0, -1, 12, -1, 0, 7, 12, 7};
    static const int arpSeq[8] = {0, 2, 1, 3, 2, 1, 3, 2};

    const double sps = SR * 60.0 / (BPM * 4);
    int step = int(mClock / sps);
    mClock += 1;
    if (step != mStep) {
        mStep = step;
        int bar = (step / 16) % 4, s = step % 16;
        float root = roots[bar];
        int third = minor[bar] ? 3 : 4;
        if (s == 0) {
            padF[0] = root / 2;
            padF[1] = root / 2 * std::pow(2.f, third / 12.f);
            padF[2] = root / 2 * 1.4983f;
        }
        if (bassPat[s] >= 0) {
            bassF = root / 4 * std::pow(2.f, bassPat[s] / 12.f);
            bassEnv = 1;
        }
        const int tones[4] = {0, third, 7, 12};
        arpF = root * 2 * std::pow(2.f, tones[arpSeq[s % 8]] / 12.f);
        arpEnv = 1;
        if (s % 4 == 0) kickEnv = 1;
        if (s % 4 == 2) hatEnv = 1;
    }

    float drums = std::clamp((musicLevel - 0.45f) / 0.35f, 0.f, 1.f);
    float out = 0;

    bassPh += bassF / SR;
    if (bassPh >= 1) bassPh -= 1;
    float saw = 2 * bassPh - 1;
    bassEnv *= 0.99994f;
    bassLp += (0.015f + 0.18f * bassEnv) * (saw - bassLp);
    out += bassLp * (0.15f + 0.45f * bassEnv);

    arpPh += arpF / SR;
    if (arpPh >= 1) arpPh -= 1;
    float tri = 4 * std::fabs(arpPh - 0.5f) - 1;
    arpEnv *= 0.99968f;
    out += tri * arpEnv * 0.10f;

    for (int i = 0; i < 3; ++i) {
        padPh[i] += padF[i] / SR;
        if (padPh[i] >= 1) padPh[i] -= 1;
        out += std::sin(TAU * padPh[i]) * 0.028f;
    }

    float kf = 45 + 110 * kickEnv * kickEnv;
    kickPh += kf / SR;
    if (kickPh >= 1) kickPh -= 1;
    out += std::sin(TAU * kickPh) * kickEnv * 0.55f * drums;
    kickEnv *= 0.99975f;

    float n = noise();
    out += (n - hatPrev) * hatEnv * 0.06f * drums;
    hatPrev = n;
    hatEnv *= 0.9985f;

    return out * musicLevel * 0.55f;
}

void Audio::mix(float* out, int frames) {
    SDL_LockMutex(mtx);
    for (int f = 0; f < frames; ++f) {
        float m = musicSample();
        float L = m, R = m;
        for (Voice& v : voices) {
            if (v.id < 0) continue;
            const std::vector<float>& b = bufs[v.id];
            size_t i = size_t(v.pos);
            if (i + 1 >= b.size()) {
                v.id = -1;
                continue;
            }
            float fr = float(v.pos - double(i));
            float s = b[i] + (b[i + 1] - b[i]) * fr;
            L += s * v.l;
            R += s * v.r;
            v.pos += v.inc;
        }
        out[2 * f] = std::tanh(L * 0.9f);
        out[2 * f + 1] = std::tanh(R * 0.9f);
    }
    SDL_UnlockMutex(mtx);
}
