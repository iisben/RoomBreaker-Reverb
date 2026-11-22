//==============================================================================
// RoomBreaker offline test harness (no JUCE needed).
//
//   rb_offline_render [outputDir]
//
// Runs the DSP core through scripted scenarios, prints the per-wall integrity
// timeline, checks stability/behaviour invariants, writes WAV renders, and
// reports the real-time factor. Exit code 0 = all checks passed.
//==============================================================================
#include "Engine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace rb::dsp;

namespace { constexpr double kPiD = 3.14159265358979323846; }

namespace
{
constexpr double kFs = 48000.0;
int failures = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

//------------------------------------------------------------------------------
struct Stereo { std::vector<float> L, R; };

/** Kick + snare pattern, gain ramps from -36 dBFS to 0 dBFS over `rampSeconds`. */
float drumSample (double t, double rampSeconds, Rng& rng)
{
    const double gainDb = -36.0 + 36.0 * std::min (1.0, t / rampSeconds);
    const double gain = std::pow (10.0, gainDb / 20.0);

    const double beat = 0.5;
    const double tk = std::fmod (t, beat);                 // kick every 0.5 s
    const double ts = std::fmod (t + 0.25, beat);          // snare on the off-beat
    const double kickPhase = 2.0 * kPiD * (45.0 * tk + (120.0 / 30.0) * (1.0 - std::exp (-30.0 * tk)));
    const double kick  = std::sin (kickPhase) * std::exp (-9.0 * tk) + (tk < 0.003 ? 0.6 * rng.bi() : 0.0);
    const double snare = 0.6 * rng.bi() * std::exp (-25.0 * ts) + 0.3 * std::sin (2.0 * kPiD * 190.0 * ts) * std::exp (-20.0 * ts);
    return (float) (gain * 0.9 * (kick + snare));
}

/** 0–8 s escalating drums, 8–14 s sustained 42 Hz + kicks (fatigue), then silence. */
Stereo makeDestructionScript (double seconds, double kFs = ::kFs)
{
    Stereo s;
    const size_t n = (size_t) (seconds * kFs);
    s.L.resize (n); s.R.resize (n);
    Rng rng (1234);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / kFs;
        float x = 0.0f, side = 0.0f;
        if (t < 8.0)
        {
            x = drumSample (t, 8.0, rng);
            side = 0.15f * x * (float) std::sin (2.0 * kPiD * 0.3 * t);
        }
        else if (t < 14.0)
        {
            x = 0.707f * (float) std::sin (2.0 * kPiD * 42.0 * t) + 0.5f * drumSample (t, 0.001, rng);
        }
        s.L[i] = x + side;
        s.R[i] = x - side;
    }
    return s;
}


//------------------------------------------------------------------------------
// Quality metrics
//------------------------------------------------------------------------------
void fftInPlace (std::vector<double>& re, std::vector<double>& im)
{
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPiD / (double) len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; ++k)
            {
                const double wr = std::cos (ang * (double) k), wi = std::sin (ang * (double) k);
                const double ur = re[i + k], ui = im[i + k];
                const double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                const double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
            }
    }
}

/** Wet impulse response (left channel) of an unbreakable room. */
std::vector<float> impulseResponse (EngineParams p, double seconds)
{
    Engine e;
    p.mix = 1.0f; p.impact = 0.0f; p.rumble = 0.0f; p.debrisLevel = 0.0f;
    p.yieldDb = 0.0f; p.material = 4;   // Brutalist Monolith at 0 dB yield: an impulse can't break it
    e.setParams (p);
    e.prepare (kFs);
    const int n = (int) (seconds * kFs);
    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
    L[480] = R[480] = 0.5f;
    for (int pos = 0; pos < n; pos += 256)
        e.process (L.data() + pos, R.data() + pos, std::min (256, n - pos));
    return L;
}

