#include "HouseView.h"
#include "Theme.h"
#include "../Parameters.h"

namespace rb::ui
{
namespace
{
    using V3 = HouseView::V3;
    using P2 = juce::Point<float>;

    V3 operator+ (V3 a, V3 b) noexcept     { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    V3 operator- (V3 a, V3 b) noexcept     { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    V3 operator* (V3 a, float s) noexcept  { return { a.x * s, a.y * s, a.z * s }; }
    float dot (V3 a, V3 b) noexcept        { return a.x * b.x + a.y * b.y + a.z * b.z; }
    V3 normalised (V3 a) noexcept          { const float l = std::sqrt (dot (a, a)); return l > 0.0f ? a * (1.0f / l) : a; }
    float smooth (float e0, float e1, float x) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0));
        return t * t * (3.0f - 2.0f * t);
    }

    //--------------------------------------------------------------------------
    // Geometry (metres). House-local origin: centre of the footprint, z = 0 on the lawn.
    constexpr float kW = 3.0f, kD = 2.2f, kH = 2.0f, kR = 1.3f;       // body and ridge rise
    constexpr float kEave = 0.24f, kGable = 0.16f, kRoofT = 0.12f;    // roof overhangs, thickness
    constexpr float kSlabX = 2.2f, kSlabY = 2.1f, kSlabT = 0.3f;      // lawn (world, centred)
    constexpr float kHouseY = 0.4f;                                   // house sits towards the back
    constexpr float kChimX = 0.62f, kChimY = 0.35f, kChimA = 0.2f, kChimTop = kH + kR + 0.62f;
    constexpr float kElev = 0.5236f;                                  // 30° camera elevation
    constexpr float kDefaultYaw = 0.7854f;                            // camera to the south-east
    constexpr float kTileV = 0.2f, kTileU = 0.28f;

    const float kPhi  = std::atan (kR / (0.5f * kW));
    const float kSlope = kR / (0.5f * kW);
    const float kTz   = kRoofT / std::cos (kPhi);
    const V3 kLight   = normalised ({ -0.45f, -0.85f, 1.1f });          // from the front-left, above

    float roofTopZ (float x) noexcept { return kH + kR + kTz - kSlope * std::abs (x); }

    struct Opening { float u0, v0, u1, v1; bool door; };
    struct WallDef
    {
        V3 o, u, n;
        float uLen;
        bool gable;
        std::vector<Opening> openings;
    };

    // dsp::Wall order: North, South, East, West
    const WallDef kWalls[4] = {
        { { 0.5f * kW, 0.5f * kD, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, kW, true,  { { 1.05f, 0.8f, 1.95f, 1.45f, false } } },
        { { -0.5f * kW, -0.5f * kD, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, kW, true,
          { { 0.55f, 0.0f, 1.3f, 1.6f, true }, { 1.85f, 0.78f, 2.5f, 1.48f, false } } },
        { { 0.5f * kW, -0.5f * kD, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, kD, false, { { 0.78f, 0.78f, 1.42f, 1.48f, false } } },
        { { -0.5f * kW, 0.5f * kD, 0 }, { 0, -1, 0 }, { -1, 0, 0 }, kD, false, { { 0.82f, 0.85f, 1.38f, 1.45f, false } } },
    };

    juce::Path wallOutline (const WallDef& w)
    {
        juce::Path p;
        p.startNewSubPath (0.0f, 0.0f);
        p.lineTo (w.uLen, 0.0f);
        p.lineTo (w.uLen, kH);
        if (w.gable) p.lineTo (0.5f * w.uLen, kH + kR);
        p.lineTo (0.0f, kH);
        p.closeSubPath();
        return p;
    }

    //--------------------------------------------------------------------------
    // What the walls are made of (follows the Material choice) and what the outside looks like (Void).
    enum Pattern { Plain, Tiles, Ribs, Blocks, Planks, Glass, Plates };
    enum Under   { Brick, Concrete, Studs, Nothing, Rubble };
    struct Finish { juce::uint32 wall, line; Pattern pattern; float a, b; juce::uint32 under; Under underKind; juce::uint32 debris; };

    const Finish kFinishes[] = {
        { 0xfff1eee8, 0xffddd8cf, Plain,  0.0f,  0.0f,   0xff9c978e, Concrete, 0xffd8d3ca },   // Concrete Bunker
        { 0xfff5f3ee, 0xffd9d3c9, Tiles,  0.22f, 0.22f,  0xffb35c3e, Brick,    0xffede8e0 },   // Ceramic Chamber
        { 0xffc3c9c4, 0xff9ea6a1, Ribs,   0.08f, 0.0f,   0xff6f7771, Nothing,  0xffb4bab5 },   // Corrugated Silo
        { 0xffa9654a, 0xff7e4532, Ribs,   0.11f, 0.0f,   0xff4f2a1e, Nothing,  0xff8c5038 },   // Rusted Hangar
        { 0xff8d8a84, 0xff74716b, Blocks, 0.9f,  0.45f,  0xff66635e, Concrete, 0xff85827c },   // Brutalist Monolith
        { 0xffbfdde6, 0xff7f98a0, Glass,  0.55f, 0.66f,  0xff2c3a40, Nothing,  0xffd8eef4 },   // Shattered Glass Atrium
        { 0xffe2d6bd, 0xffc4b79c, Blocks, 0.42f, 0.24f,  0xffa8987a, Rubble,   0xffd5c8ac },   // Cathedral Stone
        { 0xfff2f2ee, 0xffd2d2cb, Blocks, 0.15f, 0.075f, 0xff9a968f, Concrete, 0xffe6e6e0 },   // Subway Tile Tunnel
        { 0xffd3ecf2, 0xffa9cfda, Blocks, 0.6f,  0.4f,   0xff8db7c4, Nothing,  0xffe8f6fa },   // Cracked Ice Sheet
        { 0xff5b4335, 0xff3e2d24, Planks, 0.17f, 0.0f,   0xff261b15, Studs,    0xff4a372b },   // Burnt Timber Frame
        { 0xff506b6f, 0xff3d5458, Plates, 0.7f,  0.5f,   0xff2b3b3e, Nothing,  0xff5c7a7e },   // Sunken Bulkhead
    };

    struct Backdrop { juce::uint32 top, bottom; int scenery; };   // 0 none, 1 arches, 2 skyline, 3 water, 4 deep
    const Backdrop kBackdrops[] = {
        { 0xffcbe4e9, 0xffb5d4db, 0 },   // Free Field: open sky
        { 0xffb4bec8, 0xff9aa6b1, 1 },   // Infinite Secondary Tail: a hall
        { 0xff9c968e, 0xff847e77, 1 },   // Industrial Tunnel
        { 0xff252d37, 0xff14191f, 4 },   // Abyssal Drone
        { 0xff6e3c2b, 0xff3a241f, 2 },   // Burning City
        { 0xff86aeae, 0xff5f8c8e, 3 },   // Flooded Undercroft
        { 0xffcac2b4, 0xffb0a897, 2 },   // Concrete Canyon
    };

    constexpr juce::uint32 kRoofRed = 0xffd8473a, kRoofEdge = 0xffa8352c, kBatten = 0xff5b3426, kInterior = 0xff2e2824;
    constexpr juce::uint32 kGrass = 0xff86c25b, kGrassDry = 0xffa4ad62, kGrassSide = 0xff679f45, kSoil = 0xff5b3e2b;
    constexpr juce::uint32 kFrame = 0xfffbfbf8, kGlass = 0xffaed2e2, kWood = 0xffd49c5c, kBrickRed = 0xffbf5f45;

    //--------------------------------------------------------------------------
    // Procedural damage, generated once per surface (same seed → same house every time).
    struct Seed { P2 p; float dir; };

    void growCrack (juce::Random& r, HouseView::Surface& s, P2 p, float dir, float th, float length,
                    int depth, float uMax, float vMax)
    {
        const float step = 0.07f + 0.05f * r.nextFloat();
        for (float travelled = 0.0f; travelled < length; travelled += step)
        {
            dir += (r.nextFloat() - 0.5f) * 0.8f;
            const P2 q = p + P2 (std::cos (dir), std::sin (dir)) * step;
            if (q.x < -0.05f || q.y < -0.05f || q.x > uMax + 0.05f || q.y > vMax + 0.05f)
                return;
            s.cracks.push_back ({ p, q, th });
            p = q;
            th += 0.008f + 0.018f * r.nextFloat();
            if (depth < 2 && r.nextFloat() < 0.13f)
                growCrack (r, s, p, dir + (r.nextBool() ? 1.0f : -1.0f) * (0.5f + 0.6f * r.nextFloat()),
                           th + 0.05f, length * 0.45f, depth + 1, uMax, vMax);
        }
    }

    HouseView::Blob makeBlob (juce::Random& r, P2 c, float radius, float th, float roughness)
    {
        HouseView::Blob b { c, radius, th, {} };
        for (size_t k = 0; k < b.shape.size(); ++k)
            b.shape[k] = (k % 2 == 0 ? 1.0f : 0.8f) * (1.0f - roughness + 2.0f * roughness * r.nextFloat());
        return b;
    }

    HouseView::Surface makeSurface (juce::Random& r, std::vector<Seed> seeds, float uMax, float vMax, int spalls, int holes,
                                    bool finalBreach)
    {
        HouseView::Surface s;
        for (int k = 0; k < 3; ++k)
            seeds.push_back ({ { uMax * (0.15f + 0.7f * r.nextFloat()), vMax * (0.1f + 0.7f * r.nextFloat()) },
                               juce::MathConstants<float>::twoPi * r.nextFloat() });

        // Stagger the start thresholds so the first cracks come early and the rest follow.
        std::vector<float> starts;
        for (size_t k = 0; k < seeds.size(); ++k) starts.push_back (0.04f + 0.5f * r.nextFloat());
        std::sort (starts.begin(), starts.end());
        for (size_t k = 0; k < seeds.size(); ++k)
        {
            const size_t pick = (size_t) r.nextInt ((int) seeds.size());
            std::swap (seeds[k], seeds[pick]);
        }
        for (size_t k = 0; k < seeds.size(); ++k)
            growCrack (r, s, seeds[k].p, seeds[k].dir, starts[k], 0.5f + 0.9f * r.nextFloat(), 0, uMax, vMax);

        for (int k = 0; k < spalls && ! s.cracks.empty(); ++k)
        {
            const auto& c = s.cracks[(size_t) r.nextInt ((int) s.cracks.size())];
            const float th = juce::jlimit (0.28f, 0.92f, std::max (c.th + 0.1f, 0.28f) + 0.25f * r.nextFloat());
            s.spalls.push_back (makeBlob (r, (c.a + c.b) * 0.5f, 0.08f + 0.14f * r.nextFloat(), th, 0.35f));
        }
        for (int k = 0; k < holes; ++k)
        {
            const P2 c = ! s.spalls.empty() && r.nextFloat() < 0.7f ? s.spalls[(size_t) r.nextInt ((int) s.spalls.size())].c
                                                                   : P2 (uMax * r.nextFloat(), vMax * (0.15f + 0.6f * r.nextFloat()));
            s.holes.push_back (makeBlob (r, c, 0.2f + 0.22f * r.nextFloat(), 0.6f + 0.28f * r.nextFloat(), 0.3f));
        }
        if (finalBreach)
            s.holes.push_back (makeBlob (r, { 0.5f * uMax, 0.48f * vMax }, 0.62f * std::max (uMax, vMax), 0.95f, 0.18f));
        return s;
    }

    juce::Path blobPath (const HouseView::Blob& b, float scaleBy)
    {
        juce::Path p;
        const int n = (int) b.shape.size();
        for (int k = 0; k < n; ++k)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) k / (float) n;
            const P2 q = b.c + P2 (std::cos (a), std::sin (a)) * (b.r * scaleBy * b.shape[(size_t) k]);
            if (k == 0) p.startNewSubPath (q); else p.lineTo (q);
        }
        p.closeSubPath();
        return p;
    }

    juce::Path quad (P2 a, P2 b, P2 c, P2 d)
    {
        juce::Path p;
        p.startNewSubPath (a); p.lineTo (b); p.lineTo (c); p.lineTo (d); p.closeSubPath();
        return p;
    }

    juce::Path convexHull (std::vector<P2> pts)
    {
        juce::Path p;
        if (pts.size() < 3) return p;
        std::sort (pts.begin(), pts.end(), [] (P2 a, P2 b) { return a.x < b.x || (! (b.x < a.x) && a.y < b.y); });
        auto cross = [] (P2 o, P2 a, P2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
        std::vector<P2> h (pts.size() * 2);
        size_t k = 0;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            while (k >= 2 && cross (h[k - 2], h[k - 1], pts[i]) <= 0) --k;
            h[k++] = pts[i];
        }
        for (size_t i = pts.size() - 1, t = k + 1; i > 0; --i)
        {
            while (k >= t && cross (h[k - 2], h[k - 1], pts[i - 1]) <= 0) --k;
            h[k++] = pts[i - 1];
        }
        h.resize (k - 1);
        p.startNewSubPath (h[0]);
        for (size_t i = 1; i < h.size(); ++i) p.lineTo (h[i]);
        p.closeSubPath();
        return p;
    }

    float shadeFor (V3 n) noexcept { return 0.74f + 0.3f * std::max (0.0f, dot (n, kLight)); }
}

//==============================================================================
HouseView::HouseView (LiveState& s, juce::AudioProcessorValueTreeState& apvts)
    : live (s), yaw (kDefaultYaw)
{
    materialParam = apvts.getRawParameterValue (ids::material);
    voidParam = apvts.getRawParameterValue (ids::voidType);

    // Walls: cracks start at the corners of doors and windows (where real walls crack first).
    for (int w = 0; w < 4; ++w)
    {
        juce::Random r (1201 + 97 * w);
        std::vector<Seed> seeds;
        const auto& def = kWalls[w];
        constexpr float q = juce::MathConstants<float>::pi * 0.25f;
        for (const auto& o : def.openings)
        {
            seeds.push_back ({ { o.u1, o.v1 }, q });
            seeds.push_back ({ { o.u0, o.v1 }, 3.0f * q });
            if (! o.door)
            {
                seeds.push_back ({ { o.u0, o.v0 }, 5.0f * q });
                seeds.push_back ({ { o.u1, o.v0 }, 7.0f * q });
            }
        }
        seeds.push_back ({ { 0.04f, 0.03f }, 1.2f });
        seeds.push_back ({ { def.uLen - 0.04f, 0.03f }, 1.9f });
        walls[(size_t) w] = makeSurface (r, seeds, def.uLen, def.gable ? kH + kR : kH, 10, 4, true);
    }

    // Roof: tiles go first around a few weak spots.
    const float slopeLen = (0.5f * kW + kEave) / std::cos (kPhi);
    const float roofU = kD + 2.0f * kGable;
    const int rows = (int) std::ceil (slopeLen / kTileV), cols = (int) std::ceil (roofU / kTileU) + 1;
    for (int plane = 0; plane < 2; ++plane)
    {
        juce::Random r (77 + plane * 13);
        P2 spots[3];
        float radius[3];
        for (int k = 0; k < 3; ++k)
        {
            spots[k] = { roofU * (0.15f + 0.7f * r.nextFloat()), slopeLen * (0.2f + 0.7f * r.nextFloat()) };
            radius[k] = 0.5f + 0.5f * r.nextFloat();
        }
        auto& th = tileThreshold[(size_t) plane];
        th.resize ((size_t) (rows * cols));
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j)
            {
                const P2 c ((float) j * kTileU + (i % 2 == 1 ? 0.5f * kTileU : 0.0f), ((float) i + 0.5f) * kTileV);
                float near = 9.0f;
                for (int k = 0; k < 3; ++k) near = std::min (near, c.getDistanceFrom (spots[k]) / radius[k]);
                th[(size_t) (i * cols + j)] = 0.14f + 0.62f * juce::jlimit (0.0f, 1.0f, near / 1.6f) + 0.16f * r.nextFloat();
            }
        roofPlanes[(size_t) plane] = {};
    }

