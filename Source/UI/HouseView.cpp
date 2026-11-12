#include "HouseModel.h"
#include "../Parameters.h"

// Damage generation, debris, camera and mouse. Drawing is in HousePaint.cpp.

namespace rb::ui
{
using namespace house;

namespace
{
    struct Seed { P2 p; float dir; };

    //--------------------------------------------------------------------------
    // Procedural damage, generated once per surface (same seed → same house every time).

    /** A crack wanders about a mean direction, kinks now and then, and forks. */
    void growCrack (juce::Random& r, std::vector<HouseView::CrackLine>& out, P2 p, float mean, float th, float length,
                    float width, int depth, float uMax, float vMax)
    {
        struct Fork { P2 p; float dir, th; };
        std::vector<Fork> forks;

        HouseView::CrackLine line;
        line.width = width;
        line.pts.push_back (p);
        line.th.push_back (th);

        const float thPerMetre = 0.2f + 0.2f * r.nextFloat();   // how much more damage each metre of crack takes
        float wander = 0.0f, travelled = 0.0f;
        while (travelled < length && line.pts.size() < 40)
        {
            const float step = 0.05f + 0.06f * r.nextFloat();
            wander = wander * 0.55f + (r.nextFloat() - 0.5f) * 0.7f;
            if (r.nextFloat() < 0.16f)
                wander += (r.nextBool() ? 1.0f : -1.0f) * (0.5f + 0.5f * r.nextFloat());   // a kink
            mean += (r.nextFloat() - 0.5f) * 0.12f;
            const float dir = mean + wander;
            const P2 q = p + P2 (std::cos (dir), std::sin (dir)) * step;
            if (q.x < -0.02f || q.y < -0.02f || q.x > uMax + 0.02f || q.y > vMax + 0.02f)
                break;
            travelled += step;
            th += thPerMetre * step;
            p = q;
            line.pts.push_back (p);
            line.th.push_back (th);
            if (depth < 2 && r.nextFloat() < 0.15f)
                forks.push_back ({ p, dir + (r.nextBool() ? 1.0f : -1.0f) * (0.5f + 0.6f * r.nextFloat()), th + 0.03f });
        }
        if (line.pts.size() >= 2)
            out.push_back (std::move (line));
        for (const auto& f : forks)
            growCrack (r, out, f.p, f.dir, f.th, length * (0.3f + 0.3f * r.nextFloat()), width * 0.62f, depth + 1, uMax, vMax);
    }

    /** An angular patch: uneven facets, like something that broke rather than something drawn. */
    HouseView::Blob makeBlob (juce::Random& r, P2 c, float radius, float th, int sides, float rough)
    {
        HouseView::Blob b;
        b.c = c;
        b.r = radius;
        b.th = th;
        for (int k = 0; k < sides; ++k)
        {
            const float a = kTwoPi * ((float) k + (r.nextFloat() - 0.5f) * 0.7f) / (float) sides;
            const float rad = 1.0f - rough * r.nextFloat();
            b.shape.push_back ({ std::cos (a) * rad, std::sin (a) * rad });
        }
        return b;
    }

    HouseView::Surface makeSurface (juce::Random& r, std::vector<Seed> seeds, float uMax, float vMax, int spalls, int holes,
                                    float crackWidth, int forkDepth)
    {
        HouseView::Surface s;
        for (int k = 0; k < 3; ++k)
            seeds.push_back ({ { uMax * (0.15f + 0.7f * r.nextFloat()), vMax * (0.1f + 0.7f * r.nextFloat()) }, kTwoPi * r.nextFloat() });

        // Stagger the start thresholds so the first cracks come early and the rest follow.
        std::vector<float> starts;
        for (size_t k = 0; k < seeds.size(); ++k) starts.push_back (0.03f + 0.5f * r.nextFloat());
        std::sort (starts.begin(), starts.end());
        for (size_t k = 0; k < seeds.size(); ++k)
            std::swap (seeds[k], seeds[(size_t) r.nextInt ((int) seeds.size())]);
        for (size_t k = 0; k < seeds.size(); ++k)
            growCrack (r, s.cracks, seeds[k].p, seeds[k].dir, starts[k], 0.6f + 1.0f * r.nextFloat(),
                       crackWidth * (0.75f + 0.5f * r.nextFloat()), forkDepth, uMax, vMax);

        // The finish comes away along the cracks.
        for (int k = 0; k < spalls && ! s.cracks.empty(); ++k)
        {
            const auto& c = s.cracks[(size_t) r.nextInt ((int) s.cracks.size())];
            const size_t i = (size_t) r.nextInt ((int) c.pts.size());
            const float th = juce::jlimit (0.2f, 0.82f, std::max (c.th[i] + 0.06f, 0.2f) + 0.2f * r.nextFloat());
            s.spalls.push_back (makeBlob (r, c.pts[i], 0.14f + 0.2f * r.nextFloat(), th, 9 + r.nextInt (4), 0.42f));
        }
        for (int k = 0; k < holes; ++k)
        {
            const P2 c = ! s.spalls.empty() && r.nextFloat() < 0.7f ? s.spalls[(size_t) r.nextInt ((int) s.spalls.size())].c
                                                                   : P2 (uMax * r.nextFloat(), vMax * (0.15f + 0.6f * r.nextFloat()));
            s.holes.push_back (makeBlob (r, c, 0.26f + 0.24f * r.nextFloat(), 0.5f + 0.3f * r.nextFloat(), 8 + r.nextInt (4), 0.38f));
        }
        for (auto& v : s.ragged) v = r.nextFloat();
        return s;
    }

