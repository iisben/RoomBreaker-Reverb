#include "ModalDebrisGenerator.h"

namespace rb::dsp
{
namespace
{
    constexpr WallArray kWallFreqScale { 1.0f, 0.94f, 1.06f, 1.11f, 0.87f, 0.80f };
    constexpr WallArray kWallPan       { 0.0f, 0.0f, 0.75f, -0.75f, 0.0f, 0.0f };
    constexpr float kSilenceThreshold  = 1.0e-12f;
}

void ModalDebrisGenerator::prepare (double sampleRate)
{
    fs = (float) sampleRate;
    dcL.set (20.0f, fs);
    dcR.set (20.0f, fs);
    reset();
}

void ModalDebrisGenerator::reset()
{
    for (auto& v : voices) v = Voice {};
    looseness.fill (0.0f);
    chatterEnergy.fill (0.0f);
    activity = rattleMix = 0.0f;
    dcL.reset(); dcR.reset();
}

void ModalDebrisGenerator::setStructure (const WallArray& I, float inputActivity) noexcept
{
    float meanLoose = 0.0f;
    for (int w = 0; w < kNumWalls; ++w)
    {
        looseness[w] = loosenessAmount (I[w]);
        meanLoose += looseness[w] / (float) kNumWalls;
    }
    activity  = clamp01 (inputActivity);
    rattleMix = material != nullptr ? material->rattleAmount * clamp01 (2.0f * meanLoose) : 0.0f;
    rattleGap = 0.05f + 0.25f * (1.0f - meanLoose);
}

ModalDebrisGenerator::Voice& ModalDebrisGenerator::allocateVoice() noexcept
{
    Voice* quietest = &voices[0];
    float quietestEnergy = 1.0e30f;
    for (auto& v : voices)
    {
        if (! v.active)
            return v;
        const float e = v.zr * v.zr + v.zi * v.zi;
        if (v.exciteRemaining == 0 && v.startDelay == 0 && e < quietestEnergy)
        {
            quietestEnergy = e;
            quietest = &v;
        }
    }
    return *quietest; // steal the quietest ringing voice
}

void ModalDebrisGenerator::startMode (float freqHz, float t60, float amp, int delaySamples, int burstSamples, float pan) noexcept
{
    if (freqHz <= 20.0f || freqHz >= 0.42f * fs)
        return;

    auto& v = allocateVoice();
    const float w = kTwoPi * freqHz / fs;
    const float r = std::exp (-6.9078f / (std::max (t60, 0.005f) * fs));
    v.active = true;
    v.pr = r * std::cos (w);
    v.pi = r * std::sin (w);
    v.zr = v.zi = 0.0f;
    v.startDelay = std::max (0, delaySamples);
    v.exciteRemaining = std::max (1, burstSamples);
    v.exciteAmp = amp / std::sqrt ((float) v.exciteRemaining);
    v.exciteEnv = 1.0f;
    v.exciteDecay = std::exp (-5.0f / (float) v.exciteRemaining);
    v.noise = Rng (rng.next());

    const float theta = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    v.panL = std::cos (theta);
    v.panR = std::sin (theta);
}

void ModalDebrisGenerator::trigger (const FractureEvent& e) noexcept
{
    if (material == nullptr)
        return;

    const auto& m = *material;
    const int w = std::clamp (e.wall, 0, kNumWalls - 1);
    const int numModes = std::clamp (3 + (int) (e.magnitude * 5.0f), 2, 8);
    const float burstSec = e.brittle ? lerp (0.0008f, 0.004f, rng.uni())   // crack: sharp click
                                     : lerp (0.006f, 0.02f, rng.uni());    // creak: scraping groan
    const int burst = (int) (burstSec * fs);

    for (int i = 0; i < numModes; ++i)
    {
        const int idx = std::min (7, i + (rng.uni() < 0.3f ? 1 : 0));
        const float ratio = m.modeRatios[(size_t) idx];
        const float freq  = m.modeBaseHz * ratio * kWallFreqScale[w] * (1.0f + 0.035f * rng.bi());
        const float t60   = m.modeT60 * (0.6f + 0.8f * rng.uni()) / std::sqrt (ratio) * (1.0f + looseness[w]);
        const float amp   = 0.35f * e.magnitude * (0.6f + 0.4f * rng.uni()) / std::sqrt ((float) (i + 1));
        const int delay   = e.sampleOffset + (int) (rng.uni() * 0.0015f * fs);
        startMode (freq, t60, amp, delay, burst, kWallPan[w] + 0.35f * rng.bi());
    }

    // Rebar ping / shard ring on hard brittle hits.
    if (e.brittle && rng.uni() < 0.5f * e.magnitude)
        startMode (m.pingHz * (1.0f + 0.1f * rng.bi()), m.pingT60, 0.12f * e.magnitude,
                   e.sampleOffset, (int) (0.0005f * fs), kWallPan[w] + 0.5f * rng.bi());

    chatterEnergy[w] += e.magnitude * (e.brittle ? 1.0f : 0.4f);
}

void ModalDebrisGenerator::updateChatter (float dt, int blockLength) noexcept
{
    if (material == nullptr)
        return;

    const auto& m = *material;
    for (int w = 0; w < kNumWalls; ++w)
    {
        chatterEnergy[w] = std::min (chatterEnergy[w], 4.0f) * std::exp (-dt / (0.6f + 1.2f * looseness[w]));

        const float rate = 30.0f * chatterEnergy[w] + 18.0f * looseness[w] * activity; // events / s
        if (rng.uni() < rate * dt)
        {
            const float amp = (0.04f + 0.12f * rng.uni()) * (0.5f + chatterEnergy[w] + looseness[w]) * 0.35f;
            const int idx   = 3 + (int) (rng.uni() * 5.0f);  // small fragments → upper modes
            const float ratio = m.modeRatios[(size_t) std::min (idx, 7)];
            startMode (m.modeBaseHz * ratio * kWallFreqScale[w] * (1.0f + 0.06f * rng.bi()),
                       0.3f * m.modeT60 / std::sqrt (ratio), amp,
                       (int) (rng.uni() * (float) blockLength), (int) (0.0006f * fs),
                       kWallPan[w] + 0.6f * rng.bi());
        }
    }
}

void ModalDebrisGenerator::process (float* outL, float* outR, int n) noexcept
{
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            if (v.startDelay > 0)
                --v.startDelay;
            else if (v.exciteRemaining > 0)
            {
                x = v.exciteAmp * v.exciteEnv * v.noise.bi();
                v.exciteEnv *= v.exciteDecay;
                --v.exciteRemaining;
            }

            const float zr = v.pr * v.zr - v.pi * v.zi + x;
            const float zi = v.pr * v.zi + v.pi * v.zr;
            v.zr = zr;
            v.zi = zi;
            outL[i] += zi * v.panL;
            outR[i] += zi * v.panR;
        }

        if (v.startDelay == 0 && v.exciteRemaining == 0 && v.zr * v.zr + v.zi * v.zi < kSilenceThreshold)
            v.active = false;
    }

    // Structural contact distortion: loose slabs slap their frames (one-sided clip), then DC-block.
    // The DC blocker always runs so rattle can fade in and out without a step.
    const float gap = rattleGap, mix = rattleMix;
    auto contact = [gap] (float y) noexcept { return y < -gap ? -gap + (y + gap) * 0.12f : y; };

    for (int i = 0; i < n; ++i)
    {
        const float l = lerp (outL[i], contact (outL[i]), mix);
        const float r = lerp (outR[i], contact (outR[i]), mix);
        outL[i] = dcL.hp (l);
        outR[i] = dcR.hp (r);
    }
}

int ModalDebrisGenerator::activeVoices() const noexcept
{
    int count = 0;
    for (const auto& v : voices) count += v.active ? 1 : 0;
    return count;
}
} // namespace rb::dsp