    // Lawn: fissures run out from the house corners.
    {
        juce::Random r (4242);
        std::vector<Seed> seeds;
        const P2 hc (kSlabX, kSlabY + kHouseY);
        seeds.push_back ({ hc + P2 (-0.5f * kW, -0.5f * kD), 3.6f });
        seeds.push_back ({ hc + P2 (0.5f * kW, -0.5f * kD), 5.6f });
        seeds.push_back ({ hc + P2 (0.5f * kW, 0.5f * kD), 0.6f });
        seeds.push_back ({ hc + P2 (-0.5f * kW, 0.5f * kD), 2.4f });
        ground = makeSurface (r, seeds, 2.0f * kSlabX, 2.0f * kSlabY, 0, 0, false);
    }
}

//==============================================================================
// Camera
//==============================================================================
juce::Point<float> HouseView::toScreen (V3 p) const noexcept
{
    const float ca = std::cos (yaw), sa = std::sin (yaw);
    const float right = p.x * ca + p.y * sa;
    const float depth = -p.x * sa + p.y * ca;                 // away from the camera
    const float up = p.z * std::cos (kElev) + depth * std::sin (kElev);
    return { centre.x + shake.x + scale * right, centre.y + shake.y - scale * up };
}

V3 HouseView::toCamera() const noexcept
{
    return { std::sin (yaw) * std::cos (kElev), -std::cos (yaw) * std::cos (kElev), std::sin (kElev) };
}

float HouseView::tiltAngle() const noexcept
{
    return 0.05f * smooth (0.35f, 1.0f, damageOf (dsp::Floor));   // up to ~3°: the house settles
}

V3 HouseView::houseDir (V3 d) const noexcept
{
    const float t = tiltAngle(), c = std::cos (t), s = std::sin (t);
    return { d.x * c + d.z * s, d.y, -d.x * s + d.z * c };
}

V3 HouseView::houseToWorld (V3 p) const noexcept
{
    // Settling pivots on the east footing: the west side lifts off the lawn.
    const V3 rel { p.x - 0.5f * kW, p.y, p.z };
    const V3 r = houseDir (rel);
    return { r.x + 0.5f * kW, r.y + kHouseY, r.z };
}

juce::AffineTransform HouseView::planeTransform (V3 o, V3 u, V3 v, bool houseSpace) const noexcept
{
    const V3 O = houseSpace ? houseToWorld (o) : o;
    const V3 U = houseSpace ? houseDir (u) : u;
    const V3 V = houseSpace ? houseDir (v) : v;
    const auto s0 = toScreen (O);
    const auto su = toScreen (O + U) - s0;
    const auto sv = toScreen (O + V) - s0;
    return { su.x, sv.x, s0.x, su.y, sv.y, s0.y };
}

