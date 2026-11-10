#pragma once
//==============================================================================
// Private to the house view: the house's measurements, what each material and
// each outside looks like, and small geometry helpers. Shared by HouseView.cpp
// (damage, debris, camera) and HousePaint.cpp (drawing).
//==============================================================================
#include "HouseView.h"
#include "Theme.h"

namespace rb::ui
{
inline HouseView::V3 operator+ (HouseView::V3 a, HouseView::V3 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline HouseView::V3 operator- (HouseView::V3 a, HouseView::V3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline HouseView::V3 operator* (HouseView::V3 a, float s) noexcept          { return { a.x * s, a.y * s, a.z * s }; }

namespace house
{
    using V3 = HouseView::V3;
    using P2 = juce::Point<float>;

    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
    constexpr float kPi    = juce::MathConstants<float>::pi;

    inline float dot (V3 a, V3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline V3 normalised (V3 a) noexcept   { const float l = std::sqrt (dot (a, a)); return l > 0.0f ? a * (1.0f / l) : a; }
    inline float smooth (float e0, float e1, float x) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0));
        return t * t * (3.0f - 2.0f * t);
    }

    /** Stable 0..1 value for a set of indices: the same tile is always the odd one out. */
    inline float hash (int a, int b = 0, int c = 0) noexcept
    {
        auto h = (juce::uint32) a * 0x9E3779B1u ^ (juce::uint32) b * 0x85EBCA77u ^ (juce::uint32) c * 0xC2B2AE3Du;
        h ^= h >> 15; h *= 0x2C1B3C6Du;
        h ^= h >> 12; h *= 0x297A2D39u;
        h ^= h >> 15;
        return (float) (h & 0xFFFFFFu) / 16777216.0f;
    }

    //--------------------------------------------------------------------------
    // Geometry (metres). House-local origin: centre of the footprint, z = 0 on the lawn.
    constexpr float kW = 3.0f, kD = 2.2f, kH = 2.0f, kR = 1.3f;       // body and ridge rise
    constexpr float kEave = 0.24f, kGable = 0.16f, kRoofT = 0.12f;    // roof overhangs, thickness
    constexpr float kSlabX = 2.2f, kSlabY = 2.1f, kSlabT = 0.34f;     // lawn (world, centred)
    constexpr float kHouseY = 0.4f;                                   // house sits towards the back
    constexpr float kChimX = 0.62f, kChimY = 0.35f, kChimA = 0.2f, kChimTop = kH + kR + 0.62f;
    constexpr float kElev = 0.5236f;                                  // 30° camera elevation
    constexpr float kDefaultYaw = 0.7854f;                            // camera to the south-east
    constexpr float kTileV = 0.2f, kTileU = 0.28f;
    constexpr float kPlinth = 0.15f;

    inline const float kPhi   = std::atan (kR / (0.5f * kW));
    inline const float kSlope = kR / (0.5f * kW);
    inline const float kTz    = kRoofT / std::cos (kPhi);
    inline const V3 kLight    = normalised ({ -0.45f, -0.85f, 1.1f });   // towards the light: front-left, above

    inline float roofTopZ (float x) noexcept { return kH + kR + kTz - kSlope * std::abs (x); }

    struct Opening { float u0, v0, u1, v1; bool door; };
    struct WallDef
    {
        V3 o, u, n;
        float uLen;
        bool gable;
        std::vector<Opening> openings;
        float height() const noexcept { return gable ? kH + kR : kH; }
    };

    // dsp::Wall order: North, South, East, West. Seen from outside, u runs to the right.
    inline const WallDef kWalls[4] = {
        { { 0.5f * kW, 0.5f * kD, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, kW, true,  { { 1.05f, 0.8f, 1.95f, 1.45f, false } } },
        { { -0.5f * kW, -0.5f * kD, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, kW, true,
          { { 0.55f, 0.0f, 1.3f, 1.6f, true }, { 1.85f, 0.78f, 2.5f, 1.48f, false } } },
        { { 0.5f * kW, -0.5f * kD, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, kD, false, { { 0.78f, 0.78f, 1.42f, 1.48f, false } } },
        { { -0.5f * kW, 0.5f * kD, 0 }, { 0, -1, 0 }, { -1, 0, 0 }, kD, false, { { 0.82f, 0.85f, 1.38f, 1.45f, false } } },
    };

    inline juce::Path wallOutline (const WallDef& w)
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
    // What the walls are made of (follows the Material choice).
    enum Pattern   { Plain, Tiles, Ribs, Blocks, Planks, Glass, Plates };
    enum Under     { Brick, Concrete, Studs, Nothing, Rubble };
    enum SpallMode { SpallBlob,     // patches of finish fall away and show what is underneath
                     SpallCells,    // the finish comes off piece by piece (tiles, panes, boards)
                     SpallStain };  // thin skins don't spall: they scorch and rust
    enum HoleMode  { HoleBlob,      // a ragged breach
                     HoleCells };   // whole stones, panes or plates go missing
    enum Bit       { Chunk, Tile, Shard, Splinter, Clod, Scrap };

    struct Finish
    {
        juce::uint32 wall, line;
        Pattern pattern;
        float a, b;                 // cell width and height (or rib / board pitch)
        juce::uint32 under;
        Under underKind;
        juce::uint32 debris;
        SpallMode spall;
        HoleMode hole;
        float thickness;            // how deep the cut edge of a hole looks
        Bit bit;                    // what flies off
    };

    inline const Finish kFinishes[] = {
        { 0xfff1eee8, 0xffddd8cf, Plain,  0.0f,  0.0f,   0xff9c978e, Concrete, 0xffd8d3ca, SpallBlob,  HoleBlob,  0.12f, Chunk },    // Concrete Bunker
        { 0xfff5f3ee, 0xffd3cdc2, Tiles,  0.22f, 0.22f,  0xffb35c3e, Brick,    0xffede8e0, SpallCells, HoleBlob,  0.11f, Tile },     // Ceramic Chamber
        { 0xffc3c9c4, 0xff9ea6a1, Ribs,   0.08f, 0.0f,   0xff6f7771, Nothing,  0xffb4bab5, SpallStain, HoleBlob,  0.02f, Scrap },    // Corrugated Silo
        { 0xffa9654a, 0xff7e4532, Ribs,   0.11f, 0.0f,   0xff4f2a1e, Nothing,  0xff8c5038, SpallStain, HoleBlob,  0.02f, Scrap },    // Rusted Hangar
        { 0xff8d8a84, 0xff6f6c66, Blocks, 0.9f,  0.45f,  0xff66635e, Concrete, 0xff85827c, SpallBlob,  HoleBlob,  0.15f, Chunk },    // Brutalist Monolith
        { 0xffbfdde6, 0xff7f98a0, Glass,  0.55f, 0.66f,  0xff2c3a40, Nothing,  0xffd8eef4, SpallCells, HoleCells, 0.03f, Shard },    // Shattered Glass Atrium
        { 0xffe2d6bd, 0xffbcae92, Blocks, 0.42f, 0.24f,  0xffa8987a, Rubble,   0xffd5c8ac, SpallBlob,  HoleCells, 0.16f, Chunk },    // Cathedral Stone
        { 0xfff2f2ee, 0xffcbcbc3, Blocks, 0.15f, 0.075f, 0xff9a968f, Concrete, 0xffe6e6e0, SpallCells, HoleBlob,  0.11f, Tile },     // Subway Tile Tunnel
        { 0xffd3ecf2, 0xffa3cbd7, Blocks, 0.6f,  0.4f,   0xff8db7c4, Nothing,  0xffe8f6fa, SpallStain, HoleBlob,  0.12f, Shard },    // Cracked Ice Sheet
        { 0xff5b4335, 0xff3a2a21, Planks, 0.8f,  0.17f,  0xff261b15, Studs,    0xff4a372b, SpallCells, HoleCells, 0.06f, Splinter }, // Burnt Timber Frame
        { 0xff506b6f, 0xff38494c, Plates, 0.7f,  0.5f,   0xff2b3b3e, Nothing,  0xff5c7a7e, SpallStain, HoleCells, 0.03f, Scrap },    // Sunken Bulkhead
    };
    constexpr int kNumFinishes = 11;

    inline bool bonded (Pattern p) noexcept { return p == Blocks || p == Planks; }

    //--------------------------------------------------------------------------
    // What is outside (follows the Outside choice): sky colours and the light the house sits in.
    // The index is the scenery: 0 open sky, 1 hall, 2 tunnel, 3 abyss, 4 burning city, 5 flooded, 6 canyon.
    struct Backdrop { juce::uint32 top, bottom, ambient, sun; };
    inline const Backdrop kBackdrops[] = {
        { 0xffcfe6ec, 0xffb3d3db, 0xffb3bac7, 0xff6b614d },   // Free Field
        { 0xffb9c3cd, 0xff97a3af, 0xffa8b0bd, 0xff66615a },   // Infinite Secondary Tail
        { 0xff9f9991, 0xff807a73, 0xff9e998f, 0xff615c4d },   // Industrial Tunnel
        { 0xff28313c, 0xff12161c, 0xff5f6d85, 0xff4d5a70 },   // Abyssal Drone
        { 0xff70402e, 0xff34201c, 0xff966a5c, 0xff8c5c33 },   // Burning City
        { 0xff8ab0b0, 0xff5c898b, 0xff8cadad, 0xff61614f },   // Flooded Undercroft
        { 0xffcdc5b7, 0xffada594, 0xffb3ada3, 0xff665e4d },   // Concrete Canyon
    };
    constexpr int kNumBackdrops = 7;

    constexpr juce::uint32 kRoofRed = 0xffd94a3c, kRoofEdge = 0xffa8352c, kBatten = 0xff8a5a3c, kFelt = 0xff3a2a22;
    constexpr juce::uint32 kInterior = 0xff2a2421, kRafter = 0xff6b4a35;
    constexpr juce::uint32 kGrass = 0xff86c25b, kGrassDry = 0xffa4ad62, kGrassSide = 0xff5f9a3f;
    constexpr juce::uint32 kTopsoil = 0xff4f3524, kSubsoil = 0xff7a5a40;
    constexpr juce::uint32 kFrame = 0xfffbfbf8, kWood = 0xffd49c5c, kBrickRed = 0xffbf5f45, kStone = 0xffd6d0c5;
    constexpr juce::uint32 kShadowInk = 0xff16202e;

    //--------------------------------------------------------------------------
    inline juce::Path quad (P2 a, P2 b, P2 c, P2 d)
    {
        juce::Path p;
        p.startNewSubPath (a); p.lineTo (b); p.lineTo (c); p.lineTo (d); p.closeSubPath();
        return p;
    }

    inline juce::Path polygon (const P2* pts, int n)
    {
        juce::Path p;
        for (int k = 0; k < n; ++k)
            if (k == 0) p.startNewSubPath (pts[k]); else p.lineTo (pts[k]);
        p.closeSubPath();
        return p;
    }

    inline juce::Path convexHull (std::vector<P2> pts)
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

    inline juce::Colour mix (juce::Colour a, juce::Colour b, float t) noexcept { return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t)); }
} // namespace house
} // namespace rb::ui
