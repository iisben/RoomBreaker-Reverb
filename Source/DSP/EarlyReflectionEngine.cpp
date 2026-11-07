#include "EarlyReflectionEngine.h"

namespace rb::dsp
{
namespace
{
    struct Vec3 { float x, y, z; };

    constexpr float kSpeedOfSound = 343.0f;
    constexpr float kLevel        = 0.9f;
    constexpr float kMaxDelaySec  = 0.40f;
    constexpr float kDiffuserMsL[] = { 4.7f, 7.3f, 11.9f, 17.3f };
    constexpr float kDiffuserMsR[] = { 5.3f, 8.1f, 12.7f, 16.1f };

    // x: West(0)→East(Lx), y: South(0)→North(Ly), z: Floor(0)→Ceiling(Lz)
    Vec3 reflect (Vec3 p, int wall, const Vec3& dims) noexcept
    {
        switch (wall)
        {
            case West:    p.x = -p.x;               break;
            case East:    p.x = 2.0f * dims.x - p.x; break;
            case South:   p.y = -p.y;               break;
            case North:   p.y = 2.0f * dims.y - p.y; break;
            case Floor:   p.z = -p.z;               break;
            case Ceiling: p.z = 2.0f * dims.z - p.z; break;
            default: break;
        }
        return p;
    }

