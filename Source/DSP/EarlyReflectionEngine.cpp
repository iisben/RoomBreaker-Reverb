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
    // At 2x and 4x a linear read loses under 0.5 dB at 10 kHz, and the engine has 2-4x the samples to get through.
    cubicTaps = fs < 80000.0f;
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

    for (int k = 0; k < kNumTaps; ++k)
    {
        auto& t = taps[(size_t) k];
        tapDelay[k] = t.delayTarget = std::round (t.baseDelay);
        tapDelayInc[k] = 0.0f;
        t.whole = wholeDelay[k] = (int) tapDelay[k];
        t.still = false;
        t.noise = t.jitter = 0.0f;
    }
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < kNumTaps; ++k)
            gain[c][k] = gainInc[c][k] = gainTarget[c][k] = 0.0f;
    inShelf.reset();
    for (auto& v : bounceS) v = 0.0f;
    gainMaterial = nullptr;   // forces the first updateControl to compute everything
    geometryDirty = true;
    gainsSettled = false;
    steady = false;
    resizing = false;
    diffuseHold = 0;
    bounceHf = bounceHfTarget = 1.0f;
    bounceG = bounceGTarget = 0.0f;
    bounceHfInc = bounceGInc = 0.0f;
}

void EarlyReflectionEngine::buildTopology() noexcept
{
    int t = 0;
    auto add = [&] (int a, int b) { taps[(size_t) t] = Tap {}; taps[(size_t) t].wallA = a; taps[(size_t) t].wallB = b; ++t; };

    for (int w = 0; w < kNumWalls; ++w)                        // first order: taps [0, kFirstOrder)
        add (w, -1);

    for (int a = 0; a < kNumWalls; ++a)                        // second order, perpendicular walls
        for (int b = a + 1; b < kNumWalls; ++b)
            if (axisOf (a) != axisOf (b))
                add (a, b);

    for (int a = 0; a < kNumWalls; ++a)                        // second order, opposite walls (both orders)
        for (int b = a + 1; b < kNumWalls; ++b)
            if (axisOf (a) == axisOf (b))
            {
                add (a, b);
                add (b, a);
            }
}