bool HouseView::frontOfHouse (V3 p) const noexcept
{
    return p.x * std::sin (yaw) - (p.y - kHouseY) * std::cos (yaw) > 0.0f;
}

float HouseView::damageOf (int wall) const noexcept
{
    return 1.0f - juce::jlimit (0.0f, 1.0f, live.frame.integrity[(size_t) wall]);
}

void HouseView::resized()
{
    // Fit the whole lawn plus the chimney at the default angle; turning stays inside the same circle.
    const float savedYaw = yaw;
    yaw = kDefaultYaw;
    scale = 1.0f;
    centre = {};
    shake = {};
    float x0 = 1.0e9f, y0 = 1.0e9f, x1 = -1.0e9f, y1 = -1.0e9f;
    auto include = [&] (P2 p) { x0 = std::min (x0, p.x); y0 = std::min (y0, p.y); x1 = std::max (x1, p.x); y1 = std::max (y1, p.y); };
    for (float x : { -kSlabX, kSlabX })
        for (float y : { -kSlabY, kSlabY })
            for (float z : { -kSlabT, 0.0f })
                include (toScreen ({ x, y, z }));
    include (toScreen ({ kChimX, kChimY + kHouseY, kChimTop + 0.1f }));
    const juce::Rectangle<float> box (x0, y0, x1 - x0, y1 - y0);
    yaw = savedYaw;

    const auto area = getLocalBounds().toFloat().reduced (12.0f, 18.0f);
    scale = std::min (area.getWidth() / box.getWidth(), area.getHeight() / box.getHeight());
    centre = area.getCentre() - box.getCentre() * scale + P2 (0.0f, 6.0f);
}

//==============================================================================
// Debris
//==============================================================================
juce::Colour HouseView::debrisColour (int wall) const
{
    const auto& f = kFinishes[juce::jlimit (0, 10, (int) materialParam->load())];
    if (wall == dsp::Ceiling) return juce::Colour (rng.nextFloat() < 0.75f ? kRoofRed : kBatten);
    if (wall == dsp::Floor)   return juce::Colour (rng.nextFloat() < 0.6f ? kSoil : kGrassSide);
    if (damageOf (wall) > 0.3f && f.underKind != Nothing && rng.nextFloat() < 0.4f)
        return juce::Colour (f.under);
    return juce::Colour (f.debris);
}

void HouseView::spawnDust (V3 at, int count, float spread)
{
    const auto& bd = kBackdrops[juce::jlimit (0, 6, (int) voidParam->load())];
    const bool darkScene = juce::Colour (bd.top).getPerceivedBrightness() < 0.4f;
    for (int k = 0; k < count; ++k)
    {
        Particle d;
        d.dust = true;
        d.p = at + V3 { (rng.nextFloat() - 0.5f) * spread, (rng.nextFloat() - 0.5f) * spread, (rng.nextFloat() - 0.3f) * spread };
        d.v = { (rng.nextFloat() - 0.5f) * 0.3f, (rng.nextFloat() - 0.5f) * 0.3f, 0.1f + 0.2f * rng.nextFloat() };
        d.size = 0.05f + 0.08f * rng.nextFloat();
        d.life = 0.7f + 0.7f * rng.nextFloat();
        d.colour = darkScene ? juce::Colour (0xff8a8178) : juce::Colour (0xffe9e3d8);
        particles.push_back (d);
    }
}

void HouseView::spawnFracture (int wall, float m)
{
    const int count = 5 + (int) (22.0f * std::min (m, 1.2f));
    for (int k = 0; k < count; ++k)
    {
        Particle p;
        p.size  = 0.025f + 0.06f * rng.nextFloat() * (0.6f + 0.6f * std::min (m, 1.0f));
        p.spin  = (rng.nextFloat() - 0.5f) * 14.0f;
        p.angle = rng.nextFloat() * 6.28f;
        p.colour = debrisColour (wall);

        if (wall < 4)
        {
            const auto& def = kWalls[wall];
            const float u = 0.1f + (def.uLen - 0.2f) * rng.nextFloat();
            const float v = 0.25f + (kH - 0.35f) * rng.nextFloat();
            const V3 local = def.o + def.u * u + V3 { 0, 0, v } + def.n * 0.04f;
            const V3 n = houseDir (def.n), t = houseDir (def.u);
            p.p = houseToWorld (local);
            p.v = n * (0.4f + 1.6f * rng.nextFloat()) + V3 { 0, 0, 0.3f + 1.4f * rng.nextFloat() } + t * ((rng.nextFloat() - 0.5f) * 0.9f);
        }
        else if (wall == dsp::Ceiling)
        {
            // Tiles slide off the eaves of one side or the other.
            const float side = rng.nextBool() ? 1.0f : -1.0f;
            const float x = side * (0.2f + (0.5f * kW + kEave - 0.2f) * rng.nextFloat());
            const float y = (rng.nextFloat() - 0.5f) * kD;
            p.p = houseToWorld ({ x, y, roofTopZ (x) + 0.02f });
            p.v = houseDir ({ side * (0.6f + 1.2f * rng.nextFloat()), (rng.nextFloat() - 0.5f) * 0.6f, 0.4f + 1.2f * rng.nextFloat() });
        }
        else
        {
            // Clods jump out of the opening lawn.
            const auto& c = ground.cracks.empty() ? Crack { {}, {}, 0.0f } : ground.cracks[(size_t) rng.nextInt ((int) ground.cracks.size())];
            p.p = { c.a.x - kSlabX, c.a.y - kSlabY, 0.02f };
            p.v = { (rng.nextFloat() - 0.5f) * 0.8f, (rng.nextFloat() - 0.5f) * 0.8f, 0.6f + 1.4f * rng.nextFloat() * m };
            p.size *= 0.8f;
        }
        particles.push_back (p);
    }

    // a puff where it broke
    if (wall < 4)
    {
        const auto& def = kWalls[wall];
        spawnDust (houseToWorld (def.o + def.u * (0.5f * def.uLen) + V3 { 0, 0, 0.9f } + def.n * 0.2f), 2 + (int) (3.0f * m), 1.4f);
    }
    else if (wall == dsp::Ceiling)
    {
        const float side = rng.nextBool() ? 1.0f : -1.0f;
        spawnDust (houseToWorld ({ side * (0.5f * kW + kEave), 0.0f, kH }), 2 + (int) (2.0f * m), 1.2f);
    }
    else
        spawnDust ({ 0.0f, -0.6f, 0.1f }, 2, 2.0f);

    // keep the rubble count bounded: drop the oldest settled pieces first
    int solid = 0;
    for (const auto& p : particles) solid += p.dust ? 0 : 1;
    for (auto it = particles.begin(); solid > 420 && it != particles.end();)
    {
        if (! it->dust && it->settled) { it = particles.erase (it); --solid; }
        else ++it;
    }
}

void HouseView::tick (float dt)
{
    // camera
    if (returning)
    {
        float diff = std::remainder (kDefaultYaw - yaw, juce::MathConstants<float>::twoPi);
        yaw += diff * 0.18f;
        if (std::abs (diff) < 0.002f) { yaw = kDefaultYaw; returning = false; }
    }
    else if (! dragging && std::abs (yawVelocity) > 1.0e-4f)
    {
        yaw += yawVelocity;
        yawVelocity *= 0.88f;
    }

    // shake: fracture shock, and walls loaded past their yield point tremble
    const float stress = live.maxStress();
    shakeAmount = scale / 60.0f * (3.0f * live.frame.shock + 1.4f * std::max (0.0f, stress - 0.9f));
    shake = shakeAmount > 0.05f ? P2 ((rng.nextFloat() - 0.5f) * shakeAmount, (rng.nextFloat() - 0.5f) * shakeAmount) : P2();

    const bool frozen = live.frame.frozen;

    // new fractures → debris (not for ones that happened before the window opened)
    warmUp = std::max (0.0f, warmUp - dt);
    for (int w = 0; w < dsp::kNumWalls; ++w)
        if (const float m = live.pendingFracture[(size_t) w]; m > 0.02f)
        {
            if (! frozen && warmUp <= 0.0f && live.frame.integrity[(size_t) w] < 0.995f)
                spawnFracture (w, m);
            live.pendingFracture[(size_t) w] = 0.0f;
        }

    if (! frozen)
    {
        // Loose walls keep shedding grit while there is rubble energy; overloaded walls smoke a little.
        const float trickle = live.frame.rubble * 3.0f * dt;
        for (int w = 0; w < 4; ++w)
        {
            const float d = damageOf (w);
            if (d > 0.3f && rng.nextFloat() < trickle * d)
            {
                Particle p;
                const auto& def = kWalls[w];
                p.p = houseToWorld (def.o + def.u * (def.uLen * rng.nextFloat()) + V3 { 0, 0, 0.6f + 1.2f * rng.nextFloat() } + def.n * 0.03f);
                p.v = houseDir (def.n) * (0.1f + 0.3f * rng.nextFloat());
                p.size = 0.02f + 0.02f * rng.nextFloat();
                p.colour = debrisColour (w);
                particles.push_back (p);
            }
            if (live.frame.stressRatio[(size_t) w] > 1.0f && rng.nextFloat() < (live.frame.stressRatio[(size_t) w] - 1.0f) * 4.0f * dt)
            {
                const auto& def = kWalls[w];
                spawnDust (houseToWorld (def.o + def.u * (def.uLen * rng.nextFloat()) + V3 { 0, 0, kH * rng.nextFloat() } + def.n * 0.1f), 1, 0.3f);
            }
        }

        // physics
        for (auto& p : particles)
        {
            p.age += dt;
            if (p.dust)
            {
                p.p = p.p + p.v * dt;
                p.size += 0.12f * dt;
                continue;
            }
            if (p.fading) p.alpha -= 1.6f * dt;
            if (p.settled) continue;

            p.v.z -= 9.8f * dt;
            p.p = p.p + p.v * dt;
            p.angle += p.spin * dt;

            const bool onLawn = std::abs (p.p.x) < kSlabX && std::abs (p.p.y) < kSlabY;
            const float floorZ = onLawn ? 0.0f : -kSlabT;
            const bool insideHouse = std::abs (p.p.x) < 0.5f * kW && std::abs (p.p.y - kHouseY) < 0.5f * kD && p.p.z < kH;
            if (insideHouse && p.p.z < kH * 0.9f)
            {
                p.alpha = 0.0f;   // fell in through the roof: gone from view
                continue;
            }
            if (p.p.z < floorZ)
            {
                p.p.z = floorZ;
                p.v = { p.v.x * 0.5f, p.v.y * 0.5f, -p.v.z * 0.28f };
                p.spin *= 0.5f;
                if (std::abs (p.v.z) < 0.35f && std::hypot (p.v.x, p.v.y) < 0.35f)
                    p.settled = true;
            }
            if (p.p.z < -8.0f) p.alpha = 0.0f;
        }
    }

    // Healing clears the rubble: the more the room recovers, the more of it disappears.
    const float meanI = live.meanIntegrity();
    if (meanI > 0.97f && meanI - lastMeanIntegrity > 0.05f)   // Rebuild or a new preset: a clean lawn at once
    {
        particles.clear();
        live.pendingFracture.fill (0.0f);
    }
    if (meanI > lastMeanIntegrity + 0.002f || meanI > 0.985f)
    {
        const float clearChance = meanI > 0.985f ? 1.0f : std::min (1.0f, (meanI - lastMeanIntegrity) * 6.0f);
        for (auto& p : particles)
            if (! p.dust && p.settled && ! p.fading && rng.nextFloat() < clearChance)
                p.fading = true;
    }
    lastMeanIntegrity = meanI;

    particles.erase (std::remove_if (particles.begin(), particles.end(),
                                     [] (const Particle& p) { return p.alpha <= 0.0f || (p.dust && p.age > p.life); }),
                     particles.end());
}

