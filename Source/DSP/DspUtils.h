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
/** Power-of-two circular delay with Catmull-Rom fractional reads.
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

    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }
    void push (float x) noexcept { buffer[writePos] = x; writePos = (writePos + 1u) & mask; }
    float maxDelay() const noexcept { return (float) mask - 3.0f; }

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
