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
// 8-tap fractional read, 32 phases: 7th-order Lagrange interpolation. A first-order Thiran
// allpass is unity magnitude, but its coefficient wraps once per sample of delay and that wrap
// clicks under this tank's modulation. Lagrange of odd order with the delay in its central range
// (3..4 here) is passive: |H| <= 1 at every frequency and fraction, and exactly 1 at DC, so a
// frozen loop holds its level and the decay knob is the decay. Worst case it is 0.2 dB down at
// 12 kHz (Catmull-Rom: 1.1 dB). Tap 3 is the integer sample at phase 0. h[k] weights x[n - i + 3 - k].
// Adjacent phases are interpolated, including the wrap onto the next sample, so a moving delay
// does not stair-step once per 1/32 of a sample; a blend of two passive kernels is passive too.
inline constexpr int kSincPhases = 32;
inline constexpr int kSincTaps = 8;
inline constexpr float kSincTable[kSincPhases][kSincTaps] = {
    { -0.00000000f, 0.00000000f, -0.00000000f, 1.00000000f, 0.00000000f, -0.00000000f, 0.00000000f, -0.00000000f },
    { -0.00029186f, 0.00304883f, -0.01801580f, 0.99086904f, 0.03196352f, -0.00943685f, 0.00208604f, -0.00022292f },
    { -0.00057093f, 0.00593422f, -0.03455813f, 0.97914707f, 0.06527647f, -0.01895123f, 0.00416658f, -0.00044406f },
    { -0.00083542f, 0.00864098f, -0.04962391f, 0.96490931f, 0.09981820f, -0.02847273f, 0.00622522f, -0.00066165f },
    { -0.00108370f, 0.01115575f, -0.06321594f, 0.94823912f, 0.13546273f, -0.03792956f, 0.00824556f, -0.00087395f },
    { -0.00131432f, 0.01346707f, -0.07534278f, 0.92922766f, 0.17207920f, -0.04724886f, 0.01021129f, -0.00107924f },
    { -0.00152601f, 0.01556526f, -0.08601854f, 0.90797345f, 0.20953233f, -0.05635697f, 0.01210631f, -0.00127584f },
    { -0.00171763f, 0.01744246f, -0.09526267f, 0.88458197f, 0.24768295f, -0.06517972f, 0.01391477f, -0.00146212f },
    { -0.00188828f, 0.01909256f, -0.10309982f, 0.85916519f, 0.28638840f, -0.07364273f, 0.01562119f, -0.00163651f },
    { -0.00203716f, 0.02051115f, -0.10955957f, 0.83184118f, 0.32550307f, -0.08167168f, 0.01721051f, -0.00179750f },
    { -0.00216370f, 0.02169550f, -0.11467623f, 0.80273359f, 0.36487890f, -0.08919262f, 0.01866822f, -0.00194366f },
    { -0.00226747f, 0.02264449f, -0.11848861f, 0.77197122f, 0.40436588f, -0.09613226f, 0.01998043f, -0.00207367f },
    { -0.00234821f, 0.02335855f, -0.12103978f, 0.73968753f, 0.44381252f, -0.10241827f, 0.02113393f, -0.00218627f },
    { -0.00240584f, 0.02383964f, -0.12237683f, 0.70602017f, 0.48306643f, -0.10797955f, 0.02211629f, -0.00228031f },
    { -0.00244040f, 0.02409114f, -0.12255060f, 0.67111044f, 0.52197478f, -0.11274655f, 0.02291597f, -0.00235477f },
    { -0.00245213f, 0.02411783f, -0.12161544f, 0.63510284f, 0.56038485f, -0.11665154f, 0.02352233f, -0.00240873f },
    { -0.00244141f, 0.02392578f, -0.11962891f, 0.59814453f, 0.59814453f, -0.11962891f, 0.02392578f, -0.00244141f },
    { -0.00240873f, 0.02352233f, -0.11665154f, 0.56038485f, 0.63510284f, -0.12161544f, 0.02411783f, -0.00245213f },
    { -0.00235477f, 0.02291597f, -0.11274655f, 0.52197478f, 0.67111044f, -0.12255060f, 0.02409114f, -0.00244040f },
    { -0.00228031f, 0.02211629f, -0.10797955f, 0.48306643f, 0.70602017f, -0.12237683f, 0.02383964f, -0.00240584f },
    { -0.00218627f, 0.02113393f, -0.10241827f, 0.44381252f, 0.73968753f, -0.12103978f, 0.02335855f, -0.00234821f },
    { -0.00207367f, 0.01998043f, -0.09613226f, 0.40436588f, 0.77197122f, -0.11848861f, 0.02264449f, -0.00226747f },
    { -0.00194366f, 0.01866822f, -0.08919262f, 0.36487890f, 0.80273359f, -0.11467623f, 0.02169550f, -0.00216370f },
    { -0.00179750f, 0.01721051f, -0.08167168f, 0.32550307f, 0.83184118f, -0.10955957f, 0.02051115f, -0.00203716f },
    { -0.00163651f, 0.01562119f, -0.07364273f, 0.28638840f, 0.85916519f, -0.10309982f, 0.01909256f, -0.00188828f },
    { -0.00146212f, 0.01391477f, -0.06517972f, 0.24768295f, 0.88458197f, -0.09526267f, 0.01744246f, -0.00171763f },
    { -0.00127584f, 0.01210631f, -0.05635697f, 0.20953233f, 0.90797345f, -0.08601854f, 0.01556526f, -0.00152601f },
    { -0.00107924f, 0.01021129f, -0.04724886f, 0.17207920f, 0.92922766f, -0.07534278f, 0.01346707f, -0.00131432f },
    { -0.00087395f, 0.00824556f, -0.03792956f, 0.13546273f, 0.94823912f, -0.06321594f, 0.01115575f, -0.00108370f },
    { -0.00066165f, 0.00622522f, -0.02847273f, 0.09981820f, 0.96490931f, -0.04962391f, 0.00864098f, -0.00083542f },
    { -0.00044406f, 0.00416658f, -0.01895123f, 0.06527647f, 0.97914707f, -0.03455813f, 0.00593422f, -0.00057093f },
    { -0.00022292f, 0.00208604f, -0.00943685f, 0.03196352f, 0.99086904f, -0.01801580f, 0.00304883f, -0.00029186f }
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

    /** Fractional read with |H| <= 1 and unity at DC. Same delay convention as tap(). */
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