//==============================================================================
// Painting
//==============================================================================
void HouseView::paint (juce::Graphics& g)
{
    for (auto& v : hitVisible) v = false;

    drawBackdrop (g);
    drawShadow (g);
    drawGround (g);

    drawBushes (g, false);
    drawParticles (g, false, false);

    // The body: a dark footprint first (shows when the house lifts), then the walls that face us.
    {
        juce::Path foot;
        const V3 c[] = { { -0.5f * kW, -0.5f * kD + kHouseY, 0 }, { 0.5f * kW, -0.5f * kD + kHouseY, 0 },
                         { 0.5f * kW, 0.5f * kD + kHouseY, 0 }, { -0.5f * kW, 0.5f * kD + kHouseY, 0 } };
        g.setColour (juce::Colour (0xff2a231f));
        g.fillPath (quad (toScreen (c[0]), toScreen (c[1]), toScreen (c[2]), toScreen (c[3])));
    }
    for (int w = 0; w < 4; ++w)
        drawWall (g, w);

    // Roof: the far plane first; the chimney sits on the east plane.
    const bool eastNear = std::sin (yaw) > 0.0f;
    if (eastNear) { drawRoof (g, false); drawRoof (g, true); drawChimney (g); }
    else          { drawRoof (g, true); drawChimney (g); drawRoof (g, false); }

    drawBushes (g, true);
    drawParticles (g, true, false);
    drawParticles (g, true, true);
    drawOverlay (g);
}

void HouseView::drawBackdrop (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto& bd = kBackdrops[juce::jlimit (0, 6, (int) voidParam->load())];
    g.setGradientFill (juce::ColourGradient (juce::Colour (bd.top), 0.0f, 0.0f, juce::Colour (bd.bottom), 0.0f, r.getBottom(), false));
    g.fillRect (r);

    const float breach = [this] { float b = 0.0f; for (float I : live.frame.integrity) b += dsp::breachAmount (I) / 6.0f; return b; }();
    juce::Random scenery (99);
    switch (bd.scenery)
    {
        case 1:   // arches: a hall or tunnel around the house
        {
            g.setColour (juce::Colour (bd.bottom).darker (0.12f).withAlpha (0.5f));
            for (int k = 0; k < 5; ++k)
            {
                const float w = r.getWidth() * (0.6f + 0.25f * (float) k);
                juce::Path arch;
                arch.addCentredArc (r.getCentreX(), r.getBottom() * 0.95f, w * 0.5f, r.getHeight() * (0.62f + 0.12f * (float) k), 0.0f,
                                    -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
                g.strokePath (arch, juce::PathStrokeType (1.5f));
            }
            break;
        }
        case 2:   // skyline far away
        {
            juce::Path city;
            float x = 0.0f;
            city.startNewSubPath (0.0f, r.getBottom());
            while (x < r.getWidth())
            {
                const float w = 18.0f + 40.0f * scenery.nextFloat();
                const float h = r.getHeight() * (0.12f + 0.3f * scenery.nextFloat());
                city.lineTo (x, r.getBottom() - h);
                city.lineTo (x + w, r.getBottom() - h);
                x += w;
            }
            city.lineTo (r.getWidth(), r.getBottom());
            city.closeSubPath();
            g.setColour (juce::Colour (bd.top).darker (0.25f).withAlpha (0.55f));
            g.fillPath (city);
            break;
        }
        case 3:   // the undercroft fills with water as the walls let it in
        {
            const float level = r.getBottom() - r.getHeight() * (0.12f + 0.3f * breach);
            g.setColour (juce::Colour (0xff3f7478).withAlpha (0.35f));
            g.fillRect (r.withTop (level));
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawHorizontalLine ((int) level, 0.0f, r.getRight());
            break;
        }
        case 4:   // abyss: faint motes
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            for (int k = 0; k < 60; ++k)
                g.fillEllipse (r.getWidth() * scenery.nextFloat(), r.getHeight() * scenery.nextFloat(), 1.6f, 1.6f);
            break;
        }
        default: break;
    }
}

void HouseView::drawShadow (juce::Graphics& g)
{
    // Soft shadow of lawn + house on the surface the lawn sits on.
    auto onto = [] (V3 p, float z0) { return p - kLight * ((p.z - z0) / kLight.z); };
    std::vector<P2> pts;
    for (float x : { -kSlabX, kSlabX })
        for (float y : { -kSlabY, kSlabY })
        {
            pts.push_back (toScreen (onto ({ x, y, 0.0f }, -kSlabT)));
            pts.push_back (toScreen ({ x, y, -kSlabT }));
        }
    for (float x : { -0.5f * kW - kEave, 0.5f * kW + kEave })
        for (float y : { -0.5f * kD, 0.5f * kD })
            pts.push_back (toScreen (onto (houseToWorld ({ x, y, kH }), -kSlabT)));
    pts.push_back (toScreen (onto (houseToWorld ({ 0, 0, kH + kR }), -kSlabT)));
    const auto hull = convexHull (pts);
    juce::DropShadow (juce::Colours::black.withAlpha (0.22f), juce::roundToInt (0.35f * scale), { 0, juce::roundToInt (0.06f * scale) })
        .drawForPath (g, hull);
}

