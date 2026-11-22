#include "FractureGranulator.h"

namespace rb::dsp
{
void FractureGranulator::prepare (double sampleRate)
{
    fs = (float) sampleRate;
    reset();
}

void FractureGranulator::reset() noexcept
{
    bufL.fill (0.0f);
    bufR.fill (0.0f);
    for (auto& g : grains) g = Grain {};
    write = 0;
    hold = 0;
    spray = (int) std::lround (0.04 * (double) fs);
    env = prev = 0.0f;
    rng = Rng (0x6A09E667u);
}

void FractureGranulator::spawn (float level) noexcept
{
    if (level < 1.0e-4f || amount < 1.0e-4f)
        return;

    Grain* slot = &grains[0];
    for (auto& g : grains)
    {
        if (! g.on) { slot = &g; break; }
        if (g.phase > slot->phase) slot = &g;
    }

    const float u = rng.uni();
    float semis;
    if (brittle > 0.45f)
        semis = (2.0f + 10.0f * amount) * (0.30f + 0.70f * u);             // ice, glass: up
    else if (metal > 0.35f)
        semis = -11.0f * amount * u + (u - 0.40f) * 5.0f * amount;         // steel: mostly down
    else
        semis = -(3.0f + 11.0f * amount) * (0.35f + 0.65f * u);            // rubble: down

    const float ratio = std::exp2 (std::clamp (semis, -14.0f, 14.0f) / 12.0f);
    const float lenMs = (brittle > 0.45f ? 9.0f : metal > 0.35f ? 26.0f : 16.0f) * (0.65f + 0.70f * rng.uni());
    float len = std::clamp (lenMs, 6.0f, 42.0f) * 1.0e-3f * fs;

    float behind = 6.0f + rng.uni() * std::min (0.025f * fs, 0.15f * (float) kSize);
    if (ratio > 1.0f)
    {
        const float budget = 0.45f * (float) kSize;
        if (len * (ratio - 1.0f) + 6.0f > budget)
            len = (budget - 6.0f) / (ratio - 1.0f);
        behind = std::max (behind, len * (ratio - 1.0f) + 6.0f);
    }
    else if (ratio < 0.999f)
    {
        const float room = (float) kSize - 16.0f - behind;
        if (len * (1.0f - ratio) > room)
            len = std::max (2.0f, room / (1.0f - ratio));
    }

    slot->on     = true;
    slot->delay  = std::clamp (behind, 2.0f, (float) kSize - 16.0f);
    slot->ratio  = ratio;
    slot->phase  = 0.0f;
    slot->dPhase = 1.0f / std::max (len, 2.0f);
    slot->amp    = std::min (level, 0.50f) * amount * 0.42f;
}

void FractureGranulator::process (const float* inL, const float* inR, float* addL, float* addR, int n) noexcept
{
    const bool armed = amount > 1.0e-4f;

    for (int i = 0; i < n; ++i)
    {
        bufL[(size_t) write] = inL[i];
        bufR[(size_t) write] = inR[i];

        if (armed)
        {
            const float m = 0.5f * (std::abs (inL[i]) + std::abs (inR[i]));
            if (hold > 0)
                --hold;
            else if (m > 0.02f && m > env * 2.6f && m > prev)
            {
                spawn (m);
                hold = (int) std::lround (0.014 * (double) fs);
            }

            const float coeff = m > env ? 0.18f : 0.003f;
            env += coeff * (m - env);
            prev = m;

            if (spray > 0)
                --spray;
            else if (amount > 0.18f && env > 0.012f)
            {
                spawn (env * 0.70f);
                const float gap = (0.022f + 0.07f * (1.0f - amount)) * (0.65f + 0.70f * rng.uni());
                spray = std::max (1, (int) std::lround ((double) gap * (double) fs));
            }
        }
        else if (env > 1.0e-6f || hold > 0)
        {
            env += 0.01f * (0.0f - env);
            prev = 0.0f;
            if (hold > 0)
                --hold;
        }

        float gL = 0.0f, gR = 0.0f;
        for (auto& g : grains)
        {
            if (! g.on)
                continue;
            if (g.phase >= 1.0f || g.delay < 1.5f || g.delay > (float) kSize - 8.0f)
            {
                g.on = false;
                continue;
            }

            float read = (float) write - g.delay;
            if (read < 0.0f)
                read += (float) kSize;
            const int i0 = (int) read;
            const float frac = read - (float) i0;
            const int a = i0 & kMask;
            const int b = (a + 1) & kMask;
            const float sL = bufL[(size_t) a] + frac * (bufL[(size_t) b] - bufL[(size_t) a]);
            const float sR = bufR[(size_t) a] + frac * (bufR[(size_t) b] - bufR[(size_t) a]);
            const float window = 0.5f * (1.0f - std::cos (kTwoPi * g.phase));
            const float e = window * g.amp;
            gL += sL * e;
            gR += sR * e;

            g.phase += g.dPhase;
            g.delay += 1.0f - g.ratio;
        }

        if (gL != 0.0f || gR != 0.0f)
        {
            addL[i] += gL;
            addR[i] += gR;
        }

        if (++write == kSize)
            write = 0;
    }
}
} // namespace rb::dsp