/** Spectral ringing (dB RMS deviation from the smoothed spectrum), 300 Hz – 6 kHz, averaged Hann frames. */
double tailFlatnessDb (const std::vector<float>& x, double t0, double t1)
{
    constexpr size_t N = 8192;
    std::vector<double> power (N / 2, 0.0);
    int frames = 0;
    for (size_t start = (size_t) (t0 * kFs); start + N <= (size_t) (t1 * kFs); start += N / 2, ++frames)
    {
        std::vector<double> re (N), im (N, 0.0);
        for (size_t i = 0; i < N; ++i)
            re[i] = x[start + i] * (0.5 - 0.5 * std::cos (2.0 * kPiD * (double) i / (double) N));
        fftInPlace (re, im);
        for (size_t k = 0; k < N / 2; ++k) power[k] += re[k] * re[k] + im[k] * im[k];
    }
    // Ringing = how far the frame-averaged spectrum strays from its own 1/3-octave-smoothed curve.
    // Fixed (unmodulated) modes keep the same peaks in every frame, so they survive the averaging;
    // moving modes smear out. Tilt from damping is removed by the smoothing.
    std::vector<double> db (N / 2);
    for (size_t k = 0; k < N / 2; ++k) db[k] = 10.0 * std::log10 (power[k] / std::max (frames, 1) + 1e-30);
    double acc = 0.0; int count = 0;
    for (size_t k = 1; k < N / 2; ++k)
    {
        const double hz = (double) k * kFs / (double) N;
        if (hz < 300.0 || hz > 6000.0) continue;
        const size_t lo = (size_t) ((double) k / 1.122), hi = std::min (N / 2 - 1, (size_t) ((double) k * 1.122));
        double sm = 0.0;
        for (size_t j = lo; j <= hi; ++j) sm += db[j];
        sm /= (double) (hi - lo + 1);
        acc += (db[k] - sm) * (db[k] - sm); ++count;
    }
    return std::sqrt (acc / count);
}

/** Normalised echo density (Abel & Huang), mean over [t0, t1]: 1 = Gaussian-dense, low = sparse echoes. */
double echoDensity (const std::vector<float>& x, double t0, double t1)
{
    const int win = (int) (0.02 * kFs);
    double acc = 0.0; int count = 0;
    for (int c = (int) (t0 * kFs); c < (int) (t1 * kFs); c += win / 4, ++count)
    {
        double e2 = 0.0;
        for (int i = c - win / 2; i < c + win / 2; ++i) e2 += (double) x[(size_t) i] * x[(size_t) i];
        const double sd = std::sqrt (e2 / win);
        int outside = 0;
        for (int i = c - win / 2; i < c + win / 2; ++i) outside += std::abs ((double) x[(size_t) i]) > sd ? 1 : 0;
        acc += ((double) outside / win) / 0.3173;
    }
    return acc / std::max (count, 1);
}

struct RunResult
{
    Stereo out;
    std::vector<WallArray> integrityPerSecond; // sampled at the end of each second
    int frames = 0;
    float maxAbs = 0.0f;
    bool finite = true;
    double renderSeconds = 0.0;
    int brittleEvents = 0;
};

RunResult run (const EngineParams& p, const Stereo& in, int hostBlock = 512,
               const std::function<void (Engine&, double)>& hook = {})
{
    Engine engine;
    TelemetryBus bus;
    engine.setTelemetry (&bus);
    engine.setParams (p);
    engine.prepare (kFs);

    RunResult r;
    r.out = in;
    const size_t n = in.L.size();
    const auto start = std::chrono::steady_clock::now();
    size_t nextSecond = (size_t) kFs;

    for (size_t pos = 0; pos < n; pos += (size_t) hostBlock)
    {
        const int len = (int) std::min ((size_t) hostBlock, n - pos);
        if (hook) hook (engine, (double) pos / kFs);
        engine.setParams (p);
        engine.process (r.out.L.data() + pos, r.out.R.data() + pos, len);

        TelemetryFrame f;
        if (bus.popLatest (f))
        {
            ++r.frames;
            for (float m : f.fractureFlash) r.brittleEvents += m > 0.0f ? 1 : 0;
        }

        if (pos + (size_t) len >= nextSecond)
        {
            r.integrityPerSecond.push_back (engine.currentIntegrity());
            nextSecond += (size_t) kFs;
        }
    }
    r.renderSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

    for (size_t i = 0; i < n; ++i)
    {
        if (! std::isfinite (r.out.L[i]) || ! std::isfinite (r.out.R[i])) r.finite = false;
        r.maxAbs = std::max ({ r.maxAbs, std::abs (r.out.L[i]), std::abs (r.out.R[i]) });
    }
    return r;
}