void HouseView::drawGround (juce::Graphics& g)
{
    const V3 cam = toCamera();
    const float d = damageOf (dsp::Floor);

    // sides
    struct Side { V3 a, b, n; };
    const Side sides[] = {
        { { -kSlabX, -kSlabY, 0 }, { kSlabX, -kSlabY, 0 }, { 0, -1, 0 } },
        { { kSlabX, -kSlabY, 0 }, { kSlabX, kSlabY, 0 }, { 1, 0, 0 } },
        { { kSlabX, kSlabY, 0 }, { -kSlabX, kSlabY, 0 }, { 0, 1, 0 } },
        { { -kSlabX, kSlabY, 0 }, { -kSlabX, -kSlabY, 0 }, { -1, 0, 0 } },
    };
    for (const auto& s : sides)
    {
        if (dot (s.n, cam) <= 0.0f) continue;
        const V3 down { 0, 0, -kSlabT };
        const auto face = quad (toScreen (s.a), toScreen (s.b), toScreen (s.b + down), toScreen (s.a + down));
        g.setColour (juce::Colour (kGrassSide).withMultipliedBrightness (shadeFor (s.n)));
        g.fillPath (face);
        // a band of soil at the bottom
        const V3 soilTop { 0, 0, -kSlabT * 0.55f };
        g.setColour (juce::Colour (0xff6b5038).withMultipliedBrightness (shadeFor (s.n)));
        g.fillPath (quad (toScreen (s.a + soilTop), toScreen (s.b + soilTop), toScreen (s.b + down), toScreen (s.a + down)));
    }

    // top
    const auto T = planeTransform ({ -kSlabX, -kSlabY, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, false);
    juce::Path top;
    top.addRectangle (0.0f, 0.0f, 2.0f * kSlabX, 2.0f * kSlabY);
    g.setColour (juce::Colour (kGrass).interpolatedWith (juce::Colour (kGrassDry), smooth (0.3f, 0.9f, d)).withMultipliedBrightness (shadeFor ({ 0, 0, 1 })));
    g.fillPath (top, T);

    g.saveState();
    g.reduceClipRegion (top, T);
    {
        // mowing stripes
        juce::Path stripes;
        for (float u = 0.0f; u < 2.0f * kSlabX; u += 0.7f)
            stripes.addRectangle (u, 0.0f, 0.35f, 2.0f * kSlabY);
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.fillPath (stripes, T);

        // fissures in the lawn (Floor)
        juce::Path thin, wide;
        for (const auto& c : ground.cracks)
        {
            if (c.th >= d) continue;
            auto& p = (d - c.th) > 0.22f ? wide : thin;
            p.startNewSubPath (c.a);
            p.lineTo (c.b);
        }
        const float px = scale / 60.0f;
        g.setColour (juce::Colour (0xff3f2c1e).withAlpha (0.75f));
        g.strokePath (thin, juce::PathStrokeType (0.9f * px), T);
        g.setColour (juce::Colour (kSoil));
        g.strokePath (wide, juce::PathStrokeType ((1.4f + 2.2f * d) * px, juce::PathStrokeType::mitered, juce::PathStrokeType::butt), T);
        g.setColour (juce::Colour (0xff1e140d));
        g.strokePath (wide, juce::PathStrokeType (0.8f * px), T);

        // the house's shadow on the lawn
        auto onto = [] (V3 p) { return p - kLight * (p.z / kLight.z); };
        std::vector<P2> pts;
        for (float x : { -0.5f * kW - kEave, 0.5f * kW + kEave })
            for (float y : { -0.5f * kD - kGable, 0.5f * kD + kGable })
                for (float z : { 0.0f, kH })
                    pts.push_back (toScreen (onto (houseToWorld ({ x, y, z }))));
        for (float y : { -0.5f * kD - kGable, 0.5f * kD + kGable })
            pts.push_back (toScreen (onto (houseToWorld ({ 0.0f, y, kH + kR }))));
        g.setColour (juce::Colours::black.withAlpha (0.13f));
        g.fillPath (convexHull (pts));

        // stepping stones from the door to the edge of the lawn
        const float doorX = -0.5f * kW + 0.925f;
        juce::Random stones (8);
        juce::Path path;
        for (float y = -0.5f * kD + kHouseY - 0.62f; y > -kSlabY + 0.15f; y -= 0.42f)
        {
            const float jx = (stones.nextFloat() - 0.5f) * 0.12f;
            path.addRoundedRectangle (doorX + jx - 0.2f + kSlabX, y - 0.13f + kSlabY, 0.4f, 0.27f, 0.07f);
        }
        g.setColour (juce::Colour (0xffd1cbc0));
        g.fillPath (path, T);
        g.setColour (juce::Colour (0xffa59e92));
        g.strokePath (path, juce::PathStrokeType (1.0f), T);

        // compass in the front corner: N points north
        {
            const P2 base (2.0f * kSlabX - 0.45f, 0.45f);
            juce::Path arrow;
            arrow.addTriangle (base.x - 0.09f, base.y, base.x + 0.09f, base.y, base.x, base.y + 0.34f);
            g.setColour (juce::Colour (0xff3d6b2a).withAlpha (0.6f));
            g.fillPath (arrow, T);
            juce::GlyphArrangement ga;
            ga.addLineOfText (uiFont (10.0f, true), "N", 0.0f, 0.0f);
            juce::Path letter;
            ga.createPath (letter);
            const auto lb = letter.getBounds();
            const float k = 0.2f / std::max (1.0f, lb.getHeight());
            letter.applyTransform (juce::AffineTransform::translation (-lb.getCentreX(), -lb.getCentreY())
                                       .scaled (k, -k)
                                       .translated (base.x, base.y + 0.5f));
            g.fillPath (letter, T);
        }
    }
    g.restoreState();
    hitPaths[dsp::Floor] = top;
    hitPaths[dsp::Floor].applyTransform (T);
    hitVisible[dsp::Floor] = true;

    // door step (attached to the house, so it settles with it)
    {
        const float u0 = -0.5f * kW + 0.45f, u1 = -0.5f * kW + 1.4f, y0 = -0.5f * kD - 0.3f, y1 = -0.5f * kD, h = 0.08f;
        const V3 tl = houseToWorld ({ u0, y0, h }), tr = houseToWorld ({ u1, y0, h }), br = houseToWorld ({ u1, y1, h }), bl = houseToWorld ({ u0, y1, h });
        const V3 dn = houseDir ({ 0, 0, -h });
        g.setColour (juce::Colour (0xffcfc8bc).withMultipliedBrightness (shadeFor ({ 0, -1, 0 })));
        if (dot (houseDir ({ 0, -1, 0 }), cam) > 0.0f)
            g.fillPath (quad (toScreen (tl), toScreen (tr), toScreen (tr + dn), toScreen (tl + dn)));
        g.setColour (juce::Colour (0xffded8cd));
        g.fillPath (quad (toScreen (tl), toScreen (tr), toScreen (br), toScreen (bl)));
    }
}

void HouseView::drawSurfaceDamage (juce::Graphics& g, const Surface& s, float d, const juce::AffineTransform& T,
                                   juce::Colour under, int underKind, float uLen, float vLen)
{
    if (d <= 0.01f) return;
    const float px = scale / 60.0f;

    // cracks: three widths, so older cracks read as wider
    juce::Path thin, mid, wide;
    for (const auto& c : s.cracks)
    {
        if (c.th >= d) continue;
        const float age = d - c.th;
        auto& p = age < 0.1f ? thin : (age < 0.3f ? mid : wide);
        p.startNewSubPath (c.a);
        p.lineTo (c.b);
    }
    const auto crackCol = juce::Colour (0xff2b2522);
    g.setColour (crackCol.withAlpha (0.55f));
    g.strokePath (thin, juce::PathStrokeType (0.7f * px), T);
    g.setColour (crackCol.withAlpha (0.8f));
    g.strokePath (mid, juce::PathStrokeType (1.3f * px, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), T);
    g.setColour (crackCol);
    g.strokePath (wide, juce::PathStrokeType (2.2f * px, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), T);

    // spalls: the finish falls away and shows what is underneath
    juce::Path spalls;
    for (const auto& b : s.spalls)
        if (b.th < d)
            spalls.addPath (blobPath (b, 0.55f + 0.45f * smooth (b.th, b.th + 0.2f, d)));
    if (! spalls.isEmpty())
    {
        g.saveState();
        g.reduceClipRegion (spalls, T);
        g.setColour (under);
        g.fillPath (spalls, T);
        juce::Path lines;
        switch (underKind)
        {
            case Brick:
                for (float v = 0.0f, row = 0.0f; v < vLen; v += 0.075f, row += 1.0f)
                {
                    lines.startNewSubPath (0.0f, v); lines.lineTo (uLen, v);
                    for (float u = std::fmod (row, 2.0f) * 0.11f; u < uLen; u += 0.22f)
                    { lines.startNewSubPath (u, v); lines.lineTo (u, v + 0.075f); }
                }
                g.setColour (juce::Colour (0xffd9cbbd).withAlpha (0.8f));
                break;
            case Rubble:
                for (float v = 0.0f, row = 0.0f; v < vLen; v += 0.17f, row += 1.0f)
                {
                    lines.startNewSubPath (0.0f, v); lines.lineTo (uLen, v);
                    for (float u = std::fmod (row * 0.37f, 0.6f); u < uLen; u += 0.31f + 0.12f * std::fmod (u * 7.0f, 1.0f))
                    { lines.startNewSubPath (u, v); lines.lineTo (u, v + 0.17f); }
                }
                g.setColour (under.darker (0.5f));
                break;
            case Concrete:
                for (float v = 0.09f; v < vLen; v += 0.18f) { lines.startNewSubPath (0.0f, v); lines.lineTo (uLen, v); }
                for (float u = 0.09f; u < uLen; u += 0.18f) { lines.startNewSubPath (u, 0.0f); lines.lineTo (u, vLen); }
                g.setColour (juce::Colour (0xff6a4330).withAlpha (0.85f));   // rusty rebar
                break;
            case Studs:
                for (float u = 0.0f; u < uLen; u += 0.4f) lines.addRectangle (u, 0.0f, 0.06f, vLen);
                g.setColour (juce::Colour (0xff4a3426));
                g.fillPath (lines, T);
                lines.clear();
                break;
            default:
                break;
        }
        if (! lines.isEmpty())
            g.strokePath (lines, juce::PathStrokeType (underKind == Concrete ? 1.4f * px : 0.8f * px), T);
        g.restoreState();
        g.setColour (crackCol.withAlpha (0.35f));
        g.strokePath (spalls, juce::PathStrokeType (0.8f * px), T);
    }

    // holes right through the wall
    juce::Path holes, rims;
    for (const auto& b : s.holes)
        if (b.th < d)
        {
            const float grow = 0.45f + 0.55f * smooth (b.th, b.th + 0.25f, d);
            holes.addPath (blobPath (b, grow));
            rims.addPath (blobPath (b, grow * 1.14f));
        }
    if (! holes.isEmpty())
    {
        g.setColour (under.darker (0.25f));
        g.fillPath (rims, T);
        g.saveState();
        g.reduceClipRegion (holes, T);
        drawInterior (g);
        g.restoreState();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.strokePath (holes, juce::PathStrokeType (1.0f * px), T);
    }
}

void HouseView::drawInterior (juce::Graphics& g)
{
    // Inside the house: dim, lit only by the holes. Far walls show their inner faces,
    // and their windows show daylight from the other side.
    const V3 cam = toCamera();
    const auto& bd = kBackdrops[juce::jlimit (0, 6, (int) voidParam->load())];
    g.fillAll (juce::Colour (0xff241e1b));

    // underside of the far roof plane
    for (int side = 0; side < 2; ++side)
    {
        const float sg = side == 0 ? 1.0f : -1.0f;
        const V3 nTop = houseDir ({ sg * std::sin (kPhi), 0.0f, std::cos (kPhi) });
        if (dot (nTop, cam) >= 0.0f) continue;   // only the plane whose top faces away
        const float xe = 0.5f * kW, y0 = -0.5f * kD, y1 = 0.5f * kD;
        juce::Path p = quad (toScreen (houseToWorld ({ 0, y0, kH + kR })), toScreen (houseToWorld ({ 0, y1, kH + kR })),
                             toScreen (houseToWorld ({ sg * xe, y1, kH })), toScreen (houseToWorld ({ sg * xe, y0, kH })));
        g.setColour (juce::Colour (0xff3e2f26));
        g.fillPath (p);
        juce::Path rafters;
        for (float y = y0 + 0.3f; y < y1; y += 0.45f)
        {
            rafters.startNewSubPath (toScreen (houseToWorld ({ 0, y, kH + kR })));
            rafters.lineTo (toScreen (houseToWorld ({ sg * xe, y, kH })));
        }
        g.setColour (juce::Colour (0xff2a1f19));
        g.strokePath (rafters, juce::PathStrokeType (std::max (1.0f, 0.05f * scale)));
    }

    // inner faces of the far walls
    for (int w = 0; w < 4; ++w)
    {
        const auto& def = kWalls[w];
        if (dot (houseDir (def.n), cam) >= 0.0f) continue;
        const auto T = planeTransform (def.o, def.u, { 0, 0, 1 }, true);
        const float lit = 0.42f + 0.12f * std::max (0.0f, dot (houseDir (def.n) * -1.0f, kLight));
        g.setColour (juce::Colour (0xffb9b0a5).withMultipliedBrightness (lit));
        g.fillPath (wallOutline (def), T);
        for (const auto& o : def.openings)
        {
            juce::Path light;
            light.addRectangle (o.u0, o.v0, o.u1 - o.u0, o.v1 - o.v0);
            g.setColour (o.door ? juce::Colour (0xff6b4a2c) : juce::Colour (bd.top));
            g.fillPath (light, T);
        }
        // skirting shadow where wall meets floor
        juce::Path skirt;
        skirt.addRectangle (0.0f, 0.0f, def.uLen, 0.12f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillPath (skirt, T);
    }

    // floorboards
    {
        const auto T = planeTransform ({ -0.5f * kW, -0.5f * kD, 0.005f }, { 1, 0, 0 }, { 0, 1, 0 }, true);
        juce::Path floor;
        floor.addRectangle (0.0f, 0.0f, kW, kD);
        g.setColour (juce::Colour (0xff6a5442));
        g.fillPath (floor, T);
        juce::Path boards;
        for (float u = 0.18f; u < kW; u += 0.18f) { boards.startNewSubPath (u, 0.0f); boards.lineTo (u, kD); }
        g.setColour (juce::Colour (0xff4c3a2d));
        g.strokePath (boards, juce::PathStrokeType (std::max (0.8f, 0.012f * scale)), T);
    }
}

void HouseView::drawWall (juce::Graphics& g, int w)
{
    const auto& def = kWalls[w];
    const V3 n = houseDir (def.n);
    if (dot (n, toCamera()) <= 0.005f)
        return;

    const auto T = planeTransform (def.o, def.u, { 0, 0, 1 }, true);
    const auto outline = wallOutline (def);
    hitPaths[(size_t) w] = outline;
    hitPaths[(size_t) w].applyTransform (T);
    hitVisible[(size_t) w] = true;

    const auto& f = kFinishes[juce::jlimit (0, 10, (int) materialParam->load())];
    const float d = damageOf (w);
    const float sh = shadeFor (n);
    const float vLen = def.gable ? kH + kR : kH;
    const float px = scale / 60.0f;

    g.saveState();
    g.reduceClipRegion (outline, T);

    // finish
    if (f.pattern == Glass)
    {
        g.setColour (juce::Colour (kInterior));
        g.fillPath (outline, T);
        g.setColour (juce::Colour (f.wall).withMultipliedBrightness (sh).withAlpha (0.72f));
    }
    else
        g.setColour (juce::Colour (f.wall).withMultipliedBrightness (sh));
    g.fillPath (outline, T);

    {
        juce::Path lines;
        switch (f.pattern)
        {
            case Tiles: case Glass: case Plates:
                for (float v = f.b; v < vLen; v += f.b) { lines.startNewSubPath (0.0f, v); lines.lineTo (def.uLen, v); }
                for (float u = f.a; u < def.uLen; u += f.a) { lines.startNewSubPath (u, 0.0f); lines.lineTo (u, vLen); }
                break;
            case Ribs:
                for (float u = f.a * 0.5f; u < def.uLen; u += f.a) { lines.startNewSubPath (u, 0.0f); lines.lineTo (u, vLen); }
                break;
            case Planks:
                for (float v = f.a; v < vLen; v += f.a) { lines.startNewSubPath (0.0f, v); lines.lineTo (def.uLen, v); }
                break;
            case Blocks:
                for (float v = 0.0f, row = 0.0f; v < vLen; v += f.b, row += 1.0f)
                {
                    lines.startNewSubPath (0.0f, v); lines.lineTo (def.uLen, v);
                    for (float u = std::fmod (row, 2.0f) * f.a * 0.5f; u < def.uLen; u += f.a)
                    { lines.startNewSubPath (u, v); lines.lineTo (u, v + f.b); }
                }
                break;
            case Plain: default: break;
        }
        g.setColour (juce::Colour (f.line).withMultipliedBrightness (sh).withAlpha (f.pattern == Glass ? 0.9f : 0.7f));
        g.strokePath (lines, juce::PathStrokeType ((f.pattern == Glass || f.pattern == Plates ? 1.6f : 0.8f) * px), T);
        if (f.pattern == Plates)
        {
            g.setColour (juce::Colour (f.line).darker (0.3f));
            for (float v = f.b * 0.5f - 0.2f; v < vLen; v += f.b)
                for (float u = 0.06f; u < def.uLen; u += f.a)
                {
                    juce::Path rivet;
                    rivet.addEllipse (u - 0.015f, v - 0.015f, 0.03f, 0.03f);
                    g.fillPath (rivet, T);
                }
        }
    }

    // eave shadow along the top of the wall
    {
        juce::Path band;
        band.addRectangle (0.0f, kH - 0.3f, def.uLen, 0.3f + kR);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), P2 (0.0f, kH - 0.3f).transformedBy (T),
                                                 juce::Colours::black.withAlpha (0.12f), P2 (0.0f, kH).transformedBy (T), false));
        g.fillPath (band, T);
    }

    // openings
    for (const auto& o : def.openings)
    {
        const juce::Rectangle<float> hole (o.u0, o.v0, o.u1 - o.u0, o.v1 - o.v0);
        juce::Path trim;
        auto trimArea = hole.expanded (0.06f);
        if (o.door) trimArea = trimArea.withTop (0.0f);
        trim.addRectangle (trimArea);
        g.setColour (juce::Colour (kFrame).withMultipliedBrightness (sh));
        g.fillPath (trim, T);

        juce::Path inner;
        inner.addRectangle (hole);
        g.setColour (juce::Colour (kInterior));
        g.fillPath (inner, T);

        if (o.door)
        {
            if (d < 0.88f)
            {
                // the door drops on its top hinge as the wall goes
                const float swing = -0.2f * smooth (0.5f, 0.86f, d);
                const auto hinge = juce::AffineTransform::rotation (swing, o.u0 + 0.02f, o.v1 - 0.02f);
                juce::Path panel;
                panel.addRectangle (hole.reduced (0.02f));
                g.setColour (juce::Colour (kWood).withMultipliedBrightness (sh));
                g.fillPath (panel, hinge.followedBy (T));
                juce::Path planks;
                for (float u = o.u0 + 0.17f; u < o.u1 - 0.05f; u += 0.17f) { planks.startNewSubPath (u, o.v0 + 0.04f); planks.lineTo (u, o.v1 - 0.04f); }
                g.setColour (juce::Colour (kWood).darker (0.35f).withAlpha (0.6f));
                g.strokePath (planks, juce::PathStrokeType (0.8f * px), hinge.followedBy (T));
                juce::Path knob;
                knob.addEllipse (o.u1 - 0.14f, o.v0 + 0.78f, 0.06f, 0.06f);
                g.setColour (juce::Colour (0xff8a6a3a));
                g.fillPath (knob, hinge.followedBy (T));
            }
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (kGlass).brighter (0.25f), P2 (o.u0, o.v1).transformedBy (T),
                                                     juce::Colour (kGlass).darker (0.1f), P2 (o.u1, o.v0).transformedBy (T), false));
            if (d < 0.72f)
                g.fillPath (inner, T);
            else
            {
                // glass gone: a few shards left in the frame
                juce::Random r (17 + w * 5);
                juce::Path shards;
                for (int k = 0; k < 4; ++k)
                {
                    const bool vertical = k % 2 == 0;
                    const float t = 0.2f + 0.6f * r.nextFloat();
                    const P2 a = vertical ? P2 (k < 2 ? o.u0 : o.u1, o.v0 + t * (o.v1 - o.v0)) : P2 (o.u0 + t * (o.u1 - o.u0), k < 2 ? o.v0 : o.v1);
                    const P2 inwards = P2 (hole.getCentreX(), hole.getCentreY()) - a;
                    shards.addTriangle (a + P2 (inwards.y, -inwards.x) * 0.25f, a - P2 (inwards.y, -inwards.x) * 0.25f, a + inwards * (0.3f + 0.3f * r.nextFloat()));
                }
                g.fillPath (shards, T);
            }
            // glazing bars
            juce::Path bars;
            bars.addRectangle (hole.getCentreX() - 0.025f, o.v0, 0.05f, o.v1 - o.v0);
            bars.addRectangle (o.u0, hole.getCentreY() - 0.025f, o.u1 - o.u0, 0.05f);
            if (d < 0.9f)
            {
                g.setColour (juce::Colour (kFrame).withMultipliedBrightness (sh));
                g.fillPath (bars, T);
            }
            if (d > 0.42f && d < 0.72f)
            {
                // cracked pane: a star from where it was hit
                juce::Random r (31 + w);
                const P2 hit (o.u0 + (o.u1 - o.u0) * (0.25f + 0.5f * r.nextFloat()), o.v0 + (o.v1 - o.v0) * (0.25f + 0.5f * r.nextFloat()));
                juce::Path star;
                for (int k = 0; k < 7; ++k)
                {
                    const float a = juce::MathConstants<float>::twoPi * ((float) k + r.nextFloat() * 0.6f) / 7.0f;
                    star.startNewSubPath (hit);
                    star.lineTo (hit + P2 (std::cos (a), std::sin (a)) * 0.6f);
                }
                star.addEllipse (hit.x - 0.08f, hit.y - 0.08f, 0.16f, 0.16f);
                g.saveState();
                g.reduceClipRegion (inner, T);
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.strokePath (star, juce::PathStrokeType (0.8f * px), T);
                g.restoreState();
            }
        }
    }

    // round vent in the gable
    if (def.gable)
    {
        const P2 c (0.5f * def.uLen, kH + kR * 0.42f);
        juce::Path vent;
        vent.addEllipse (c.x - 0.15f, c.y - 0.15f, 0.3f, 0.3f);
        g.setColour (juce::Colour (kFrame).withMultipliedBrightness (sh));
        g.fillPath (vent, T);
        juce::Path slats;
        for (float y = c.y - 0.08f; y < c.y + 0.1f; y += 0.055f) slats.addRectangle (c.x - 0.1f, y, 0.2f, 0.022f);
        g.setColour (juce::Colour (kInterior).withAlpha (0.7f));
        g.fillPath (slats, T);
    }

    const auto under = f.underKind == Nothing ? juce::Colour (f.wall).darker (0.35f) : juce::Colour (f.under);
    drawSurfaceDamage (g, walls[(size_t) w], d, T, under.withMultipliedBrightness (sh), f.underKind, def.uLen, vLen);

    g.restoreState();

    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.strokePath (outline, juce::PathStrokeType (0.9f), T);
}