    float distance (const Vec3& a, const Vec3& b) noexcept
    {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt (dx * dx + dy * dy + dz * dz);
    }
}

void EarlyReflectionEngine::prepare (double sampleRate)
{
    fs = (float) sampleRate;
    line.prepare ((int) std::ceil (kMaxDelaySec * fs));

    for (int s = 0; s < kDiffuserStages; ++s)
    {
        diffuserL[(size_t) s].prepare ((int) (kDiffuserMsL[s] * 1.0e-3f * fs));
        diffuserR[(size_t) s].prepare ((int) (kDiffuserMsR[s] * 1.0e-3f * fs));
    }

    buildTopology();
    currentSize = -1.0f;
    setGeometry (1.0f);
    reset();
}

void EarlyReflectionEngine::reset()
{
    line.reset();
    for (auto& d : diffuserL) d.reset();
    for (auto& d : diffuserR) d.reset();

    for (auto& t : taps)
    {
        t.delay = t.baseDelay;
        t.delayInc = t.noise = 0.0f;
        t.gL = t.gR = t.dL = t.dR = 0.0f;
        t.gLInc = t.gRInc = t.dLInc = t.dRInc = 0.0f;
    }
}

void EarlyReflectionEngine::buildTopology() noexcept
{
    int t = 0;
    for (int w = 0; w < kNumWalls; ++w)                        // first order
        taps[(size_t) t++] = Tap { w, -1 };

    for (int a = 0; a < kNumWalls; ++a)                        // second order, perpendicular walls
        for (int b = a + 1; b < kNumWalls; ++b)
            if (axisOf (a) != axisOf (b))
                taps[(size_t) t++] = Tap { a, b };

    for (int a = 0; a < kNumWalls; ++a)                        // second order, opposite walls (both orders)
        for (int b = a + 1; b < kNumWalls; ++b)
            if (axisOf (a) == axisOf (b))
            {
                taps[(size_t) t++] = Tap { a, b };
                taps[(size_t) t++] = Tap { b, a };
            }
}

void EarlyReflectionEngine::setGeometry (float sizeScale) noexcept
{
    if (std::abs (sizeScale - currentSize) < 1.0e-4f)
        return;
    currentSize = sizeScale;

    const Vec3 dims { 14.0f * sizeScale, 10.0f * sizeScale, 5.5f * sizeScale };
    const Vec3 src  { 0.60f * dims.x, 0.72f * dims.y, 0.40f * dims.z };
    const Vec3 lis  { 0.44f * dims.x, 0.30f * dims.y, 0.33f * dims.z };
    const float direct = distance (src, lis);
    const float maxDelay = line.maxDelay() - 64.0f;

    for (auto& t : taps)
    {
        Vec3 img = reflect (src, t.wallA, dims);
        if (t.wallB >= 0)
            img = reflect (img, t.wallB, dims);

        const float d = distance (img, lis);
        t.baseDelay = std::clamp ((d - direct) / kSpeedOfSound * fs + 2.0f, 2.0f, maxDelay);
        t.geoGain   = direct / std::max (d, 0.1f);

        const float dx = img.x - lis.x, dy = img.y - lis.y;
        const float pan = dx / std::max (std::sqrt (dx * dx + dy * dy), 1.0e-3f); // -1 West .. +1 East
        const float theta = (pan + 1.0f) * 0.25f * kPi;
        t.panL = std::cos (theta);
        t.panR = std::sin (theta);
    }
}

void EarlyReflectionEngine::updateControl (const WallArray& I, const MaterialProfile& m, int n) noexcept
{
    WallArray R {}, spec {}, damage {};
    float meanDamage = 0.0f;
    for (int w = 0; w < kNumWalls; ++w)
    {
        R[w]      = m.reflectivity * (1.0f - breachAmount (I[w]));
        spec[w]   = std::pow (clamp01 (I[w]), m.scatterExponent);
        damage[w] = 1.0f - I[w];
        meanDamage += damage[w] / (float) kNumWalls;
    }

    const float invN = 1.0f / (float) std::max (n, 1);
    const float jitterScale = m.jitterMaxMs * 1.0e-3f * fs;
    const float maxDelay = line.maxDelay() - 1.0f;

    for (auto& t : taps)
    {
        const bool second = t.wallB >= 0;
        const float r   = R[t.wallA]    * (second ? R[t.wallB]    : 1.0f);
        const float s   = spec[t.wallA] * (second ? spec[t.wallB] : 1.0f);
        const float dmg = std::max (damage[t.wallA], second ? damage[t.wallB] : 0.0f);

        // Micro-jitter: low-passed random walk (~5 Hz), depth grows with cracking.
        t.noise += (rng.bi() - t.noise) * 0.06f;
        const float depth = jitterScale * std::pow (dmg, 1.5f);
        const float targetDelay = std::min (t.baseDelay + depth * std::max (0.0f, 1.0f + 3.0f * t.noise), maxDelay);
        t.delayInc = (targetDelay - t.delay) * invN;

        const float gSpec = kLevel * t.geoGain * r * s;
        const float gDiff = kLevel * t.geoGain * r * std::sqrt (std::max (0.0f, 1.0f - s * s));
        t.gLInc = (gSpec * t.panL - t.gL) * invN;
        t.gRInc = (gSpec * t.panR - t.gR) * invN;
        t.dLInc = (gDiff * t.panL - t.dL) * invN;
        t.dRInc = (gDiff * t.panR - t.dR) * invN;
    }

    // Shattered surfaces smear harder.
    const float diffusion = 0.45f + 0.3f * meanDamage;
    for (auto& d : diffuserL) d.setGain (diffusion);
    for (auto& d : diffuserR) d.setGain (diffusion);
}

void EarlyReflectionEngine::process (const float* in, float* outL, float* outR, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        float sL = 0.0f, sR = 0.0f, dL = 0.0f, dR = 0.0f;

        for (auto& t : taps)
        {
            const float v = line.tap (t.delay);
            sL += v * t.gL;  sR += v * t.gR;
            dL += v * t.dL;  dR += v * t.dR;
            t.delay += t.delayInc;
            t.gL += t.gLInc; t.gR += t.gRInc;
            t.dL += t.dLInc; t.dR += t.dRInc;
        }

        line.push (in[i]);

        for (auto& ap : diffuserL) dL = ap.process (dL);
        for (auto& ap : diffuserR) dR = ap.process (dR);

        outL[i] = sL + dL;
        outR[i] = sR + dR;
    }
}
} // namespace rb::dsp