    std::array<P2, 5> bitShape (juce::Random& r, int kind, int& verts)
    {
        std::array<P2, 5> s {};
        switch (kind)
        {
            case Tile:     verts = 4; s = { P2 (-0.5f, -0.36f), P2 (0.5f, -0.36f), P2 (0.5f, 0.36f), P2 (-0.5f + 0.3f * r.nextFloat(), 0.36f), P2() }; break;
            case Shard:    verts = 3; s = { P2 (-0.25f - 0.2f * r.nextFloat(), -0.3f), P2 (0.3f, -0.2f * r.nextFloat()), P2 (-0.1f, 0.7f), P2(), P2() }; break;
            case Splinter: verts = 4; s = { P2 (-0.8f, -0.07f), P2 (0.7f, -0.1f), P2 (0.85f, 0.04f), P2 (-0.7f, 0.1f), P2() }; break;
            case Scrap:    verts = 4; s = { P2 (-0.5f, -0.4f), P2 (0.55f, -0.25f - 0.2f * r.nextFloat()), P2 (0.4f, 0.45f), P2 (-0.45f - 0.15f * r.nextFloat(), 0.3f), P2() }; break;
            case Chunk: case Clod: default:
                verts = 5;
                for (int k = 0; k < 5; ++k)
                {
                    const float a = kTwoPi * ((float) k + (r.nextFloat() - 0.5f) * 0.6f) / 5.0f;
                    const float rad = (kind == Clod ? 0.5f : 0.42f) + 0.2f * r.nextFloat();
                    s[(size_t) k] = { std::cos (a) * rad, std::sin (a) * rad };
                }
                break;
        }
        return s;
    }

    void addRect (juce::Path& p, juce::Rectangle<float> r) { if (r.getWidth() > 0.0f && r.getHeight() > 0.0f) p.addRectangle (r); }
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
        constexpr float q = kPi * 0.25f;
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
        if (def.gable)
            seeds.push_back ({ { 0.5f * def.uLen, kH + kR - 0.08f }, -kPi * 0.5f + (r.nextFloat() - 0.5f) * 0.6f });
        walls[(size_t) w] = makeSurface (r, seeds, def.uLen, def.height(), 12, 4, 0.034f, 0);
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
                float nearest = 9.0f;
                for (int k = 0; k < 3; ++k) nearest = std::min (nearest, c.getDistanceFrom (spots[k]) / radius[k]);
                th[(size_t) (i * cols + j)] = 0.14f + 0.62f * juce::jlimit (0.0f, 1.0f, nearest / 1.6f) + 0.16f * r.nextFloat();
            }
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
        ground = makeSurface (r, seeds, 2.0f * kSlabX, 2.0f * kSlabY, 0, 0, 0.12f, 2);   // no forks: fissures, not twigs
    }

    // Rubble that builds up at the foot of each wall, and broken tiles under the eaves.
    {
        juce::Random r (9001);
        int verts = 0;
        for (int w = 0; w < 4; ++w)
        {
            const auto& def = kWalls[w];
            constexpr int n = 58;
            for (int k = 0; k < n; ++k)
            {
                HeapPiece h;
                const float t = (float) k / (float) (n - 1);
                h.wall = w;
                h.th = 0.28f + 0.7f * t;
                const float u = def.uLen * (0.03f + 0.94f * r.nextFloat());
                const float out = 0.06f + 0.55f * r.nextFloat() * r.nextFloat();
                h.size = (0.05f + 0.1f * r.nextFloat()) * (0.7f + 1.0f * t);   // the big pieces come down last
                h.height = h.size * (0.4f + 0.5f * r.nextFloat());
                const V3 local = def.o + def.u * u + def.n * out;
                h.p = { local.x, local.y + kHouseY, out < 0.22f ? 0.1f * r.nextFloat() * t : 0.0f };   // piled against the wall
                h.tone = r.nextInt (3);
                h.shape = bitShape (r, Chunk, verts);
                heap.push_back (h);
            }
        }
        for (int k = 0; k < 36; ++k)
        {
            HeapPiece h;
            const float side = k % 2 == 0 ? 1.0f : -1.0f;
            h.wall = dsp::Ceiling;
            h.th = 0.2f + 0.75f * (float) k / 35.0f;
            h.size = 0.09f + 0.06f * r.nextFloat();
            h.height = 0.025f;
            h.p = { side * (0.5f * kW + kEave + 0.02f + 0.5f * r.nextFloat() * r.nextFloat()), kHouseY + (r.nextFloat() - 0.5f) * (kD + 0.2f), 0.0f };
            h.tone = r.nextInt (3);
            h.shape = bitShape (r, Tile, verts);
            h.shape[4] = h.shape[3];
            heap.push_back (h);
        }
    }

    // Lawn detail: tufts of longer grass and a few daisies, kept off the house and the path.
    {
        juce::Random r (606);
        const float doorX = -0.5f * kW + 0.925f;
        auto clear = [&] (float x, float y)
        {
            const bool underHouse = std::abs (x) < 0.5f * kW + 0.12f && std::abs (y - kHouseY) < 0.5f * kD + 0.12f;
            const bool onPath = std::abs (x - doorX) < 0.34f && y < kHouseY - 0.5f * kD;
            return ! underHouse && ! onPath;
        };
        while (tufts.size() < 150)
        {
            const float x = (r.nextFloat() - 0.5f) * 2.0f * (kSlabX - 0.06f), y = (r.nextFloat() - 0.5f) * 2.0f * (kSlabY - 0.06f);
            if (clear (x, y))
                tufts.push_back ({ x, y, 0.04f + 0.06f * r.nextFloat(), (r.nextFloat() - 0.5f) * 0.06f });
        }
        for (int k = 0; k < 34;)
        {
            const float x = (r.nextFloat() - 0.5f) * 2.0f * (kSlabX - 0.1f), y = (r.nextFloat() - 0.5f) * 2.0f * (kSlabY - 0.1f);
            if (! clear (x, y)) continue;
            (k % 3 == 0 ? flowersB : flowersA).addEllipse (x + kSlabX - 0.022f, y + kSlabY - 0.022f, 0.044f, 0.044f);
            ++k;
        }
    }
}