void HouseView::drawRoof (juce::Graphics& g, bool east)
{
    const float sg = east ? 1.0f : -1.0f;
    const float xe = 0.5f * kW + kEave, y0 = -0.5f * kD - kGable, y1 = 0.5f * kD + kGable;
    const float zr = kH + kR, ze = zr - kSlope * xe;
    const V3 Rt0 { 0, y0, zr + kTz }, Rt1 { 0, y1, zr + kTz }, Et0 { sg * xe, y0, ze + kTz }, Et1 { sg * xe, y1, ze + kTz };
    const V3 Rb0 { 0, y0, zr }, Rb1 { 0, y1, zr }, Eb0 { sg * xe, y0, ze }, Eb1 { sg * xe, y1, ze };
    const V3 cam = toCamera();
    const float d = damageOf (dsp::Ceiling);

    auto face = [&] (std::initializer_list<V3> pts, V3 normal, juce::Colour c)
    {
        const V3 n = houseDir (normal);
        if (dot (n, cam) <= 0.0f) return;
        juce::Path p;
        bool first = true;
        for (const auto& v : pts)
        {
            const auto s = toScreen (houseToWorld (v));
            if (first) { p.startNewSubPath (s); first = false; } else p.lineTo (s);
        }
        p.closeSubPath();
        g.setColour (c.withMultipliedBrightness (shadeFor (n)));
        g.fillPath (p);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (0.8f));
    };

    const float sinP = std::sin (kPhi), cosP = std::cos (kPhi);
    face ({ Rb0, Eb0, Et0, Rt0 }, { 0, -1, 0 }, juce::Colour (kRoofEdge));
    face ({ Rb1, Eb1, Et1, Rt1 }, { 0, 1, 0 }, juce::Colour (kRoofEdge));
    face ({ Eb0, Eb1, Et1, Et0 }, { sg * cosP, 0, -sinP }, juce::Colour (kRoofEdge));

    const V3 nTop { sg * sinP, 0, cosP };
    const V3 n = houseDir (nTop);
    if (dot (n, cam) <= 0.0f) return;

    const float slopeLen = xe / cosP, uLen = y1 - y0;
    const auto T = planeTransform (Et0, { 0, 1, 0 }, { -sg * cosP, 0, sinP }, true);
    juce::Path top;
    top.addRectangle (0.0f, 0.0f, uLen, slopeLen);
    auto& hit = hitPaths[dsp::Ceiling];
    if (! hitVisible[dsp::Ceiling]) hit.clear();
    hit.addPath (top, T);
    hitVisible[dsp::Ceiling] = true;

    const float sh = shadeFor (n);
    const float px = scale / 60.0f;
    g.saveState();
    g.reduceClipRegion (top, T);
    g.setColour (juce::Colour (kRoofRed).withMultipliedBrightness (sh));
    g.fillPath (top, T);

    const auto& th = tileThreshold[east ? 0 : 1];
    const int rows = (int) std::ceil (slopeLen / kTileV), cols = (int) std::ceil (uLen / kTileU) + 1;
    juce::Path courses, joints, missing, holes;
    for (int i = 0; i < rows; ++i)
    {
        const float v = (float) i * kTileV;
        courses.addRectangle (0.0f, v + kTileV - 0.03f, uLen, 0.03f);
        const float off = i % 2 == 1 ? 0.5f * kTileU : 0.0f;
        for (int j = 0; j < cols; ++j)
        {
            const float u = (float) j * kTileU + off - 0.5f * kTileU;
            joints.startNewSubPath (u, v);
            joints.lineTo (u, v + kTileV - 0.03f);
            const float t = th[(size_t) (i * cols + j)];
            if (t < d)
                (t + 0.3f < d ? holes : missing).addRectangle (u, v, kTileU, kTileV);
        }
    }
    g.setColour (juce::Colour (kRoofEdge).withMultipliedBrightness (sh).withAlpha (0.55f));
    g.fillPath (courses, T);
    g.strokePath (joints, juce::PathStrokeType (0.7f * px), T);
    if (! missing.isEmpty())
    {
        g.setColour (juce::Colour (kBatten).withMultipliedBrightness (sh));
        g.fillPath (missing, T);
        juce::Path battens;
        for (float v = 0.07f; v < slopeLen; v += kTileV) battens.addRectangle (0.0f, v, uLen, 0.04f);
        g.saveState();
        g.reduceClipRegion (missing, T);
        g.setColour (juce::Colour (0xff7a4a34).withMultipliedBrightness (sh));
        g.fillPath (battens, T);
        g.restoreState();
    }
    if (! holes.isEmpty())
    {
        g.saveState();
        g.reduceClipRegion (holes, T);
        drawInterior (g);
        g.restoreState();
    }
    g.restoreState();

    // ridge
    juce::Path ridge;
    ridge.startNewSubPath (toScreen (houseToWorld (Rt0)));
    ridge.lineTo (toScreen (houseToWorld (Rt1)));
    g.setColour (juce::Colour (kRoofEdge).darker (0.2f));
    g.strokePath (ridge, juce::PathStrokeType (2.0f * px));
}