void EarlyReflectionEngine::setGeometry (float sizeScale) noexcept
{
    if (std::abs (sizeScale - currentSize) < 1.0e-4f)
        return;
    currentSize = sizeScale;
    geometryDirty = true;
    resizing = true;

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
    const float invN = 1.0f / (float) std::max (n, 1);
    const float maxDelay = line.maxDelay() - 1.0f;

    // Tap gains and jitter depths depend only on the walls, the material and the geometry:
    // their pow/sqrt are paid only on the ticks where one of those has moved.
    bool dirty = geometryDirty || gainMaterial != &m;
    for (int w = 0; w < kNumWalls && ! dirty; ++w)
        dirty = std::abs (I[(size_t) w] - gainIntegrity[(size_t) w]) > 2.0e-4f;

    if (dirty)
    {
        geometryDirty = false;
        gainMaterial = &m;
        gainIntegrity = I;
        gainsSettled = false;

        WallArray R {}, spec {}, wear {};
        float meanDamage = 0.0f;
        for (int w = 0; w < kNumWalls; ++w)
        {
            const size_t i = (size_t) w;
            const float damage = 1.0f - I[i];
            R[i]    = m.reflectivity * (1.0f - breachAmount (I[i]));
            spec[i] = I[i] >= 1.0f ? 1.0f : std::pow (clamp01 (I[i]), m.scatterExponent);
            wear[i] = damage > 0.0f ? damage * std::sqrt (damage) : 0.0f;   // damage^1.5
            meanDamage += damage / (float) kNumWalls;
        }

        const float jitterScale = m.jitterMaxMs * 1.0e-3f * fs;
        for (int k = 0; k < kNumTaps; ++k)
        {
            auto& t = taps[(size_t) k];
            const bool second = t.wallB >= 0;
            const size_t a = (size_t) t.wallA, b = (size_t) (second ? t.wallB : t.wallA);
            const float r = R[a] * (second ? R[b] : 1.0f);
            const float s = spec[a] * (second ? spec[b] : 1.0f);
            t.jitter = jitterScale * std::max (wear[a], second ? wear[b] : 0.0f);

            const float gSpec = kLevel * t.geoGain * r * s;
            const float gDiff = s >= 1.0f ? 0.0f : kLevel * t.geoGain * r * std::sqrt (std::max (0.0f, 1.0f - s * s));
            gainTarget[0][k] = gSpec * t.panL;  gainTarget[1][k] = gSpec * t.panR;
            gainTarget[2][k] = gDiff * t.panL;  gainTarget[3][k] = gDiff * t.panR;
            for (int c = 0; c < 4; ++c)
                gainInc[c][k] = (gainTarget[c][k] - gain[c][k]) * invN;
        }

        // Shattered surfaces smear harder.
        const float diffusion = 0.45f + 0.3f * meanDamage;
        for (auto& d : diffuserL) d.setGain (diffusion);
        for (auto& d : diffuserR) d.setGain (diffusion);

        // What one bounce takes off the top: the material's shelf, half as deep as 50 ms in the tank.
        // Corner and depth are both ramped: stepping a filter's corner shifts the phase of everything
        // passing through it at once, which is heard as a tick even on a low note.
        const float gc = std::tan (kPi * std::clamp (m.hfDampingHz, 1.0f, 0.49f * fs) / fs);
        bounceGTarget = gc / (1.0f + gc);
        if (bounceG <= 0.0f) bounceG = bounceGTarget;
        bounceGInc = (bounceGTarget - bounceG) * invN;
        bounceHfTarget = std::sqrt (std::clamp (m.hfShelfGain, 0.05f, 1.0f));
        bounceHfInc = (bounceHfTarget - bounceHf) * invN;
    }
    else if (! gainsSettled)
    {
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < kNumTaps; ++k)
            {
                gain[c][k] = gainTarget[c][k];   // last tick's ramp has landed
                gainInc[c][k] = 0.0f;
            }
        bounceHf = bounceHfTarget;
        bounceG = bounceGTarget;
        bounceHfInc = bounceGInc = 0.0f;
        gainsSettled = true;
    }

    const bool moving = resizing;
    resizing = false;
    for (int k = 0; k < kNumTaps; ++k)
    {
        auto& t = taps[(size_t) k];
        if (tapDelayInc[k] != 0.0f)
            tapDelay[k] = t.delayTarget;      // last tick's ramp has landed

        // An intact wall reflects from a fixed place: once the room has stopped changing size the
        // tap settles on a whole sample. While it is changing, the tap follows the geometry exactly.
        float target = moving ? t.baseDelay : std::round (t.baseDelay);
        if (t.jitter > 0.0f)
        {
            // Micro-jitter: low-passed random walk (~5 Hz), depth grows with cracking.
            t.noise += (rng.bi() - t.noise) * 0.06f;
            target = std::min (t.baseDelay + t.jitter * std::max (0.0f, 1.0f + 3.0f * t.noise), maxDelay);
        }
        t.delayTarget = target;
        tapDelayInc[k] = (target - tapDelay[k]) * invN;
        t.still = tapDelayInc[k] == 0.0f && t.jitter <= 0.0f;
        t.whole = (int) tapDelay[k];
    }

    steady = gainsSettled;
    bool diffuse = false;
    for (int k = 0; k < kNumTaps; ++k)
    {
        steady = steady && taps[(size_t) k].still;
        wholeDelay[k] = taps[(size_t) k].whole;
        diffuse = diffuse || gain[2][k] != 0.0f || gain[3][k] != 0.0f || gainTarget[2][k] != 0.0f || gainTarget[3][k] != 0.0f;
    }
    // Intact walls scatter nothing, so the diffusers only run while something feeds them, plus 1.5 s of ring-out.
    diffuseHold = diffuse ? (int) (1.5f * fs) : std::max (0, diffuseHold - n);
}