//==============================================================================
// Per-material drawing data
//==============================================================================
bool HouseView::cellBounds (const Skin& s, int row, int col, float uLen, float vLen, juce::Rectangle<float>& out) const noexcept
{
    const float u0 = (float) col * s.cw - (s.bond && row % 2 == 1 ? 0.5f * s.cw : 0.0f);
    out = juce::Rectangle<float> (u0, (float) row * s.ch, s.cw, s.ch).getIntersection ({ 0.0f, 0.0f, uLen, vLen });
    return out.getWidth() > 0.001f && out.getHeight() > 0.001f;
}

void HouseView::rebuildSkins (int m)
{
    skinMaterial = m;
    const auto& f = kFinishes[m];
    const bool cells = f.pattern == Tiles || f.pattern == Blocks || f.pattern == Glass || f.pattern == Plates || f.pattern == Planks;

    for (int w = 0; w < 4; ++w)
    {
        const auto& def = kWalls[w];
        const auto& surf = walls[(size_t) w];
        const float uLen = def.uLen, vLen = def.height();
        auto& s = skins[(size_t) w];
        s = {};
        juce::Random r (500 + 31 * w + 7 * m);

        if (cells)
        {
            s.cw = f.a;
            s.ch = f.b;
            s.bond = bonded (f.pattern);
            s.cols = (int) std::ceil (uLen / s.cw) + 1;
            s.rows = (int) std::ceil (vLen / s.ch);
            s.spall.assign ((size_t) (s.rows * s.cols), 9.0f);
            s.hole.assign ((size_t) (s.rows * s.cols), 9.0f);

            for (int row = 0; row < s.rows; ++row)
            {
                const float v = (float) row * s.ch;
                if (row > 0) { s.joints.startNewSubPath (0.0f, v); s.joints.lineTo (uLen, v); }
                for (int col = 0; col < s.cols; ++col)
                {
                    juce::Rectangle<float> cell;
                    if (! cellBounds (s, row, col, uLen, vLen, cell))
                        continue;
                    if (cell.getX() > 0.001f) { s.joints.startNewSubPath (cell.getX(), cell.getY()); s.joints.lineTo (cell.getX(), cell.getBottom()); }

                    // odd ones out: a little darker or lighter than their neighbours
                    const float tone = hash (w, row, col);
                    if (tone < 0.16f)      s.toneDark.addRectangle (cell);
                    else if (tone > 0.86f) s.toneLight.addRectangle (cell);

                    // each cell goes when the nearest weak spot has grown out to it
                    const P2 c = cell.getCentre();
                    float sp = 9.0f, ho = 9.0f;
                    for (const auto& b : surf.spalls)
                        sp = std::min (sp, b.th + 0.42f * std::max (0.0f, c.getDistanceFrom (b.c) / (0.2f + 1.7f * b.r) - 0.3f));
                    for (const auto& b : surf.holes)
                        ho = std::min (ho, b.th + 0.4f * std::max (0.0f, c.getDistanceFrom (b.c) / (0.15f + 1.4f * b.r) - 0.35f));
                    sp += (hash (w, row, col + 501) - 0.5f) * 0.08f;
                    ho += (hash (w, row + 77, col) - 0.5f) * 0.06f;
                    for (const auto& o : def.openings)
                        if (cell.intersects (juce::Rectangle<float> (o.u0, o.v0, o.u1 - o.u0, o.v1 - o.v0).expanded (0.06f)))
                            sp = 9.0f;   // windows and doors keep their own damage
                    if (f.spall == SpallCells && f.hole == HoleCells)
                        ho = juce::jlimit (std::min (sp, 0.85f) + 0.1f, std::min (sp, 0.85f) + 0.2f, ho);   // cracks, then drops out
                    const size_t i = (size_t) (row * s.cols + col);
                    s.spall[i] = sp;
                    s.hole[i] = ho;
                }
            }
        }

        // surface detail
        if (f.pattern == Plates)
        {
            for (float u = s.cw; u < uLen; u += s.cw)
                for (float v = 0.06f; v < vLen; v += 0.14f)
                    s.detail.addRectangle (u + 0.03f, v, 0.026f, 0.026f);
            for (float v = s.ch; v < vLen; v += s.ch)
                for (float u = 0.06f; u < uLen; u += 0.14f)
                    s.detail.addRectangle (u, v - 0.056f, 0.026f, 0.026f);
        }
        else if (f.pattern == Planks)
        {
            for (int k = 0; k < 9; ++k)
            {
                const float u = uLen * r.nextFloat(), v = vLen * r.nextFloat();
                s.detail.addEllipse (u - 0.03f, v - 0.016f, 0.06f, 0.032f);
            }
        }
        else if (f.pattern == Ribs)
        {
            for (float v = 0.5f; v < vLen; v += 1.05f)
                for (float u = f.a; u < uLen; u += f.a * 3.0f)
                    s.detail.addRectangle (u - 0.012f, v - 0.012f, 0.024f, 0.024f);
        }
        if (f.pattern == Plain || f.pattern == Blocks || f.pattern == Tiles)
        {
            for (int k = 0; k < 170; ++k)
            {
                const float sz = 0.01f + 0.014f * r.nextFloat();
                (k % 3 == 0 ? s.speckLight : s.speckDark).addRectangle (uLen * r.nextFloat(), vLen * r.nextFloat(), sz, sz * (0.6f + 0.6f * r.nextFloat()));
            }
        }

        // what is underneath
        switch (f.underKind)
        {
            case Brick:
                for (int row = 0; (float) row * 0.075f < vLen; ++row)
                    for (int col = 0; (float) col * 0.22f - 0.11f < uLen; ++col)
                    {
                        const float u0 = (float) col * 0.22f - (row % 2 == 1 ? 0.11f : 0.0f) + 0.006f;
                        const auto brick = juce::Rectangle<float> (u0, (float) row * 0.075f + 0.006f, 0.208f, 0.063f);
                        const float t = hash (w + 40, row, col);
                        addRect (t < 0.6f ? s.underA : (t < 0.85f ? s.underB : s.underC), brick.getIntersection ({ 0.0f, 0.0f, uLen, vLen }));
                    }
                break;
            case Rubble:
                for (float v = 0.0f; v < vLen;)
                {
                    const float h = 0.13f + 0.09f * r.nextFloat();
                    for (float u = -0.2f * r.nextFloat(); u < uLen;)
                    {
                        const float len = 0.18f + 0.26f * r.nextFloat();
                        const float t = r.nextFloat();
                        addRect (t < 0.5f ? s.underA : (t < 0.8f ? s.underB : s.underC),
                                 juce::Rectangle<float> (u + 0.008f, v + 0.008f, len - 0.016f, h - 0.016f).getIntersection ({ 0.0f, 0.0f, uLen, vLen }));
                        u += len;
                    }
                    v += h;
                }
                break;
            case Concrete:
                for (int k = 0; k < 320; ++k)
                {
                    const float sz = 0.014f + 0.024f * r.nextFloat();
                    (k % 3 == 0 ? s.underB : s.underA).addRectangle (uLen * r.nextFloat(), vLen * r.nextFloat(), sz, sz * (0.6f + 0.5f * r.nextFloat()));
                }
                break;
            case Studs:
                for (float u = 0.0f; u < uLen; u += 0.4f)
                    s.underA.addRectangle (u, 0.0f, 0.055f, vLen);
                s.underA.addRectangle (uLen - 0.055f, 0.0f, 0.055f, vLen);
                for (float u = 0.0f; u < uLen; u += 0.4f)
                    s.underB.addRectangle (u + 0.055f, 0.98f + (((int) (u / 0.4f)) % 2 == 0 ? 0.0f : 0.08f), 0.345f, 0.05f);
                break;
            case Nothing: default: break;
        }
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

juce::Point<float> HouseView::lightOnScreen() const noexcept
{
    const float ca = std::cos (yaw), sa = std::sin (yaw);
    const float right = kLight.x * ca + kLight.y * sa;
    const float depth = -kLight.x * sa + kLight.y * ca;
    const float up = kLight.z * std::cos (kElev) + depth * std::sin (kElev);
    const float len = std::max (0.001f, std::hypot (right, up));
    return { right / len, -up / len };
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

juce::AffineTransform HouseView::roofTransform (bool east, float& uLen, float& slopeLen) const noexcept
{
    // u runs along the ridge (south to north), v runs up the slope from the eave.
    const float sg = east ? 1.0f : -1.0f;
    const float xe = 0.5f * kW + kEave, y0 = -0.5f * kD - kGable;
    const float ze = kH + kR - kSlope * xe;
    uLen = kD + 2.0f * kGable;
    slopeLen = xe / std::cos (kPhi);
    return planeTransform ({ sg * xe, y0, ze + kTz }, { 0, 1, 0 }, { -sg * std::cos (kPhi), 0, std::sin (kPhi) }, true);
}

bool HouseView::inFront (V3 p) const noexcept
{
    // Behind the house = beyond the plane of any wall that faces away from the camera.
    const V3 cam = toCamera();
    for (const auto& def : kWalls)
    {
        const V3 n = houseDir (def.n);
        if (dot (n, cam) < 0.0f && dot (p - houseToWorld (def.o), n) > 0.0f)
            return false;
    }
    return true;
}

juce::Point<float> HouseView::wallAnchor (int wall) const noexcept
{
    if (wall < 4)
    {
        const auto& def = kWalls[wall];
        return toScreen (houseToWorld (def.o + def.u * (0.5f * def.uLen) + V3 { 0, 0, 1.05f }));
    }
    if (wall == dsp::Ceiling)
    {
        const float x = std::sin (yaw) > 0.0f ? 0.7f : -0.7f;   // on the slope that faces the viewer
        return toScreen (houseToWorld ({ x, -0.1f, roofTopZ (x) }));
    }
    // the lawn in front of the house, towards the viewer
    return toScreen ({ juce::jlimit (-1.9f, 1.9f, 2.0f * std::sin (yaw)), juce::jlimit (-1.8f, 1.8f, kHouseY - 2.0f * std::cos (yaw)), 0.0f });
}

bool HouseView::wallFacesViewer (int wall) const noexcept
{
    return wall >= 4 || dot (houseDir (kWalls[wall].n), toCamera()) > 0.0f;
}

juce::Range<float> HouseView::plotSpan() const noexcept
{
    float lo = 1.0e9f, hi = -1.0e9f;
    for (float x : { -kSlabX, kSlabX })
        for (float y : { -kSlabY, kSlabY })
        {
            const float sx = toScreen ({ x, y, 0.0f }).x - shake.x;
            lo = std::min (lo, sx);
            hi = std::max (hi, sx);
        }
    return { lo, hi };
}

juce::Point<float> HouseView::northOnScreen() const noexcept
{
    const P2 north = toScreen ({ 0, 1, 0 }) - toScreen ({ 0, 0, 0 });
    return north / std::max (0.001f, std::hypot (north.x, north.y));
}

float HouseView::damageOf (int wall) const noexcept
{
    return 1.0f - juce::jlimit (0.0f, 1.0f, live.frame.integrity[(size_t) wall]);
}

float HouseView::meanDamage() const noexcept
{
    return 1.0f - juce::jlimit (0.0f, 1.0f, live.meanIntegrity());
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
    include (toScreen ({ kChimX, kChimY + kHouseY, kChimTop + 0.16f }));
    const juce::Rectangle<float> box (x0, y0, x1 - x0, y1 - y0);
    yaw = savedYaw;

    const auto area = getLocalBounds().toFloat().reduced (14.0f, 20.0f);
    scale = std::min (area.getWidth() / box.getWidth(), area.getHeight() / box.getHeight());
    centre = area.getCentre() - box.getCentre() * scale + P2 (0.0f, 4.0f);
    slabShadow = {};
    backdropCache = {};
    groundCache = {};
    interiorCache = {};
}

//==============================================================================
// Where the house is giving way (so debris comes from the damage you can see)
//==============================================================================
float HouseView::collapseV (int wall, float u, float d) const noexcept
{
    // Past ~82 % damage the wall comes down from the top: a ragged line that sinks in the middle
    // and leaves the corners standing.
    const auto& def = kWalls[wall];
    const float vLen = def.height();
    const float c = smooth (0.82f, 1.0f, d);
    if (c <= 0.0f) return vLen + 1.0f;
    const float t = juce::jlimit (0.0f, 1.0f, u / def.uLen);
    const auto& rag = walls[(size_t) wall].ragged;
    // Nine slabs across the wall, each broken off at its own height: flat tops and steep sides,
    // the way masonry shears, with a little roughness along the break.
    const float pos = t * 9.0f;
    const size_t i = (size_t) std::min ((int) pos, 8);
    const float noise = rag[i] + (rag[i + 1] - rag[i]) * smooth (0.32f, 0.68f, pos - (float) i);
    const float rough = rag[10 + (size_t) ((int) (t * 44.0f) % 15)] - 0.5f;
    const float bump = std::pow (std::sin (kPi * t), 0.55f);
    const float depth = c * (vLen - 0.5f) * bump * (0.66f + 0.42f * noise) + c * 0.07f * rough;
    return vLen + 0.04f - depth;
}

bool HouseView::cellIsHole (int wall, int row, int col, float d) const noexcept
{
    const auto& s = skins[(size_t) wall];
    const auto& def = kWalls[wall];
    juce::Rectangle<float> cell;
    if (! cellBounds (s, row, col, def.uLen, def.height(), cell))
        return false;
    if (s.hole[(size_t) (row * s.cols + col)] < d)
        return true;
    return cell.getCentreY() > collapseV (wall, cell.getCentreX(), d);
}

V3 HouseView::breakPoint (int wall, V3& outward)
{
    if (wall < 4)
    {
        const auto& def = kWalls[wall];
        const auto& s = walls[(size_t) wall];
        const float d = damageOf (wall);
        P2 at (0.1f + (def.uLen - 0.2f) * rng.nextFloat(), 0.25f + (kH - 0.35f) * rng.nextFloat());

        // Prefer the weak spot whose threshold the damage has just reached.
        float best = 0.0f;
        auto consider = [&] (P2 c, float th, float radius)
        {
            if (th > d + 0.06f) return;
            const float score = rng.nextFloat() / (0.04f + std::abs (d - th));
            if (score > best)
            {
                best = score;
                const float a = kTwoPi * rng.nextFloat();
                at = c + P2 (std::cos (a), std::sin (a)) * (radius * rng.nextFloat());
            }
        };
        for (const auto& b : s.spalls) consider (b.c, b.th, b.r * 1.5f);
        for (const auto& b : s.holes)  consider (b.c, b.th, b.r);
        for (const auto& c : s.cracks)
        {
            size_t tip = 0;
            while (tip + 1 < c.pts.size() && c.th[tip + 1] < d) ++tip;
            consider (c.pts[tip], c.th[tip], 0.05f);
        }
        if (d > 0.84f && rng.nextFloat() < 0.6f)
        {
            at.x = def.uLen * (0.12f + 0.76f * rng.nextFloat());
            at.y = std::min (collapseV (wall, at.x, d), def.height());
        }
        at.x = juce::jlimit (0.03f, def.uLen - 0.03f, at.x);
        at.y = juce::jlimit (0.05f, def.height() - 0.05f, at.y);
        outward = houseDir (def.n);
        return houseToWorld (def.o + def.u * at.x + V3 { 0, 0, at.y } + def.n * 0.05f);
    }
    if (wall == dsp::Ceiling)
    {
        // A tile that is about to go, on one side or the other.
        const bool east = rng.nextBool();
        const float sg = east ? 1.0f : -1.0f;
        const float d = damageOf (dsp::Ceiling);
        const float xe = 0.5f * kW + kEave, slopeLen = xe / std::cos (kPhi), uLen = kD + 2.0f * kGable;
        const int rows = (int) std::ceil (slopeLen / kTileV), cols = (int) std::ceil (uLen / kTileU) + 1;
        const auto& th = tileThreshold[east ? 0 : 1];
        int bi = rng.nextInt (rows), bj = rng.nextInt (cols);
        float best = 9.0f;
        for (int k = 0; k < 14; ++k)
        {
            const int i = rng.nextInt (rows), j = rng.nextInt (cols);
            const float t = th[(size_t) (i * cols + j)];
            const float miss = t <= d + 0.05f ? std::abs (d - t) : 5.0f;
            if (miss < best) { best = miss; bi = i; bj = j; }
        }
        const float u = juce::jlimit (0.05f, uLen - 0.05f, (float) bj * kTileU), v = juce::jlimit (0.05f, slopeLen - 0.05f, ((float) bi + 0.5f) * kTileV);
        const V3 local = V3 { sg * xe, -0.5f * kD - kGable, kH + kR - kSlope * xe + kTz + 0.03f }
                         + V3 { 0, 1, 0 } * u + V3 { -sg * std::cos (kPhi), 0, std::sin (kPhi) } * v;
        outward = houseDir ({ sg * 0.75f, 0.0f, 0.5f });
        return houseToWorld (local);
    }

    // Clods jump out of the opening lawn.
    outward = { 0, 0, 1 };
    if (ground.cracks.empty()) return { 0, -1.0f, 0.02f };
    const auto& c = ground.cracks[(size_t) rng.nextInt ((int) ground.cracks.size())];
    const P2 q = c.pts[(size_t) rng.nextInt ((int) c.pts.size())];
    return { q.x - kSlabX, q.y - kSlabY, 0.02f };
}

//==============================================================================
// Debris
//==============================================================================
juce::Colour HouseView::debrisColour (int wall) const
{
    const auto& f = kFinishes[material];
    if (wall == dsp::Ceiling) return juce::Colour (rng.nextFloat() < 0.78f ? kRoofRed : kBatten);
    if (wall == dsp::Floor)   return juce::Colour (rng.nextFloat() < 0.65f ? kTopsoil : kGrassSide);
    if (damageOf (wall) > 0.3f && f.underKind != Nothing && rng.nextFloat() < 0.4f)
        return juce::Colour (f.underKind == Studs ? 0xff6a4c38 : f.under);
    return juce::Colour (f.debris);
}

void HouseView::spawnDust (V3 at, V3 drift, int count, float spread, bool low)
{
    const auto& f = kFinishes[material];
    auto colour = mix (juce::Colour (f.debris), juce::Colour (0xffe6dfd2), 0.55f);
    if (light.dark) colour = colour.withMultipliedBrightness (0.55f);

    int dustCount = 0;
    for (const auto& p : particles) dustCount += p.dust ? 1 : 0;
    for (int k = 0; k < count && dustCount < 110; ++k, ++dustCount)
    {
        Particle d;
        d.dust = true;
        d.low = low;
        d.p = at + V3 { (rng.nextFloat() - 0.5f) * spread, (rng.nextFloat() - 0.5f) * spread, (rng.nextFloat() - 0.3f) * spread * (low ? 0.2f : 1.0f) };
        d.v = drift * (0.6f + 0.8f * rng.nextFloat())
              + V3 { (rng.nextFloat() - 0.5f) * 0.3f, (rng.nextFloat() - 0.5f) * 0.3f, low ? 0.05f * rng.nextFloat() : 0.15f + 0.3f * rng.nextFloat() };
        d.size = 0.12f + 0.14f * rng.nextFloat();
        d.life = (low ? 1.3f : 0.9f) + 0.9f * rng.nextFloat();
        d.colour = colour;
        particles.push_back (d);
    }
}

void HouseView::spawnFracture (int wall, float m)
{
    const auto& f = kFinishes[material];
    const float mm = std::min (m, 1.2f);
    const int count = 8 + (int) (34.0f * mm);
    V3 lastOut;
    for (int k = 0; k < count; ++k)
    {
        V3 out;
        const V3 at = breakPoint (wall, out);
        lastOut = out;

        Particle p;
        p.kind = wall < 4 ? (int) f.bit : (wall == dsp::Ceiling ? (rng.nextFloat() < 0.8f ? (int) Tile : (int) Splinter) : (int) Clod);
        p.shape = bitShape (rng, p.kind, p.verts);
        p.size = 0.03f + 0.11f * std::pow (rng.nextFloat(), 2.4f) * (0.6f + 0.6f * std::min (m, 1.0f));
        if (p.kind == Tile) p.size = 0.07f + 0.05f * rng.nextFloat();
        if (p.kind == Splinter) p.size *= 1.5f;
        p.spin = (rng.nextFloat() - 0.5f) * 12.0f;
        p.tumbleRate = (rng.nextFloat() - 0.5f) * 16.0f;
        p.angle = rng.nextFloat() * kTwoPi;
        p.tumble = rng.nextFloat() * kTwoPi;
        p.colour = debrisColour (wall);
        p.p = at;

        // Thrown hard enough to read as a hit, gently enough that most of it lands on the lawn
        // (there is less grass beside the house than in front of it).
        const float push = (0.6f + 0.6f * mm) * (wall == dsp::East || wall == dsp::West || wall == dsp::Ceiling ? 0.55f : 1.0f);
        if (wall < 4)
        {
            const V3 t = houseDir (kWalls[wall].u);
            p.v = out * ((0.3f + 1.5f * rng.nextFloat()) * push) + V3 { 0, 0, 0.2f + 1.7f * rng.nextFloat() } + t * ((rng.nextFloat() - 0.5f) * 0.9f);
        }
        else if (wall == dsp::Ceiling)
            p.v = out * ((0.7f + 1.5f * rng.nextFloat()) * push) + V3 { 0, (rng.nextFloat() - 0.5f) * 0.8f, 0.5f * rng.nextFloat() };
        else
        {
            p.v = { (rng.nextFloat() - 0.5f) * 0.9f, (rng.nextFloat() - 0.5f) * 0.9f, 0.7f + 1.5f * rng.nextFloat() * mm };
            p.size *= 0.8f;
        }
        particles.push_back (p);

        if (k % 9 == 0)
            spawnDust (at, out * 0.5f, 2 + (int) (3.0f * mm), 0.3f, false);
    }

    // A hard hit rolls dust out along the ground.
    if (m > 0.7f && wall < 4)
    {
        const auto& def = kWalls[wall];
        for (int k = 0; k < 5; ++k)
            spawnDust (houseToWorld (def.o + def.u * (def.uLen * (0.1f + 0.2f * (float) k)) + def.n * 0.15f + V3 { 0, 0, 0.08f }),
                       lastOut * 0.9f, 1, 0.25f, true);
    }

    // keep the rubble count bounded: drop the oldest settled pieces first
    int solid = 0;
    for (const auto& p : particles) solid += p.dust ? 0 : 1;
    for (auto it = particles.begin(); solid > 520 && it != particles.end();)
    {
        if (! it->dust && it->settled) { it = particles.erase (it); --solid; }
        else ++it;
    }
}

void HouseView::tick (float dt)
{
    material = juce::jlimit (0, kNumFinishes - 1, (int) materialParam->load());
    outside = juce::jlimit (0, kNumBackdrops - 1, (int) voidParam->load());

    // camera
    if (returning)
    {
        float diff = std::remainder (kDefaultYaw - yaw, kTwoPi);
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
    blastAge += dt;

    // new fractures → debris (not for ones that happened before the window opened)
    warmUp = std::max (0.0f, warmUp - dt);
    float hitTotal = 0.0f;
    for (int w = 0; w < dsp::kNumWalls; ++w)
        if (const float m = live.pendingFracture[(size_t) w]; m > 0.02f)
        {
            if (! frozen && warmUp <= 0.0f && live.frame.integrity[(size_t) w] < 0.995f)
            {
                spawnFracture (w, m);
                hitTotal += std::min (m, 1.0f);
            }
            live.pendingFracture[(size_t) w] = 0.0f;
        }
    if (hitTotal >= 1.6f && blastAge > 0.5f)
        blastAge = 0.0f;   // several surfaces hit hard at once: a detonation

    if (! frozen)
    {
        // Loose walls keep shedding grit while there is rubble energy; overloaded walls smoke a little.
        const float trickle = live.frame.rubble * 3.0f * dt;
        for (int w = 0; w < 4; ++w)
        {
            const float d = damageOf (w);
            const auto& def = kWalls[w];
            if (d > 0.3f && rng.nextFloat() < trickle * d)
            {
                V3 out;
                Particle p;
                p.p = breakPoint (w, out);
                p.v = out * (0.1f + 0.3f * rng.nextFloat());
                p.size = 0.02f + 0.025f * rng.nextFloat();
                p.kind = (int) kFinishes[material].bit;
                p.shape = bitShape (rng, p.kind, p.verts);
                p.tumbleRate = (rng.nextFloat() - 0.5f) * 10.0f;
                p.colour = debrisColour (w);
                particles.push_back (p);
            }
            if (live.frame.stressRatio[(size_t) w] > 1.0f && rng.nextFloat() < (live.frame.stressRatio[(size_t) w] - 1.0f) * 4.0f * dt)
                spawnDust (houseToWorld (def.o + def.u * (def.uLen * rng.nextFloat()) + V3 { 0, 0, kH * rng.nextFloat() } + def.n * 0.1f),
                           houseDir (def.n) * 0.2f, 1, 0.2f, false);
        }

        // physics
        for (auto& p : particles)
        {
            p.age += dt;
            if (p.dust)
            {
                p.p = p.p + p.v * dt;
                p.v = p.v * (1.0f - std::min (0.9f, 1.6f * dt));
                p.size += (p.low ? 0.5f : 0.34f) * dt * (1.0f - 0.6f * p.age / p.life);
                continue;
            }
            if (p.fading) p.alpha -= 1.6f * dt;
            if (p.settled) continue;

            p.v.z -= 9.8f * dt;
            p.p = p.p + p.v * dt;
            p.angle += p.spin * dt;
            p.tumble += p.tumbleRate * dt;

            const bool onLawn = std::abs (p.p.x) < kSlabX && std::abs (p.p.y) < kSlabY;
            const float floorZ = onLawn ? 0.0f : -8.5f;
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
                p.tumbleRate *= 0.5f;
                if (std::abs (p.v.z) < 0.35f && std::hypot (p.v.x, p.v.y) < 0.35f)
                {
                    p.settled = true;
                    p.tumble = 0.0f;
                }
            }
            if (p.p.z < -kSlabT) p.alpha -= 2.2f * dt;   // went over the edge of the lawn: gone before it gets far
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

    // Repaint only when the picture would differ: an untouched room costs nothing to show.
    bool moving = blastAge < 0.6f;
    for (const auto& p : particles)
        if (p.dust || p.fading || ! p.settled) { moving = true; break; }
    const auto state = stateSignature();
    if (moving || state != paintedState)
    {
        paintedState = state;
        repaint();
    }
}

juce::uint64 HouseView::stateSignature() const noexcept
{
    juce::uint64 h = 1469598103934665603ull;
    auto add = [&h] (juce::int64 v) { h = (h ^ (juce::uint64) v) * 1099511628211ull; };
    add (juce::roundToInt (yaw * 4000.0f));
    add (juce::roundToInt (shake.x * 8.0f));
    add (juce::roundToInt (shake.y * 8.0f));
    add (material);
    add (outside);
    add (hover);
    add (live.frame.frozen ? 1 : 0);
    add (isMouseOver() ? 1 : 0);
    add (dragging ? 1 : 0);
    add ((juce::int64) particles.size());
    for (float I : live.frame.integrity)
        add (juce::roundToInt (I * 2000.0f));
    if (hover >= 0)
        add (juce::roundToInt (live.frame.stressRatio[(size_t) hover] * 100.0f));
    return h;
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
