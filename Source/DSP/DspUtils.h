#pragma once
//==============================================================================
// RoomBreaker — small real-time-safe DSP building blocks.
// No allocation outside prepare(), no locks, no exceptions on the audio path.
//==============================================================================
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rb::dsp
{
constexpr float kPi     = 3.14159265358979323846f;
constexpr float kHalfPi = 0.5f * kPi;
constexpr float kTwoPi  = 2.0f * kPi;

inline float clamp01 (float x) noexcept               { return std::clamp (x, 0.0f, 1.0f); }
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float dbToGain (float db) noexcept              { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept               { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

/** Hermite smoothstep. Works for falling edges too (edge0 > edge1). */
inline float smoothstep (float edge0, float edge1, float x) noexcept
{
    const float t = clamp01 ((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

/** sin(x) for x in [0, 2π], 7th-order minimax polynomial after folding: error below -120 dB.
    Used where a sine is needed every sample (the impact booms). */
inline float fastSin (float x) noexcept
{
    x -= kPi;                                             // [-π, π], sign flipped
    x = x > kHalfPi ? kPi - x : (x < -kHalfPi ? -kPi - x : x);
    const float x2 = x * x;
    return -x * (0.99999660f + x2 * (-0.16664824f + x2 * (0.00830629f - x2 * 0.00018363f)));
}

/** tan(x) for x in [0, 0.49π] (filter pre-warping), Padé [5/4]: exact to 1e-6 below a quarter of the
    sample rate, 0.03 % off at the very top,
    with no library call. Used where many filter coefficients are recomputed per control tick. */
inline float fastTan (float x) noexcept
{
    const float x2 = x * x;
    return x * (945.0f - 105.0f * x2 + x2 * x2) / (945.0f - 420.0f * x2 + 15.0f * x2 * x2);
}

/** Sum of n values (n a multiple of 8) as eight lanes: short chains that overlap and vectorise. */
inline float sumLanes (const float* a, int n) noexcept
{
    float lane[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    for (int k = 0; k < n; k += 8)
        for (int j = 0; j < 8; ++j)
            lane[j] += a[k + j];
    return ((lane[0] + lane[1]) + (lane[2] + lane[3])) + ((lane[4] + lane[5]) + (lane[6] + lane[7]));
}

/** Pole for a one-pole smoother with time constant tau, evaluated at rateHz. */
inline float onePoleCoeff (float tauSeconds, float rateHz) noexcept
{
    return tauSeconds > 0.0f ? std::exp (-1.0f / (tauSeconds * rateHz)) : 0.0f;
}

//==============================================================================
/** xorshift32 — deterministic, allocation-free noise for jitter, debris and chatter. */
struct Rng
{
    explicit Rng (uint32_t seed = 0x9E3779B9u) noexcept : state (seed != 0 ? seed : 1u) {}
    uint32_t next() noexcept { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    float uni() noexcept     { return (float) (next() >> 8) * (1.0f / 16777216.0f); } // [0, 1)
    float bi() noexcept      { return uni() * 2.0f - 1.0f; }                          // [-1, 1)
    uint32_t state;
};

//==============================================================================
struct OnePoleSmoother
{
    void setTime (float tauSeconds, float rateHz) noexcept { a = onePoleCoeff (tauSeconds, rateHz); }
    void reset (float v) noexcept                          { y = v; }
    float process (float target) noexcept                  { y = target + a * (y - target); return y; }
    float y = 0.0f, a = 0.0f;
};

//==============================================================================
/** Zero-delay-feedback one-pole (TPT). Safe under per-block coefficient changes. */
struct OnePoleTpt
{
    void set (float cutoffHz, float fs) noexcept
    {
        const float g = std::tan (kPi * std::clamp (cutoffHz, 1.0f, 0.49f * fs) / fs);
        G = g / (1.0f + g);
    }
    float lp (float x) noexcept { const float v = (x - s) * G; const float y = v + s; s = y + v; return y; }
    float hp (float x) noexcept { return x - lp (x); }
    void reset() noexcept       { s = 0.0f; }
    float G = 0.0f, s = 0.0f;
};

//==============================================================================
/** Simper/Zavalishin TPT state-variable filter. Modulation-stable, simultaneous LP/BP/HP. */
struct SvfTpt
{
    struct Out { float lp, bp, hp; };

    void set (float cutoffHz, float Q, float fs) noexcept
    {
        const float g = std::tan (kPi * std::clamp (cutoffHz, 5.0f, 0.49f * fs) / fs);
        k  = 1.0f / std::max (Q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    Out process (float v0) noexcept
    {
        const float v3 = v0 - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        return { v2, v1, v0 - k * v1 - v2 };
    }

    void reset() noexcept { ic1eq = ic2eq = 0.0f; }

    float k = 1.4142f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1eq = 0.0f, ic2eq = 0.0f;
};

//==============================================================================
// 8-tap windowed-sinc fractional read, 32 phases. A first-order Thiran allpass is
// unity magnitude, but its coefficient wraps once per sample of delay and that wrap
// clicks under this tank's modulation. The kernel below is stateless, scaled so
// |H| <= 1 (float32), and loses about 0.06 dB at 12 kHz where Catmull-Rom loses 1.1 dB.
// Tap 3 is the integer sample at phase 0. h[k] weights x[n - i + 3 - k].
// Adjacent phases are interpolated, including the wrap onto the next sample,
// so a moving delay does not stair-step once per 1/32 of a sample.
inline constexpr int kSincPhases = 32;
inline constexpr int kSincTaps = 8;
inline constexpr float kSincTable[kSincPhases][kSincTaps] = {
    { 0.00000000f, -0.00000000f, 0.00000000f, 0.99850398f, 0.00000000f, -0.00000000f, 0.00000000f, -0.00000000f },
    { -0.00023014f, 0.00362029f, -0.01943604f, 0.99047846f, 0.03195092f, -0.01018078f, 0.00247704f, -0.00017577f },
    { -0.00045065f, 0.00705377f, -0.03732081f, 0.97976791f, 0.06531786f, -0.02046625f, 0.00495265f, -0.00035051f },
    { -0.00066005f, 0.01028098f, -0.05364213f, 0.96644265f, 0.09997683f, -0.03077827f, 0.00740673f, -0.00052276f },
    { -0.00085696f, 0.01328474f, -0.06839495f, 0.95058257f, 0.13579751f, -0.04103697f, 0.00981916f, -0.00069110f },
    { -0.00104017f, 0.01605010f, -0.08158115f, 0.93227670f, 0.17264383f, -0.05116106f, 0.01216986f, -0.00085413f },
    { -0.00120859f, 0.01856438f, -0.09320936f, 0.91162277f, 0.21037448f, -0.06106820f, 0.01443896f, -0.00101046f },
    { -0.00136126f, 0.02081709f, -0.10329478f, 0.88872670f, 0.24884348f, -0.07067538f, 0.01660689f, -0.00115876f },
    { -0.00149738f, 0.02279994f, -0.11185895f, 0.86370215f, 0.28790072f, -0.07989925f, 0.01865449f, -0.00129773f },
    { -0.00161629f, 0.02450676f, -0.11892949f, 0.83666994f, 0.32739258f, -0.08865653f, 0.02056315f, -0.00142614f },
    { -0.00171746f, 0.02593352f, -0.12453989f, 0.80775756f, 0.36716253f, -0.09686436f, 0.02231489f, -0.00154281f },
    { -0.00180052f, 0.02707819f, -0.12872921f, 0.77709863f, 0.40705167f, -0.10444068f, 0.02389252f, -0.00164663f },
    { -0.00186521f, 0.02794076f, -0.13154181f, 0.74483230f, 0.44689938f, -0.11130460f, 0.02527974f, -0.00173658f },
    { -0.00191144f, 0.02852312f, -0.13302703f, 0.71110268f, 0.48654394f, -0.11737679f, 0.02646121f, -0.00181172f },
    { -0.00193924f, 0.02882901f, -0.13323891f, 0.67605829f, 0.52582312f, -0.12257980f, 0.02742271f, -0.00187120f },
    { -0.00194877f, 0.02886393f, -0.13223587f, 0.63985142f, 0.56457479f, -0.12683849f, 0.02815124f, -0.00191428f },
    { -0.00194031f, 0.02863507f, -0.13008034f, 0.60263757f, 0.60263757f, -0.13008034f, 0.02863507f, -0.00194031f },
    { -0.00191428f, 0.02815124f, -0.12683849f, 0.56457479f, 0.63985142f, -0.13223587f, 0.02886393f, -0.00194877f },
    { -0.00187120f, 0.02742271f, -0.12257980f, 0.52582312f, 0.67605829f, -0.13323891f, 0.02882901f, -0.00193924f },
    { -0.00181172f, 0.02646121f, -0.11737679f, 0.48654394f, 0.71110268f, -0.13302703f, 0.02852312f, -0.00191144f },
    { -0.00173658f, 0.02527974f, -0.11130460f, 0.44689938f, 0.74483230f, -0.13154181f, 0.02794076f, -0.00186521f },
    { -0.00164663f, 0.02389252f, -0.10444068f, 0.40705167f, 0.77709863f, -0.12872921f, 0.02707819f, -0.00180052f },
    { -0.00154281f, 0.02231489f, -0.09686436f, 0.36716253f, 0.80775756f, -0.12453989f, 0.02593352f, -0.00171746f },
    { -0.00142614f, 0.02056315f, -0.08865653f, 0.32739258f, 0.83666994f, -0.11892949f, 0.02450676f, -0.00161629f },
    { -0.00129773f, 0.01865449f, -0.07989925f, 0.28790072f, 0.86370215f, -0.11185895f, 0.02279994f, -0.00149738f },
    { -0.00115876f, 0.01660689f, -0.07067538f, 0.24884348f, 0.88872670f, -0.10329478f, 0.02081709f, -0.00136126f },
    { -0.00101046f, 0.01443896f, -0.06106820f, 0.21037448f, 0.91162277f, -0.09320936f, 0.01856438f, -0.00120859f },
    { -0.00085413f, 0.01216986f, -0.05116106f, 0.17264383f, 0.93227670f, -0.08158115f, 0.01605010f, -0.00104017f },
    { -0.00069110f, 0.00981916f, -0.04103697f, 0.13579751f, 0.95058257f, -0.06839495f, 0.01328474f, -0.00085696f },
    { -0.00052276f, 0.00740673f, -0.03077827f, 0.09997683f, 0.96644265f, -0.05364213f, 0.01028098f, -0.00066005f },
    { -0.00035051f, 0.00495265f, -0.02046625f, 0.06531786f, 0.97976791f, -0.03732081f, 0.00705377f, -0.00045065f },
    { -0.00017577f, 0.00247704f, -0.01018078f, 0.03195092f, 0.99047846f, -0.01943604f, 0.00362029f, -0.00023014f },
};

//==============================================================================
/** Power-of-two circular delay. tap() is Catmull-Rom; readSinc() is the flat fractional read.
    Convention: read taps for x[n] BEFORE pushing x[n]; tap(d) returns x[n - d]. */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 4)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = (uint32_t) size - 1u;
        writePos = 0;
    }

    void reset() noexcept { clear(); writePos = 0; }
    void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }
    void push (float x) noexcept { buffer[writePos] = x; writePos = (writePos + 1u) & mask; }
    float maxDelay() const noexcept { return (float) mask - 3.0f; }

    /** Fractional read with |H| <= 1. Same delay convention as tap(). */
    float readSinc (float d) const noexcept
    {
        const float hi = std::max (4.0f, maxDelay() - 4.0f);
        d = std::clamp (d, 4.0f, hi);
        const int i = (int) d;
        const float frac = d - (float) i;
        const float phaseF = frac * (float) kSincPhases;
        int phase = (int) phaseF;
        if (phase >= kSincPhases) phase = kSincPhases - 1;
        if (phase < 0) phase = 0;
        const float a = sincAt (i, phase);
        const float t = phaseF - (float) phase;
        if (t <= 1.0e-6f)
            return a;
        // The next table row past the last one is the integer kernel, one sample later.
        const float b = phase + 1 < kSincPhases ? sincAt (i, phase + 1) : sincAt (i + 1, 0);
        return a + t * (b - a);
    }

private:
    float sincAt (int delay, int phase) const noexcept
    {
        const float* h = kSincTable[phase];
        const uint32_t idx = writePos - (uint32_t) delay;
        return h[0] * buffer[(idx + 3u) & mask]
             + h[1] * buffer[(idx + 2u) & mask]
             + h[2] * buffer[(idx + 1u) & mask]
             + h[3] * buffer[idx & mask]
             + h[4] * buffer[(idx - 1u) & mask]
             + h[5] * buffer[(idx - 2u) & mask]
             + h[6] * buffer[(idx - 3u) & mask]
             + h[7] * buffer[(idx - 4u) & mask];
    }

public:

    float tapInt (int d) const noexcept { return buffer[(writePos - (uint32_t) d) & mask]; }

    /** The four samples around a fractional tap and its fraction, for callers that interpolate
        many lines at once (see catmullRom). Same clamping and convention as tap(). */
    void fetch (float d, float& p0, float& p1, float& p2, float& p3, float& frac) const noexcept
    {
        d = std::clamp (d, 2.0f, maxDelay());
        const int i = (int) d;
        frac = d - (float) i;
        const uint32_t idx = writePos - (uint32_t) i;
        p0 = buffer[(idx + 1u) & mask];
        p1 = buffer[idx & mask];
        p2 = buffer[(idx - 1u) & mask];
        p3 = buffer[(idx - 2u) & mask];
    }

    float tap (float d) const noexcept
    {
        d = std::clamp (d, 2.0f, maxDelay());
        const int i   = (int) d;
        const float f = d - (float) i;
        const uint32_t idx = writePos - (uint32_t) i; // x[n - i]
        const float p0 = buffer[(idx + 1u) & mask];   // x[n - i + 1]
        const float p1 = buffer[idx & mask];
        const float p2 = buffer[(idx - 1u) & mask];
        const float p3 = buffer[(idx - 2u) & mask];
        const float c1 = 0.5f * (p2 - p0);
        const float c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
        const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
        return ((c3 * f + c2) * f + c1) * f + p1;
    }

private:
    std::vector<float> buffer;
    uint32_t mask = 0, writePos = 0;
};

/** Catmull-Rom through four samples (p1 is the one at the integer tap, f runs towards p2). */
inline float catmullRom (float p0, float p1, float p2, float p3, float f) noexcept
{
    const float c1 = 0.5f * (p2 - p0);
    const float c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
    return ((c3 * f + c2) * f + c1) * f + p1;
}

//==============================================================================
/** Schroeder allpass used for diffusion. */
class Allpass
{
public:
    void prepare (int lengthSamples) { buffer.assign ((size_t) std::max (1, lengthSamples), 0.0f); pos = 0; }
    void reset() noexcept            { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }
    void setGain (float g) noexcept  { gain = g; }

    float process (float x) noexcept
    {
        const float d = buffer[pos];
        const float v = x + gain * d;
        const float y = d - gain * v;
        buffer[pos] = v;
        if (++pos >= buffer.size()) pos = 0;
        return y;
    }

private:
    std::vector<float> buffer;
    size_t pos = 0;
    float gain = 0.5f;
};
} // namespace rb::dsp