void EarlyReflectionEngine::process (const float* in, float* outL, float* outR, int n) noexcept
{

    static_assert (kNumTaps == 24 && kFirstOrder == 6, "the tap mix below is laid out for 6 + 18 taps");

    for (int i = 0; i < n; ++i)
    {
        alignas (32) float v[kNumTaps];
        alignas (16) float first[4], second[4];

        // Read every tap. Nothing moving (the usual case): whole-sample reads. Walls cracking or
        // the room resizing: cubic reads, fetched first and interpolated together.
        if (steady)
        {
            for (int k = 0; k < kNumTaps; ++k)
                v[k] = line.tapInt (wholeDelay[k]);
        }
        else if (! cubicTaps)
        {
            for (int k = 0; k < kNumTaps; ++k)
            {
                const float d = std::clamp (tapDelay[k], 2.0f, line.maxDelay());
                const int whole = (int) d;
                const float a = line.tapInt (whole);
                v[k] = a + (d - (float) whole) * (line.tapInt (whole + 1) - a);
                tapDelay[k] += tapDelayInc[k];
            }
        }
        else
        {
            alignas (32) float p0[kNumTaps], p1[kNumTaps], p2[kNumTaps], p3[kNumTaps], fr[kNumTaps];
            for (int k = 0; k < kNumTaps; ++k)
                line.fetch (tapDelay[k], p0[k], p1[k], p2[k], p3[k], fr[k]);
            for (int k = 0; k < kNumTaps; ++k)
            {
                v[k] = catmullRom (p0[k], p1[k], p2[k], p3[k], fr[k]);
                tapDelay[k] += tapDelayInc[k];
            }
        }

        // Four mixes (specular L, R / diffuse L, R), first- and second-order images apart. Each is
        // summed as a tree rather than one long chain, so the additions overlap and vectorise.
        for (int c = 0; c < 4; ++c)
        {
            const float* g = gain[c];
            first[c] = (v[0] * g[0] + v[1] * g[1]) + (v[2] * g[2] + v[3] * g[3]) + (v[4] * g[4] + v[5] * g[5]);
            float lane[8];
            for (int j = 0; j < 8; ++j)
                lane[j] = v[6 + j] * g[6 + j] + v[14 + j] * g[14 + j];
            second[c] = ((lane[0] + lane[1]) + (lane[2] + lane[3])) + ((lane[4] + lane[5]) + (lane[6] + lane[7]))
                        + (v[22] * g[22] + v[23] * g[23]);
        }

        if (! gainsSettled)   // walls moving: ramp the gains towards this tick's targets
            for (int c = 0; c < 4; ++c)
                for (int k = 0; k < kNumTaps; ++k)
                    gain[c][k] += gainInc[c][k];

        // One bounce's treble loss on the way in; a second one on the images that bounced twice.
        bounceHf += bounceHfInc;
        bounceG += bounceGInc;
        const float hf = bounceHf, bg = bounceG;
        inShelf.G = bg;
        const float lp = inShelf.lp (in[i]);
        line.push (lp + hf * (in[i] - lp));
        for (int j = 0; j < 4; ++j)
        {
            const float sv = (second[j] - bounceS[j]) * bg;
            const float low = sv + bounceS[j];
            bounceS[j] = low + sv;
            first[j] += low + hf * (second[j] - low);
        }

        float dL = first[2], dR = first[3];
        if (diffuseHold > 0)
        {
            for (auto& ap : diffuserL) dL = ap.process (dL);
            for (auto& ap : diffuserR) dR = ap.process (dR);
        }

        outL[i] = first[0] + dL;
        outR[i] = first[1] + dR;
    }
}
} // namespace rb::dsp