void HouseView::drawChimney (juce::Graphics& g)
{
    const float d = damageOf (dsp::Ceiling);
    const float tilt = 0.32f * smooth (0.45f, 0.85f, d);
    const float topZ = kChimTop - 0.45f * smooth (0.8f, 0.98f, d);
    const float x0 = kChimX - kChimA, x1 = kChimX + kChimA, y0 = kChimY - kChimA, y1 = kChimY + kChimA;
    const V3 pivot { x1, kChimY, roofTopZ (x1) };
    const float ct = std::cos (tilt), st = std::sin (tilt);
    auto tilted = [&] (V3 p)
    {
        const float rx = p.x - pivot.x, rz = p.z - pivot.z;
        return houseToWorld ({ pivot.x + rx * ct + rz * st, p.y, pivot.z - rx * st + rz * ct });
    };
    auto dirT = [&] (V3 n) { return houseDir ({ n.x * ct + n.z * st, n.y, -n.x * st + n.z * ct }); };

    const float zb0 = roofTopZ (x0) - 0.03f, zb1 = roofTopZ (x1) - 0.03f;
    struct Face { V3 a, b, c, e; V3 n; float uLen; };
    const Face faces[] = {
        { { x1, y0, zb1 }, { x1, y1, zb1 }, { x1, y1, topZ }, { x1, y0, topZ }, { 1, 0, 0 }, 2.0f * kChimA },
        { { x0, y1, zb0 }, { x0, y0, zb0 }, { x0, y0, topZ }, { x0, y1, topZ }, { -1, 0, 0 }, 2.0f * kChimA },
        { { x0, y0, zb0 }, { x1, y0, zb1 }, { x1, y0, topZ }, { x0, y0, topZ }, { 0, -1, 0 }, 2.0f * kChimA },
        { { x1, y1, zb1 }, { x0, y1, zb0 }, { x0, y1, topZ }, { x1, y1, topZ }, { 0, 1, 0 }, 2.0f * kChimA },
    };
    const V3 cam = toCamera();
    const float px = scale / 60.0f;
    for (const auto& f : faces)
    {
        const V3 n = dirT (f.n);
        if (dot (n, cam) <= 0.0f) continue;
        const auto path = quad (toScreen (tilted (f.a)), toScreen (tilted (f.b)), toScreen (tilted (f.c)), toScreen (tilted (f.e)));
        const float sh = shadeFor (n);
        g.setColour (juce::Colour (kBrickRed).withMultipliedBrightness (sh));
        g.fillPath (path);

        // brick courses in the face's own plane
        const float baseZ = std::min (zb0, zb1) - 0.05f;
        const V3 o = tilted ({ f.a.x, f.a.y, baseZ });
        const V3 uDir = normalised (tilted ({ f.b.x, f.b.y, baseZ }) - o);
        const V3 vDir = dirT ({ 0, 0, 1 });
        const auto s0 = toScreen (o);
        const auto su = toScreen (o + uDir) - s0, sv = toScreen (o + vDir) - s0;
        const juce::AffineTransform T (su.x, sv.x, s0.x, su.y, sv.y, s0.y);
        juce::Path bricks;
        const float uLen = f.uLen;
        for (float v = 0.0f, row = 0.0f; v < topZ - baseZ; v += 0.07f, row += 1.0f)
        {
            bricks.startNewSubPath (0.0f, v); bricks.lineTo (uLen, v);
            for (float u = std::fmod (row, 2.0f) * 0.07f; u < uLen; u += 0.14f) { bricks.startNewSubPath (u, v); bricks.lineTo (u, v + 0.07f); }
        }
        g.saveState();
        g.reduceClipRegion (path);
        g.setColour (juce::Colour (0xffe2c9b8).withAlpha (0.55f));
        g.strokePath (bricks, juce::PathStrokeType (0.7f * px), T);
        g.restoreState();
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.strokePath (path, juce::PathStrokeType (0.8f));
    }

    // top with its flue
    const auto t0 = toScreen (tilted ({ x0, y0, topZ })), t1 = toScreen (tilted ({ x1, y0, topZ })),
               t2 = toScreen (tilted ({ x1, y1, topZ })), t3 = toScreen (tilted ({ x0, y1, topZ }));
    g.setColour (juce::Colour (kBrickRed).brighter (0.1f));
    g.fillPath (quad (t0, t1, t2, t3));
    const float in = 0.06f;
    g.setColour (juce::Colour (0xff2a1f1b));
    g.fillPath (quad (toScreen (tilted ({ x0 + in, y0 + in, topZ })), toScreen (tilted ({ x1 - in, y0 + in, topZ })),
                      toScreen (tilted ({ x1 - in, y1 - in, topZ })), toScreen (tilted ({ x0 + in, y1 - in, topZ }))));
}