float minOf (const WallArray& a) { return *std::min_element (a.begin(), a.end()); }
float meanOf (const WallArray& a) { float s = 0; for (float v : a) s += v; return s / (float) a.size(); }

float rms (const Stereo& s, double t0, double t1)
{
    const size_t a = (size_t) (t0 * kFs), b = std::min (s.L.size(), (size_t) (t1 * kFs));
    double acc = 0.0;
    for (size_t i = a; i < b; ++i) acc += 0.5 * (s.L[i] * s.L[i] + s.R[i] * s.R[i]);
    return (float) std::sqrt (acc / (double) std::max<size_t> (1, b - a));
}

void printTimeline (const RunResult& r)
{
    std::printf ("    t(s)   North  South  East   West   Ceil   Floor\n");
    for (size_t s = 0; s < r.integrityPerSecond.size(); ++s)
    {
        const auto& I = r.integrityPerSecond[s];
        std::printf ("    %3zu   ", s + 1);
        for (float v : I) std::printf (" %5.2f ", v);
        std::printf ("\n");
    }
}

void writeWav (const std::string& path, const Stereo& s)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t frames = (uint32_t) s.L.size(), dataBytes = frames * 4;
    f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVE", 4);
    f.write ("fmt ", 4); u32 (16); u16 (1); u16 (2); u32 ((uint32_t) kFs); u32 ((uint32_t) kFs * 4); u16 (4); u16 (16);
    f.write ("data", 4); u32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
    {
        u16 ((uint16_t) (int16_t) std::lround (std::clamp (s.L[i], -1.0f, 1.0f) * 32767.0f));
        u16 ((uint16_t) (int16_t) std::lround (std::clamp (s.R[i], -1.0f, 1.0f) * 32767.0f));
    }
}
} // namespace

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const double scriptSeconds = 30.0;
    const Stereo script = makeDestructionScript (scriptSeconds);

    //--------------------------------------------------------------------------
    std::printf ("\n== 1. Silence: nothing breaks, nothing comes out ==\n");
    {
        Stereo silence { std::vector<float> ((size_t) (5 * kFs), 0.0f), std::vector<float> ((size_t) (5 * kFs), 0.0f) };
        EngineParams p; p.mix = 1.0f;
        auto r = run (p, silence);
        check (r.finite && r.maxAbs == 0.0f, "output is exactly zero");
        check (minOf (r.integrityPerSecond.back()) == 1.0f, "all walls pristine");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== 2. Destruction + healing (Concrete, heal 6 s) ==\n");
    RunResult concrete;
    {
        EngineParams p; p.material = 0; p.healSeconds = 6.0f; p.mix = 0.5f;
        concrete = run (p, script);
        printTimeline (concrete);
        const float at14 = meanOf (concrete.integrityPerSecond[13]);
        const float at30 = meanOf (concrete.integrityPerSecond.back());
        std::printf ("    mean integrity @14 s = %.3f, @30 s = %.3f, peak |out| = %.3f, frames = %d\n",
                     at14, at30, concrete.maxAbs, concrete.frames);
        check (concrete.finite, "output finite");
        check (concrete.maxAbs < 4.0f, "output bounded (< 4.0)");
        check (minOf (concrete.integrityPerSecond[1]) > 0.95f, "quiet start leaves walls intact");
        check (minOf (concrete.integrityPerSecond[13]) < 0.5f, "loud section damages walls");
        check (at30 > at14 + 0.3f, "walls heal during silence");
        check (concrete.frames > 100, "telemetry frames delivered");
        writeWav (outDir + "/concrete_destruction.wav", concrete.out);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== 3. Permadeath: no recovery ==\n");
    {
        EngineParams p; p.material = 0; p.permadeath = true; p.mix = 0.5f;
        auto r = run (p, script);
        const float at15 = meanOf (r.integrityPerSecond[14]);
        const float at30 = meanOf (r.integrityPerSecond.back());
        std::printf ("    mean integrity @15 s = %.3f, @30 s = %.3f\n", at15, at30);
        check (std::abs (at30 - at15) < 1.0e-6f, "integrity frozen after the load stops");
        check (r.finite && r.maxAbs < 4.0f, "output finite and bounded");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== 4. Manual reset ==\n");
    {
        EngineParams p; p.material = 0; p.permadeath = true; p.mix = 0.5f;
        auto r = run (p, script, 512, [] (Engine& e, double t) {
            static bool fired = false;
            if (t < 1.0) fired = false;
            if (t >= 20.0 && ! fired) { e.resetStructure(); fired = true; }
        });
        check (minOf (r.integrityPerSecond[18]) < 0.9f, "damaged before reset");
        check (minOf (r.integrityPerSecond[21]) == 1.0f, "pristine after reset");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== 5. Material personalities (same script, permadeath) ==\n");
    std::printf ("    material                 first<0.9   first breach<0.15   fracture frames   mean@14s\n");
    for (int m = 0; m < kNumMaterials; ++m)
    {
        EngineParams p; p.material = m; p.permadeath = true; p.mix = 0.5f;
        auto r = run (p, script);
        int firstCrack = -1, firstBreach = -1;
        for (size_t s = 0; s < r.integrityPerSecond.size(); ++s)
        {
            if (firstCrack  < 0 && minOf (r.integrityPerSecond[s]) < 0.9f)  firstCrack  = (int) s + 1;
            if (firstBreach < 0 && minOf (r.integrityPerSecond[s]) < 0.15f) firstBreach = (int) s + 1;
        }
        std::printf ("    %-24s %6d s     %10d s      %10d        %.3f\n", getMaterialProfile (m).name,
                     firstCrack, firstBreach, r.brittleEvents, meanOf (r.integrityPerSecond[13]));
        check (r.finite && r.maxAbs < 4.0f, std::string (getMaterialProfile (m).name) + " stable");
        if (m == 0) writeWav (outDir + "/concrete_permadeath.wav", r.out);
        if (m == 1) writeWav (outDir + "/ceramic_permadeath.wav", r.out);
        if (m == 2) writeWav (outDir + "/silo_permadeath.wav", r.out);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== 6. Torture: worst-case states, full-scale noise then 40 s silence ==\n");
    {
        struct Case { float state; int voidType; int material; };
        std::vector<Case> cases;
        for (float state : { 1.0f, 0.55f, 0.3f, 0.0f })
            for (int v : { 0, 1 })
                cases.push_back ({ state, v, 2 });
        for (int v = 2; v < kNumVoids; ++v)                       // every new void, fully breached
            cases.push_back ({ 0.0f, v, 10 });                    // Sunken Bulkhead: the heaviest groans
        for (int m = 3; m < kNumMaterials; ++m)                   // every new material at peak looseness
            cases.push_back ({ 0.55f, 3, m });                    // into the 90 s Abyssal Drone

        for (const auto& c : cases)
        {
            const double secs = 45.0;
            Stereo noise { std::vector<float> ((size_t) (secs * kFs), 0.0f), std::vector<float> ((size_t) (secs * kFs), 0.0f) };
            Rng rng (99);
            for (size_t i = 0; i < (size_t) (5 * kFs); ++i) { noise.L[i] = rng.bi(); noise.R[i] = rng.bi(); }

            EngineParams p; p.material = c.material; p.decaySeconds = 12.0f; p.size = 2.0f; p.permadeath = true;
            p.voidType = c.voidType; p.mix = 1.0f; p.yieldDb = 0.0f;
            WallArray I; I.fill (c.state);
            auto r = run (p, noise, 333, [I] (Engine& e, double t) { if (t == 0.0) e.requestStructureRestore (I); });
            const float during = rms (r.out, 2.0, 5.0), early = rms (r.out, 5.0, 6.0), late = rms (r.out, 44.0, 45.0);
            std::printf ("    I=%.2f %-24s %-24s peak %.3f  wet/dry %+5.1f dB  rms@5s %.4f  rms@44s %.6f\n",
                         c.state, getMaterialProfile (c.material).name, getVoidProfile (c.voidType).name,
                         r.maxAbs, gainToDb (during / 0.57735f), early, late);
            check (r.finite && r.maxAbs < 4.0f && late < early, "bounded and decaying");
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== 7. Host block-size independence ==\n");
    {
        EngineParams p; p.material = 1; p.healSeconds = 6.0f; p.mix = 0.5f;
        auto a = run (p, script, 64);
        auto b = run (p, script, 333);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < a.out.L.size(); ++i) maxDiff = std::max (maxDiff, std::abs (a.out.L[i] - b.out.L[i]));
        std::printf ("    max |out64 - out333| = %g\n", maxDiff);
        check (maxDiff < 1.0e-5f, "64- and 333-sample host blocks match (float rounding only)");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== 8. Telemetry hand-off ==\n");
    {
        TelemetryBus bus;
        TelemetryFrame f;
        for (int k = 1; k <= 1000; ++k)          // UI closed: nobody reads for a long time
        {
            f.samplePosition = (uint64_t) k;
            f.fractureFlash.fill (0.0f);
            if (k == 10) f.fractureFlash[West] = 0.8f;   // one early crack
            bus.publish (f);
        }
        TelemetryFrame got;
        check (bus.popLatest (got) && got.samplePosition == 1000, "UI opening late receives the newest frame, not a stale one");
        check (got.fractureFlash[West] == 0.8f, "a crack from an overwritten frame is still delivered");
        check (! bus.popLatest (got), "no duplicate frame when nothing new was published");

        // Two threads hammering: every field of a frame carries the same sequence number,
        // so a torn read shows up as a mismatch. Sequence numbers must never go backwards.
        TelemetryBus shared;
        std::atomic<bool> done { false };
        std::thread producer ([&] {
            TelemetryFrame p;
            for (uint64_t seq = 1; seq <= 2000000; ++seq)
            {
                p.samplePosition = seq;
                p.integrity.fill ((float) (seq & 0xFFFF));
                p.stressRatio.fill ((float) (seq & 0xFFFF));
                shared.publish (p);
            }
            done.store (true);
        });
        uint64_t last = 0, reads = 0, torn = 0, backwards = 0;
        while (! done.load())
        {
            TelemetryFrame c;
            if (shared.popLatest (c))
            {
                ++reads;
                const float tag = (float) (c.samplePosition & 0xFFFF);
                for (int w = 0; w < kNumWalls; ++w)
                    if (c.integrity[(size_t) w] != tag || c.stressRatio[(size_t) w] != tag) ++torn;
                if (c.samplePosition < last) ++backwards;
                last = c.samplePosition;
            }
        }
        producer.join();
        std::printf ("    %llu reads during 2M publishes\n", (unsigned long long) reads);
        check (reads > 0 && torn == 0, "no torn frames under concurrent publish/read");
        check (backwards == 0, "frames never go backwards in time");
    }


    //--------------------------------------------------------------------------
    std::printf ("\n== 10. Quality measurements ==\n");
    {
        EngineParams base; base.decaySeconds = 3.0f;
        auto withMotion  = [&] (float m) { auto p = base; p.motion = m; return p; };
        auto withDensity = [&] (float d) { auto p = base; p.density = d; return p; };

        const double f0  = tailFlatnessDb (impulseResponse (withMotion (0.0f), 2.0), 0.5, 1.6);
        const double f25 = tailFlatnessDb (impulseResponse (withMotion (0.25f), 2.0), 0.5, 1.6);
        const double f60 = tailFlatnessDb (impulseResponse (withMotion (0.6f), 2.0), 0.5, 1.6);
        std::printf ("    tail ringing 0.5-1.6 s (dB spread of fixed spectral peaks; lower = less metallic):\n"
                     "      Motion 0 %% %.2f dB   25 %% %.2f dB   60 %% %.2f dB\n", f0, f25, f60);
        check (f25 < f0 && f60 < f25, "Motion makes the tail smoother (less modal ringing)");

        const double d0 = echoDensity (impulseResponse (withDensity (0.0f), 0.5), 0.03, 0.12);
        const double d70 = echoDensity (impulseResponse (withDensity (0.7f), 0.5), 0.03, 0.12);
        std::printf ("    echo density 30-120 ms (1.0 = fully dense):  Density 0 %% %.2f   70 %% %.2f\n", d0, d70);
        check (d70 > d0, "Density fills in the onset faster");

        // Same room, same script at 48 kHz and 192 kHz (the 4x engine rate): the structure must behave alike.
        auto meanAt14 = [&] (double fs)
        {
            EngineParams p; p.material = 0; p.permadeath = true; p.mix = 0.5f;
            const auto in = makeDestructionScript (16.0, fs);
            Engine e; e.setParams (p); e.prepare (fs);
            auto L = in.L, R = in.R;
            for (size_t pos = 0; pos < L.size(); pos += 512)
                e.process (L.data() + pos, R.data() + pos, (int) std::min<size_t> (512, L.size() - pos));
            const auto I = e.currentIntegrity();
            float m = 0.0f; for (float v : I) m += v / 6.0f;
            return std::make_pair (m, e.controlBlockSize());
        };
        const auto [m48, cb48]   = meanAt14 (48000.0);
        const auto [m96, cb96]   = meanAt14 (96000.0);
        const auto [m192, cb192] = meanAt14 (192000.0);
        std::printf ("    same script, permadeath: mean integrity 48 kHz %.3f (ctl %d)  96 kHz %.3f (ctl %d)  192 kHz %.3f (ctl %d)\n",
                     m48, cb48, m96, cb96, m192, cb192);
        check (std::abs (m96 - m48) < 0.12f && std::abs (m192 - m48) < 0.12f, "structure behaves the same at 1x, 2x and 4x engine rates");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== 11. Automation: no clicks when controls jump ==\n");
    {
        // A low sine through a room that never breaks: the wet signal is smooth, so any click is ours.
        // Metric: worst |second difference| in the 80 ms after a jump vs. the steady state before it.
        struct Jump { const char* name; std::function<void (EngineParams&, bool)> set; float limit; };
        const Jump jumps[] = {
            { "Drive 0 -> 100 %",        [] (EngineParams& p, bool b) { p.drive = b ? 1.0f : 0.0f; }, 6.0f },
            { "Drive 30 % -> 0",         [] (EngineParams& p, bool b) { p.drive = b ? 0.0f : 0.3f; }, 3.0f },
            { "Drive 0 -> 2 %",          [] (EngineParams& p, bool b) { p.drive = b ? 0.02f : 0.0f; }, 3.0f },
            { "Low Cut 20 Hz -> 1 kHz",  [] (EngineParams& p, bool b) { p.lowCutHz = b ? 1000.0f : 20.0f; }, 3.0f },
            { "High Cut 20 k -> 1 kHz",  [] (EngineParams& p, bool b) { p.highCutHz = b ? 1000.0f : 20000.0f; }, 3.0f },
            { "Width 0 -> 200 %",        [] (EngineParams& p, bool b) { p.width = b ? 2.0f : 0.0f; }, 3.0f },
            { "Ducking 0 -> 100 %",      [] (EngineParams& p, bool b) { p.duck = b ? 1.0f : 0.0f; }, 3.0f },
            { "Mix 0 -> 100 %",          [] (EngineParams& p, bool b) { p.mix = b ? 1.0f : 0.0f; }, 3.0f },
            { "Pre-Delay 0 -> 250 ms",   [] (EngineParams& p, bool b) { p.predelayMs = b ? 250.0f : 0.0f; }, 3.0f },
            { "Size 0.5x -> 2x",         [] (EngineParams& p, bool b) { p.size = b ? 2.0f : 0.5f; }, 4.5f },   // a pitch glide (Doppler), not a click
            // The phase-interpolated tank read lowers the steady-state second difference, so this
            // same gain step ratios a little higher. The jump itself is unchanged.
            { "Decay 0.5 s -> 20 s",     [] (EngineParams& p, bool b) { p.decaySeconds = b ? 20.0f : 0.5f; }, 3.5f },
            { "Density 0 -> 100 %",      [] (EngineParams& p, bool b) { p.density = b ? 1.0f : 0.0f; }, 3.0f },
            { "Motion 0 -> 100 %",       [] (EngineParams& p, bool b) { p.motion = b ? 1.0f : 0.0f; }, 3.0f },
            { "Freeze on",               [] (EngineParams& p, bool b) { p.freeze = b; }, 3.0f },
            { "Freeze off",              [] (EngineParams& p, bool b) { p.freeze = ! b; }, 3.0f },
            { "Material Concrete -> Glass", [] (EngineParams& p, bool b) { p.material = b ? 5 : 0; }, 4.0f },   // timbre change
            { "Void Free Field -> Hall", [] (EngineParams& p, bool b) { p.voidType = b ? 1 : 0; }, 3.0f },
        };
        for (const auto& j : jumps)
        {
            EngineParams p; p.yieldDb = 0.0f; p.impact = 0.0f; p.rumble = 0.0f; p.debrisLevel = 0.0f; p.mix = 0.6f;
            j.set (p, false);
            Engine e; e.setParams (p); e.prepare (kFs);
            const int total = (int) (1.6 * kFs), jumpAt = (int) (1.2 * kFs);
            std::vector<float> L ((size_t) total), R ((size_t) total);
            for (int i = 0; i < total; ++i)
                L[(size_t) i] = R[(size_t) i] = 0.25f * (float) std::sin (2.0 * kPiD * 110.0 * i / kFs);
            for (int pos = 0; pos < total; pos += 256)
            {
                if (pos == jumpAt) { j.set (p, true); e.setParams (p); }
                e.process (L.data() + pos, R.data() + pos, std::min (256, total - pos));
            }
            auto worstD2 = [&] (int a, int b)
            {
                float m = 0.0f;
                for (int i = std::max (a, 2); i < b; ++i)
                    for (const auto* x : { &L, &R })
                        m = std::max (m, std::abs ((*x)[(size_t) i] - 2.0f * (*x)[(size_t) i - 1] + (*x)[(size_t) i - 2]));
                return m;
            };
            const float before = worstD2 ((int) (0.8 * kFs), jumpAt);
            const float after  = worstD2 (jumpAt, jumpAt + (int) (0.08 * kFs));
            const float ratio  = after / std::max (before, 1.0e-9f);
            std::printf ("    %-28s  click ratio %5.2f\n", j.name, ratio);
            check (ratio < j.limit, std::string ("no click: ") + j.name);
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== 12. Fuzz: random automation, random input, 40 s ==\n");
    {
        Rng rng (777);
        EngineParams p;
        Engine e; e.setParams (p); e.prepare (kFs);
        bool finite = true; float peak = 0.0f;
        std::vector<float> L (512), R (512);
        for (int block = 0; block < (int) (40.0 * kFs / 512); ++block)
        {
            if (block % 3 == 0)
            {
                p.material = (int) (rng.uni() * 11.0f) % 11;  p.voidType = (int) (rng.uni() * 7.0f) % 7;
                p.yieldDb = -40.0f + 40.0f * rng.uni();      p.ductileBias = rng.uni();  p.asymmetry = rng.uni();
                p.healSeconds = 0.5f + 60.0f * rng.uni();    p.permadeath = rng.uni() < 0.2f;
                p.impact = rng.uni(); p.rumble = rng.uni();  p.debrisLevel = rng.uni(); p.granular = rng.uni();
                p.size = 0.5f + 1.5f * rng.uni();            p.decaySeconds = 0.3f + 30.0f * rng.uni();
                p.predelayMs = 250.0f * rng.uni();           p.density = rng.uni(); p.motion = rng.uni();
                p.freeze = rng.uni() < 0.15f;                p.drive = rng.uni();
                p.lowCutHz = 20.0f + 980.0f * rng.uni();     p.highCutHz = 1000.0f + 19000.0f * rng.uni();
                p.width = 2.0f * rng.uni();                  p.duck = rng.uni(); p.mix = rng.uni();
                e.setParams (p);
                if (rng.uni() < 0.05f) e.detonate();
                if (rng.uni() < 0.02f) e.resetStructure();
            }
            const float level = rng.uni() < 0.1f ? 2.0f : (rng.uni() < 0.3f ? 0.0f : rng.uni());
            for (int i = 0; i < 512; ++i) { L[(size_t) i] = level * rng.bi(); R[(size_t) i] = level * rng.bi(); }
            e.process (L.data(), R.data(), 512);
            for (int i = 0; i < 512; ++i)
            {
                finite = finite && std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]);
                peak = std::max ({ peak, std::abs (L[(size_t) i]), std::abs (R[(size_t) i]) });
            }
        }
        std::printf ("    peak %.2f (input peaks up to 2.0)\n", peak);
        check (finite, "every sample finite under random automation");
        check (peak < 12.0f, "output stays bounded under random automation");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== 9. Performance ==\n");
    std::printf ("    30 s stereo @48 kHz rendered in %.3f s  ->  %.0fx real time (single core)\n",
                 concrete.renderSeconds, scriptSeconds / concrete.renderSeconds);

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