void HouseView::drawBushes (juce::Graphics& g, bool front)
{
    struct Bush { float x, y, r; };
    static constexpr Bush bushes[] = {
        { -1.45f, -0.95f, 0.26f }, { -1.15f, -1.02f, 0.2f }, { -1.32f, -0.8f, 0.17f },
        { 0.42f, -0.95f, 0.22f }, { 0.68f, -0.98f, 0.18f }, { 0.55f, -0.82f, 0.15f },
        { 1.85f, 1.55f, 0.28f }, { 2.0f, 1.28f, 0.2f }, { -1.95f, 1.65f, 0.22f },
    };
    std::vector<const Bush*> order;
    for (const auto& b : bushes)
        if (frontOfHouse ({ b.x, b.y, 0 }) == front) order.push_back (&b);
    const float ca = std::cos (yaw), sa = std::sin (yaw);
    std::sort (order.begin(), order.end(), [&] (const Bush* a, const Bush* b)
               { return (-a->x * sa + a->y * ca) > (-b->x * sa + b->y * ca); });   // far first

    for (const auto* b : order)
    {
        const auto c = toScreen ({ b->x, b->y, b->r * 0.8f });
        const float r = b->r * scale;
        g.setColour (juce::Colour (0xff4b8535));
        g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);
        g.setColour (juce::Colour (0xff6fae4c));
        g.fillEllipse (c.x - r * 0.85f, c.y - r * 0.95f, 1.6f * r, 1.6f * r);
        g.setColour (juce::Colour (0xff8cc566).withAlpha (0.8f));
        g.fillEllipse (c.x - r * 0.62f, c.y - r * 0.78f, 0.75f * r, 0.7f * r);
    }
}

void HouseView::drawParticles (juce::Graphics& g, bool front, bool dustPass)
{
    for (const auto& p : particles)
    {
        if (p.dust != dustPass) continue;
        if (! dustPass && frontOfHouse (p.p) != front) continue;
        const auto s = toScreen (p.p);
        const float sz = std::max (1.2f, p.size * scale);
        if (p.dust)
        {
            const float a = 0.2f * (1.0f - p.age / p.life);
            g.setColour (p.colour.withAlpha (std::max (0.0f, a)));
            g.fillEllipse (s.x - sz, s.y - sz * 0.8f, 2.0f * sz, 1.6f * sz);
            continue;
        }
        juce::Path chunk;
        chunk.addTriangle (-0.5f, -0.4f, 0.55f, -0.3f, 0.1f, 0.55f);
        chunk.applyTransform (juce::AffineTransform::rotation (p.angle).scaled (sz).translated (s));
        g.setColour (p.colour.withAlpha (juce::jlimit (0.0f, 1.0f, p.alpha)));
        g.fillPath (chunk);
    }
}

void HouseView::drawOverlay (juce::Graphics& g)
{
    const auto& bd = kBackdrops[juce::jlimit (0, 6, (int) voidParam->load())];
    const bool dark = juce::Colour (bd.top).getPerceivedBrightness() < 0.45f;
    const auto ink = dark ? juce::Colour (0xffe8e2d4) : juce::Colour (0xff23272b);

    if (hover >= 0 && hitVisible[(size_t) hover])
    {
        g.setColour (ink.withAlpha (0.8f));
        g.strokePath (hitPaths[(size_t) hover], juce::PathStrokeType (1.5f));

        static const char* names[] = { "North wall", "South wall (front door)", "East wall", "West wall", "Roof (ceiling)", "Lawn (floor)" };
        const float I = live.frame.integrity[(size_t) hover], s = live.frame.stressRatio[(size_t) hover];
        g.setColour (ink);
        g.setFont (uiFont (15.0f, true));
        g.drawText (names[hover], 16, 12, 300, 20, juce::Justification::centredLeft);
        g.setFont (monoFont (13.0f));
        g.setColour (ink.withAlpha (0.75f));
        g.drawText (juce::String (juce::roundToInt (I * 100.0f)) + "% standing    load " + juce::String (s, 2) + "x yield",
                    16, 32, 320, 18, juce::Justification::centredLeft);
    }

    if (live.frame.frozen)
    {
        g.setColour (ink.withAlpha (0.8f));
        g.setFont (uiFont (13.0f, true));
        g.drawText ("Frozen", getWidth() - 116, 12, 100, 20, juce::Justification::centredRight);
    }

    if (isMouseOver() && ! dragging)
    {
        g.setColour (ink.withAlpha (0.45f));
        g.setFont (uiFont (11.5f));
        g.drawText ("Drag to turn the house. Double-click to face it again.", 16, getHeight() - 26, getWidth() - 32, 16,
                    juce::Justification::centredRight);
    }
}

//==============================================================================
// Mouse
//==============================================================================
void HouseView::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    returning = false;
    dragYaw = yaw;
    yawVelocity = 0.0f;
}

void HouseView::mouseDrag (const juce::MouseEvent& e)
{
    const float before = yaw;
    yaw = dragYaw - (float) e.getDistanceFromDragStartX() * 0.009f;
    yawVelocity = yaw - before;
    repaint();
}

void HouseView::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

void HouseView::mouseDoubleClick (const juce::MouseEvent&)
{
    returning = true;
    yawVelocity = 0.0f;
}

void HouseView::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    int found = -1;
    // top-most first: roof, walls, lawn
    if (hitVisible[dsp::Ceiling] && hitPaths[dsp::Ceiling].contains (p)) found = dsp::Ceiling;
    for (int w = 0; w < 4 && found < 0; ++w)
        if (hitVisible[(size_t) w] && hitPaths[(size_t) w].contains (p)) found = w;
    if (found < 0 && hitPaths[dsp::Floor].contains (p)) found = dsp::Floor;
    if (found != hover) { hover = found; repaint(); }
}

void HouseView::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}
} // namespace rb::ui
