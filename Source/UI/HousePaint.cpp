#include "HouseModel.h"

// Everything the house view draws. Faces are filled in their own (u, v) metres through
// an affine transform, so patterns, cracks and shadows foreshorten correctly; stroke
// widths are in screen pixels.

namespace rb::ui
{
using namespace house;

namespace
{
    constexpr juce::uint32 kCrackInk = 0xff231d1a;

    juce::Colour ink (float alpha)    { return juce::Colours::black.withAlpha (alpha); }
    juce::Colour chalk (float alpha)  { return juce::Colours::white.withAlpha (alpha); }
    juce::Colour shadow (float alpha) { return juce::Colour (kShadowInk).withAlpha (alpha); }

    juce::AffineTransform nudged (float du, float dv, const juce::AffineTransform& T)
    {
        return juce::AffineTransform::translation (du, dv).followedBy (T);   // move in the face's own metres
    }

    void addBlob (juce::Path& p, const HouseView::Blob& b, float grow)
    {
        for (size_t k = 0; k < b.shape.size(); ++k)
        {
            const P2 q = b.c + b.shape[k] * (b.r * grow);
            if (k == 0) p.startNewSubPath (q); else p.lineTo (q);
        }
        p.closeSubPath();
    }

    /** The part of a crack that has opened at damage `d`: a tapered outline plus its centre line. */
    void crackOutline (const HouseView::CrackLine& c, float d, float widthScale, juce::Path& fill, juce::Path& hair)
    {
        const size_t n = c.pts.size();
        if (n < 2 || c.th[0] >= d)
            return;

        std::array<P2, 42> pts, side;
        std::array<float, 42> half {};
        size_t m = 0;
        for (size_t i = 0; i < n && m < 41; ++i)
        {
            if (c.th[i] < d)
            {
                pts[m] = c.pts[i];
                half[m] = 0.5f * c.width * widthScale * (0.3f + 0.7f * smooth (0.0f, 0.42f, d - c.th[i]));
                ++m;
            }
            else
            {
                // the tip is part-way along this step
                const float t = (d - c.th[i - 1]) / std::max (1.0e-4f, c.th[i] - c.th[i - 1]);
                pts[m] = c.pts[i - 1] + (c.pts[i] - c.pts[i - 1]) * t;
                ++m;
                break;
            }
        }
        if (m < 2)
            return;
        half[m - 1] = 0.0f;   // every crack ends in a point

        hair.startNewSubPath (pts[0]);
        for (size_t i = 1; i < m; ++i) hair.lineTo (pts[i]);

        for (size_t i = 0; i < m; ++i)
        {
            const P2 dir = pts[std::min (i + 1, m - 1)] - pts[i > 0 ? i - 1 : 0];
            const float len = std::max (1.0e-5f, std::hypot (dir.x, dir.y));
            side[i] = P2 (-dir.y, dir.x) * (half[i] / len);
        }
        fill.startNewSubPath (pts[0] + side[0]);
        for (size_t i = 1; i < m; ++i) fill.lineTo (pts[i] + side[i]);
        for (size_t i = m; i-- > 0;)   fill.lineTo (pts[i] - side[i]);
        fill.closeSubPath();
    }

    /** A shape and its shadow: everything between the rectangle and the same rectangle `s` away. */
    juce::Path sweep (juce::Rectangle<float> r, P2 s)
    {
        std::vector<P2> pts;
        for (const P2 c : { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() })
        {
            pts.push_back (c);
            pts.push_back (c + s);
        }
        return convexHull (std::move (pts));
    }

    float chimneyTop (float roofDamage) noexcept
    {
        return kChimTop - 0.2f * smooth (0.3f, 0.8f, roofDamage) - 0.5f * smooth (0.8f, 0.98f, roofDamage);
    }
}

//==============================================================================
// Frame set-up
//==============================================================================
void HouseView::prepareFrame()
{
    material = juce::jlimit (0, kNumFinishes - 1, (int) materialParam->load());
    outside = juce::jlimit (0, kNumBackdrops - 1, (int) voidParam->load());
    if (material != skinMaterial)
        rebuildSkins (material);

    const auto& bd = kBackdrops[outside];
    const juce::Colour a (bd.ambient), s (bd.sun);
    light.amb[0] = a.getFloatRed(); light.amb[1] = a.getFloatGreen(); light.amb[2] = a.getFloatBlue();
    light.sun[0] = s.getFloatRed(); light.sun[1] = s.getFloatGreen(); light.sun[2] = s.getFloatBlue();
    light.dark = juce::Colour (bd.top).getPerceivedBrightness() < 0.45f;

    // Dust hangs over a wrecked house: the sun weakens and everything drifts towards grey.
    const float dusty = smooth (0.45f, 1.0f, meanDamage());
    const float grey = 0.72f * (light.amb[0] + light.amb[1] + light.amb[2]) / 2.2f;
    for (int k = 0; k < 3; ++k)
    {
        light.amb[k] += (grey - light.amb[k]) * 0.3f * dusty;
        light.sun[k] *= 1.0f - 0.35f * dusty;
    }
}

juce::Colour HouseView::lit (juce::Colour base, V3 n, float gain) const noexcept
{
    // Ambient light takes the colour of the outside; the sun adds to faces turned towards it.
    const float d = std::max (0.0f, dot (n, kLight));
    return juce::Colour::fromFloatRGBA (juce::jmin (1.0f, base.getFloatRed()   * (light.amb[0] + light.sun[0] * d) * gain),
                                        juce::jmin (1.0f, base.getFloatGreen() * (light.amb[1] + light.sun[1] * d) * gain),
                                        juce::jmin (1.0f, base.getFloatBlue()  * (light.amb[2] + light.sun[2] * d) * gain),
                                        base.getFloatAlpha());
}

juce::ColourGradient HouseView::skyGradient() const
{
    // The same fill is used for the backdrop and for daylight seen through the house, so they match.
    const auto& bd = kBackdrops[outside];
    const float haze = light.dark ? 0.0f : 0.22f * smooth (0.35f, 1.0f, meanDamage());
    const juce::Colour dust (0xffa39c8e);
    return { mix (juce::Colour (bd.top), dust, haze), 0.0f, 0.0f,
             mix (juce::Colour (bd.bottom), dust, haze), 0.0f, (float) getHeight(), false };
}

//==============================================================================
// Damage shapes
//==============================================================================
juce::Path HouseView::wallHolePath (int w, float d) const
{
    juce::Path p;
    const auto& def = kWalls[w];
    const auto& f = kFinishes[material];
    const auto& s = skins[(size_t) w];

    if (f.hole == HoleCells && s.rows > 0)
    {
        if (d < 0.25f) return p;
        for (int row = 0; row < s.rows; ++row)
            for (int col = 0; col < s.cols; ++col)
                if (cellIsHole (w, row, col, d))
                {
                    juce::Rectangle<float> cell;
                    cellBounds (s, row, col, def.uLen, def.height(), cell);
                    p.addRectangle (cell.expanded (0.004f));   // overlap, so neighbours leave no seam
                }
        return p;
    }

    for (const auto& b : walls[(size_t) w].holes)
        if (b.th < d)
            addBlob (p, b, 0.45f + 0.55f * smooth (b.th, b.th + 0.25f, d));

    if (d > 0.82f)
    {
        const float top = def.height() + 0.6f;
        p.startNewSubPath (-0.1f, top);
        for (int k = 0; k <= 48; ++k)
        {
            const float u = def.uLen * (float) k / 48.0f;
            p.lineTo (u, collapseV (w, u, d));
        }
        p.lineTo (def.uLen + 0.1f, top);
        p.closeSubPath();
    }
    return p;
}

juce::Path HouseView::roofHolePath (bool east, float d) const
{
    juce::Path p;
    if (d < 0.4f) return p;
    const float slopeLen = (0.5f * kW + kEave) / std::cos (kPhi), uLen = kD + 2.0f * kGable;
    const int rows = (int) std::ceil (slopeLen / kTileV), cols = (int) std::ceil (uLen / kTileU) + 1;
    const auto& th = tileThreshold[east ? 0 : 1];
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            if (th[(size_t) (i * cols + j)] + 0.3f < d)
                p.addRectangle (juce::Rectangle<float> ((float) j * kTileU + (i % 2 == 1 ? 0.5f * kTileU : 0.0f) - 0.5f * kTileU,
                                                        (float) i * kTileV, kTileU, kTileV).expanded (0.004f));
    return p;
}

//==============================================================================
// Painting
//==============================================================================
void HouseView::paint (juce::Graphics& g)
{
    for (auto& v : hitVisible) v = false;
    prepareFrame();
    pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor() > 1.05f ? 2.0f : 1.0f;
    interiorFresh = false;

    drawBackdrop (g);
    drawSlabShadow (g);
    drawGround (g);

    drawBushes (g, false);
    drawHeap (g, false);
    drawParticles (g, false);
    drawDust (g, false);

    // The body: a dark footprint first (shows when the house lifts), then the walls that face us.
    {
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
    drawHeap (g, true);
    drawParticles (g, true);
    drawDust (g, true);
    drawAtmosphere (g);
    drawOverlay (g);
}

//==============================================================================
void HouseView::drawBackdrop (juce::Graphics& g)
{
    // The outside only changes with the Outside choice, the haze of a wrecked house and (for the
    // undercroft) the water level, so it is painted once and kept.
    float breach = 0.0f;
    for (float I : live.frame.integrity) breach += dsp::breachAmount (I) / 6.0f;
    const int wPx = juce::jmax (1, juce::roundToInt ((float) getWidth() * pixelScale)), hPx = juce::jmax (1, juce::roundToInt ((float) getHeight() * pixelScale));
    const juce::uint64 key = (juce::uint64) outside | ((juce::uint64) (meanDamage() * 60.0f) << 4) | ((juce::uint64) (outside == 5 ? breach * 80.0f : 0.0f) << 12)
                             | ((juce::uint64) wPx << 20) | ((juce::uint64) hPx << 36) | 1ull << 63;
    if (key != backdropKey || backdropCache.isNull())
    {
        backdropKey = key;
        backdropCache = juce::Image (juce::Image::RGB, wPx, hPx, false);
        juce::Graphics bg (backdropCache);
        bg.addTransform (juce::AffineTransform::scale (pixelScale));
        paintBackdrop (bg);
    }
    g.drawImage (backdropCache, getLocalBounds().toFloat());
}

void HouseView::paintBackdrop (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float w = r.getWidth(), h = r.getHeight(), k600 = w / 600.0f;
    const auto& bd = kBackdrops[outside];
    const juce::Colour top (bd.top), bottom (bd.bottom);

    g.setGradientFill (skyGradient());
    g.fillRect (r);

    float breach = 0.0f;
    for (float I : live.frame.integrity) breach += dsp::breachAmount (I) / 6.0f;

    switch (outside)
    {
        case 0:   // open sky: sun from the upper left, a few clouds, far hills
        {
            g.setGradientFill (juce::ColourGradient (chalk (0.45f), w * 0.18f, h * 0.1f, chalk (0.0f), w * 0.8f, h * 0.1f, true));
            g.fillRect (r);

            struct Cloud { float x, y, s; };
            for (const auto c : { Cloud { 0.17f, 0.15f, 1.0f }, Cloud { 0.72f, 0.1f, 0.8f }, Cloud { 0.93f, 0.33f, 0.62f }, Cloud { 0.44f, 0.055f, 0.5f } })
            {
                const float cx = c.x * w, cy = c.y * h, u = 44.0f * c.s * k600;
                juce::Path cloud;
                cloud.addEllipse (cx - 1.6f * u, cy - 0.32f * u, 1.5f * u, 0.74f * u);
                cloud.addEllipse (cx - 0.95f * u, cy - 0.78f * u, 1.5f * u, 1.2f * u);
                cloud.addEllipse (cx - 0.05f * u, cy - 0.5f * u, 1.45f * u, 0.92f * u);
                cloud.addRoundedRectangle (cx - 1.5f * u, cy, 2.8f * u, 0.42f * u, 0.21f * u);
                g.setColour (chalk (0.42f));
                g.fillPath (cloud);
            }
            for (int layer = 0; layer < 2; ++layer)
            {
                juce::Path hills;
                const float base = h * (0.79f + 0.075f * (float) layer), fl = (float) layer;
                hills.startNewSubPath (0.0f, h);
                for (float x = 0.0f; x <= w + 8.0f; x += 8.0f)
                    hills.lineTo (x, base - h * 0.032f * std::sin (x / k600 * 0.011f + 1.0f + 2.0f * fl)
                                          - h * 0.015f * std::sin (x / k600 * 0.027f + fl));
                hills.lineTo (w, h);
                hills.closeSubPath();
                g.setColour (mix (bottom, juce::Colour (0xff86b39f), 0.22f + 0.1f * fl).withAlpha (0.4f));
                g.fillPath (hills);
            }
            break;
        }
        case 1:   // a long vaulted hall, lighter towards the far end
        {
            for (int k = 0; k < 6; ++k)
            {
                const float s = 1.0f - 0.15f * (float) k;
                const float aw = w * 1.3f * s, ah = h * 1.45f * s;
                juce::Path arch;
                arch.addRoundedRectangle (w * 0.5f - aw * 0.5f, h * 0.98f - ah, aw, ah + h, aw * 0.5f, aw * 0.42f, true, true, false, false);
                g.setColour (mix (bottom.darker (0.22f), top.brighter (0.28f), (float) k / 5.0f));
                g.fillPath (arch);
                g.setColour (ink (0.07f));
                g.strokePath (arch, juce::PathStrokeType (1.2f));
            }
            g.setGradientFill (juce::ColourGradient (bottom.darker (0.3f).withAlpha (0.0f), 0.0f, h * 0.74f,
                                                     bottom.darker (0.3f).withAlpha (0.55f), 0.0f, h, false));
            g.fillRect (0.0f, h * 0.74f, w, h * 0.26f);
            break;
        }
        case 2:   // a barrel-vaulted tunnel running off into the dark, a lamp at each rib
        {
            for (int k = 0; k < 6; ++k)
            {
                const float s = 1.0f - 0.155f * (float) k;
                const float aw = w * 1.42f * s, ah = h * 1.08f * s;
                const float x0 = w * 0.5f - aw * 0.5f, y0 = h * 0.9f - ah * 0.86f;
                juce::Path vault;
                vault.addRoundedRectangle (x0, y0, aw, ah + h, aw * 0.5f, std::min (ah * 0.62f, aw * 0.5f), true, true, false, false);
                g.setColour (mix (top.brighter (0.06f), bottom.darker (0.5f), (float) k / 5.0f));
                g.fillPath (vault);
                g.setColour (ink (0.16f));
                g.strokePath (vault, juce::PathStrokeType (2.2f * k600));
                if (k > 0 && k < 5)
                {
                    const float lamp = 8.0f * s * k600;
                    const P2 c (w * 0.5f, y0 + lamp * 1.8f);
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffe2a8).withAlpha (0.5f), c.x, c.y,
                                                             juce::Colour (0xffffe2a8).withAlpha (0.0f), c.x + lamp * 4.5f, c.y, true));
                    g.fillEllipse (c.x - lamp * 4.5f, c.y - lamp * 4.5f, lamp * 9.0f, lamp * 9.0f);
                    g.setColour (juce::Colour (0xfffff3d6));
                    g.fillEllipse (c.x - lamp * 0.5f, c.y - lamp * 0.3f, lamp, lamp * 0.6f);
                }
            }
            // the tunnel floor, with two rails running away
            g.setGradientFill (juce::ColourGradient (bottom.darker (0.45f).withAlpha (0.0f), 0.0f, h * 0.72f,
                                                     bottom.darker (0.45f).withAlpha (0.7f), 0.0f, h, false));
            g.fillRect (0.0f, h * 0.72f, w, h * 0.28f);
            break;
        }
        case 3:   // the abyss: a pool of light and drifting motes
        {
            g.setGradientFill (juce::ColourGradient (top.brighter (0.4f), w * 0.5f, h * 0.44f, bottom.darker (0.55f), w * 0.5f + w * 0.8f, h * 0.44f, true));
            g.fillRect (r);
            juce::Path shaft = quad ({ w * 0.41f, 0.0f }, { w * 0.59f, 0.0f }, { w * 0.84f, h }, { w * 0.16f, h });
            g.setGradientFill (juce::ColourGradient (chalk (0.085f), 0.0f, 0.0f, chalk (0.0f), 0.0f, h, false));
            g.fillPath (shaft);
            for (int k = 0; k < 70; ++k)
            {
                const float sz = (1.0f + 2.4f * std::pow (hash (k, 3), 3.0f)) * k600;
                g.setColour (chalk (0.05f + 0.2f * hash (k, 4)));
                g.fillEllipse (w * hash (k, 1), h * hash (k, 2), sz, sz);
            }
            break;
        }
        case 4:   // a city on fire behind two rows of rooftops
        {
            const juce::Colour fire (0xffffa851);
            g.setGradientFill (juce::ColourGradient (fire.withAlpha (0.62f), w * 0.5f, h, fire.withAlpha (0.0f), w * 0.5f, h * 0.22f, true));
            g.fillRect (r);

            for (int col = 0; col < 3; ++col)   // smoke
                for (int k = 0; k < 6; ++k)
                {
                    const float fk = (float) k, rad = (34.0f + 14.0f * fk) * k600;
                    const float cx = w * (0.2f + 0.3f * (float) col) + 13.0f * fk * k600, cy = h * (0.6f - 0.09f * fk);
                    g.setGradientFill (juce::ColourGradient (ink (0.16f), cx, cy, ink (0.0f), cx + rad, cy, true));
                    g.fillEllipse (cx - rad, cy - rad, 2.0f * rad, 2.0f * rad);
                }

            for (int layer = 0; layer < 2; ++layer)
            {
                juce::Path city, lit2;
                city.startNewSubPath (0.0f, h);
                int i = 0;
                for (float x = 0.0f; x < w; ++i)
                {
                    const float bw = w * (layer == 0 ? 0.03f + 0.06f * hash (i, 11) : 0.045f + 0.08f * hash (i, 13));
                    const float bh = h * (layer == 0 ? 0.2f + 0.26f * hash (i, 12) : 0.07f + 0.2f * hash (i, 14));
                    city.lineTo (x, h - bh);
                    city.lineTo (x + bw, h - bh);
                    if (layer == 1)
                        for (int k = 0; k < 3; ++k)
                            if (hash (i, k, 15) < 0.5f)
                                lit2.addRectangle (x + bw * (0.15f + 0.6f * hash (i, k, 16)), h - bh * (0.2f + 0.7f * hash (i, k, 17)), 3.0f * k600, 4.0f * k600);
                    x += bw;
                }
                city.lineTo (w, h);
                city.closeSubPath();
                g.setColour (layer == 0 ? mix (top, juce::Colour (0xffd27a45), 0.28f).withAlpha (0.6f) : juce::Colour (0xff1f1512).withAlpha (0.9f));
                g.fillPath (city);
                g.setColour (fire.withAlpha (0.8f));
                g.fillPath (lit2);
            }
            for (int k = 0; k < 44; ++k)   // embers
            {
                const float sz = (1.2f + 1.6f * hash (k, 23)) * k600;
                g.setColour (juce::Colour (0xffffb866).withAlpha (0.25f + 0.65f * hash (k, 24)));
                g.fillEllipse (w * hash (k, 21), h * 0.85f * hash (k, 22), sz, sz);
            }
            break;
        }
        case 5:   // the undercroft fills with water as the walls let it in
        {
            const auto stone = bottom.darker (0.3f).withAlpha (0.5f);
            juce::Path vault;
            for (int k = 0; k < 4; ++k)
            {
                const float x0 = w * (0.05f + 0.3f * (float) k);
                vault.addRectangle (x0, 0.0f, w * 0.07f, h);
                if (k < 3)
                {
                    const float xa = x0 + w * 0.07f, xb = x0 + w * 0.3f;
                    vault.startNewSubPath (xa, 0.0f);
                    vault.lineTo (xa, h * 0.26f);
                    vault.quadraticTo (0.5f * (xa + xb), h * 0.02f, xb, h * 0.26f);
                    vault.lineTo (xb, 0.0f);
                    vault.closeSubPath();
                }
            }
            g.setColour (stone);
            g.fillPath (vault);

            const float level = h - h * (0.15f + 0.3f * breach);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a9597).withAlpha (0.6f), 0.0f, level,
                                                     juce::Colour (0xff1d464c).withAlpha (0.85f), 0.0f, h, false));
            g.fillRect (0.0f, level, w, h - level);
            for (int k = 0; k < 14; ++k)
            {
                const float x = w * hash (k, 31), y = level + (h - level) * (0.08f + 0.9f * hash (k, 32)), len = (20.0f + 46.0f * hash (k, 33)) * k600;
                g.setColour (chalk (0.1f + 0.14f * hash (k, 34)));
                g.fillRect (x, y, len, 1.0f);
            }
            g.setColour (chalk (0.4f));
            g.fillRect (0.0f, level - 0.5f, w, 1.0f);
            break;
        }
        case 6:   // concrete canyon: slabs closing in from both sides
        {
            for (int k = 0; k < 3; ++k)
                for (int side = 0; side < 2; ++side)
                {
                    const float fk = (float) k;
                    const float bw = w * (0.36f - 0.11f * fk);
                    const float y = h * (0.02f + 0.14f * fk + 0.07f * hash (k, side, 41));
                    const juce::Rectangle<float> slab (side == 0 ? 0.0f : w - bw, y, bw, h - y);
                    g.setColour (mix (top.darker (0.07f), bottom.darker (0.32f), fk / 2.0f).withAlpha (0.92f));
                    g.fillRect (slab);
                    g.setColour (ink (0.05f));
                    for (float jy = y + 26.0f * k600; jy < h; jy += 30.0f * k600)
                        g.fillRect (slab.getX(), jy, bw, 1.0f);
                    g.setColour (ink (0.12f));
                    g.fillRect (side == 0 ? slab.getRight() - 5.0f * k600 : slab.getX(), y, 5.0f * k600, h - y);
                }
            g.setGradientFill (juce::ColourGradient (chalk (0.0f), 0.0f, h * 0.55f, chalk (0.3f), 0.0f, h, false));
            g.fillRect (0.0f, h * 0.55f, w, h * 0.45f);
            break;
        }
        default: break;
    }

    // darker corners keep the eye on the house
    juce::ColourGradient vignette (ink (0.0f), r.getCentreX(), r.getCentreY(), ink (light.dark ? 0.42f : 0.22f), 0.0f, 0.0f, true);
    vignette.addColour (0.58, ink (0.0f));
    g.setGradientFill (vignette);
    g.fillRect (r);
}

void HouseView::drawSlabShadow (juce::Graphics& g)
{
    // Soft shadow of lawn + house on whatever the lawn floats over. The blur is the costly
    // part, so it is kept until the view turns or the house settles.
    const float tilt = tiltAngle();
    if (slabShadow.isNull() || std::abs (yaw - slabShadowYaw) > 0.003f || std::abs (scale - slabShadowScale) > 0.001f
        || std::abs (tilt - slabShadowTilt) > 0.004f)
    {
        slabShadowYaw = yaw;
        slabShadowScale = scale;
        slabShadowTilt = tilt;
        slabShadow = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
        juce::Graphics sg (slabShadow);

        const P2 savedShake = shake;
        shake = {};
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
        shake = savedShake;

        juce::DropShadow (ink (0.26f), juce::roundToInt (0.36f * scale), { 0, juce::roundToInt (0.07f * scale) })
            .drawForPath (sg, convexHull (pts));
    }
    g.setOpacity (light.dark ? 0.75f : 1.0f);
    g.drawImageAt (slabShadow, juce::roundToInt (shake.x), juce::roundToInt (shake.y));
    g.setOpacity (1.0f);
}

//==============================================================================
// Lawn
//==============================================================================
void HouseView::drawGroundSide (juce::Graphics& g, V3 a, V3 b, V3 n, int index)
{
    // The cut edge of the lawn: turf, dark topsoil, paler subsoil with stones and roots in it.
    const V3 e = b - a;
    const float len = std::sqrt (dot (e, e));
    const auto T = planeTransform (a + V3 { 0, 0, -kSlabT }, e * (1.0f / len), { 0, 0, 1 }, false);
    const float px = scale / 60.0f;
    const float dry = smooth (0.3f, 0.9f, damageOf (dsp::Floor));

    juce::Path face;
    face.addRectangle (0.0f, 0.0f, len, kSlabT);
    g.saveState();
    g.reduceClipRegion (face, T);

    g.setColour (lit (juce::Colour (kSubsoil), n));
    g.fillPath (face, T);

    juce::Path soil;
    soil.startNewSubPath (0.0f, kSlabT);
    for (int k = 0; k <= 18; ++k)
        soil.lineTo (len * (float) k / 18.0f, kSlabT - 0.15f - 0.05f * hash (index, k, 3));
    soil.lineTo (len, kSlabT);
    soil.closeSubPath();
    g.setColour (lit (juce::Colour (kTopsoil), n));
    g.fillPath (soil, T);

    juce::Path rocks, rockTops;
    for (int k = 0; k < 10; ++k)
    {
        const float cu = len * hash (index, k, 11), cv = 0.035f + 0.12f * hash (index, k, 12);
        const float rx = 0.035f + 0.05f * hash (index, k, 13), ry = rx * (0.55f + 0.2f * hash (index, k, 14));
        rocks.addEllipse (cu - rx, cv - ry, 2.0f * rx, 2.0f * ry);
        rockTops.addEllipse (cu - rx * 0.75f, cv - ry * 0.05f, 1.3f * rx, ry * 0.85f);
    }
    g.setColour (lit (juce::Colour (0xff8a8073), n));
    g.fillPath (rocks, T);
    g.setColour (lit (juce::Colour (0xffb3a99b), n));
    g.fillPath (rockTops, T);

    juce::Path roots;
    for (int k = 0; k < 5; ++k)
    {
        const float u0 = len * hash (index, k, 21), lean = (hash (index, k, 22) - 0.5f) * 0.2f;
        roots.startNewSubPath (u0, kSlabT - 0.05f);
        roots.quadraticTo (u0 + lean * 0.4f, kSlabT - 0.13f, u0 + lean, kSlabT - 0.19f - 0.07f * hash (index, k, 23));
    }
    g.setColour (juce::Colour (0xff2e1e13).withAlpha (0.75f));
    g.strokePath (roots, juce::PathStrokeType (1.1f * px), T);

    // turf, with blades hanging over the cut
    juce::Path turf;
    turf.addRectangle (0.0f, kSlabT - 0.05f, len, 0.05f);
    for (int k = 0; (float) k * 0.06f < len; ++k)
    {
        const float u0 = (float) k * 0.06f;
        turf.addTriangle (u0, kSlabT - 0.05f, u0 + 0.06f, kSlabT - 0.05f,
                          u0 + 0.03f + 0.03f * (hash (index, k, 32) - 0.5f), kSlabT - 0.062f - 0.045f * hash (index, k, 31));
    }
    g.setColour (lit (mix (juce::Colour (kGrassSide), juce::Colour (kGrassDry).darker (0.2f), dry), n));
    g.fillPath (turf, T);

    g.setGradientFill (juce::ColourGradient (ink (0.3f), P2 (0.0f, 0.0f).transformedBy (T), ink (0.0f), P2 (0.0f, 0.17f).transformedBy (T), false));
    g.fillPath (face, T);
    g.restoreState();

    g.setColour (ink (0.25f));
    g.strokePath (face, juce::PathStrokeType (0.8f), T);
}

void HouseView::paintGroundBase (juce::Graphics& g)
{
    // The parts of the lawn that only change with the view: its cut sides, the turf, mowing
    // stripes, tufts and daisies.
    const V3 cam = toCamera();
    const V3 up { 0, 0, 1 };
    const float d = damageOf (dsp::Floor);
    const float px = scale / 60.0f;

    struct Side { V3 a, b, n; };
    const Side sides[] = {
        { { -kSlabX, -kSlabY, 0 }, { kSlabX, -kSlabY, 0 }, { 0, -1, 0 } },
        { { kSlabX, -kSlabY, 0 }, { kSlabX, kSlabY, 0 }, { 1, 0, 0 } },
        { { kSlabX, kSlabY, 0 }, { -kSlabX, kSlabY, 0 }, { 0, 1, 0 } },
        { { -kSlabX, kSlabY, 0 }, { -kSlabX, -kSlabY, 0 }, { -1, 0, 0 } },
    };
    for (int k = 0; k < 4; ++k)
        if (dot (sides[k].n, cam) > 0.0f)
            drawGroundSide (g, sides[k].a, sides[k].b, sides[k].n, k);

    const auto T = planeTransform ({ -kSlabX, -kSlabY, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, false);
    juce::Path top;
    top.addRectangle (0.0f, 0.0f, 2.0f * kSlabX, 2.0f * kSlabY);
    const auto grass = lit (mix (juce::Colour (kGrass), juce::Colour (kGrassDry), smooth (0.3f, 0.9f, d)), up);
    g.setColour (grass);
    g.fillPath (top, T);

    g.saveState();
    g.reduceClipRegion (top, T);

    // brighter towards the light
    g.setGradientFill (juce::ColourGradient (chalk (0.1f), toScreen ({ -kSlabX, -kSlabY, 0 }), ink (0.09f), toScreen ({ kSlabX, kSlabY, 0 }), false));
    g.fillPath (top, T);

    // mowing stripes
    juce::Path stripes;
    for (float u = 0.0f; u < 2.0f * kSlabX; u += 0.7f)
        stripes.addRectangle (u, 0.0f, 0.35f, 2.0f * kSlabY);
    g.setColour (chalk (0.05f));
    g.fillPath (stripes, T);

    // tufts of longer grass, standing up from the lawn
    juce::Path blades, bladeTips;
    for (size_t k = 0; k < tufts.size(); ++k)
    {
        const auto& t = tufts[k];
        const P2 base = toScreen ({ t.x, t.y, 0.0f });
        for (int j = -1; j <= 1; ++j)
        {
            const P2 tip = toScreen ({ t.x + t.lean + 0.022f * (float) j, t.y, t.h * (j == 0 ? 1.0f : 0.7f) });
            auto& path = k % 4 == 0 ? bladeTips : blades;
            path.startNewSubPath (base.x + 0.9f * px * (float) j, base.y);
            path.lineTo (tip);
        }
    }
    g.setColour (grass.darker (0.3f).withAlpha (0.7f));
    g.strokePath (blades, juce::PathStrokeType (1.0f * px));
    g.setColour (grass.brighter (0.35f).withAlpha (0.8f));
    g.strokePath (bladeTips, juce::PathStrokeType (1.0f * px));

    const float bloom = 1.0f - smooth (0.25f, 0.7f, d);
    if (bloom > 0.02f)
    {
        g.setColour (lit (juce::Colour (0xfffdfbf2), up).withAlpha (0.95f * bloom));
        g.fillPath (flowersA, T);
        g.setColour (lit (juce::Colour (0xfff6cf4f), up).withAlpha (0.95f * bloom));
        g.fillPath (flowersB, T);
    }
    g.restoreState();

    g.setColour (chalk (0.16f));   // the lit edge of the turf
    g.strokePath (top, juce::PathStrokeType (0.8f), T);
}

void HouseView::drawGround (juce::Graphics& g)
{
    const V3 cam = toCamera();
    const V3 up { 0, 0, 1 };
    const float d = damageOf (dsp::Floor);
    const float px = scale / 60.0f;

    // the static part of the lawn, from its cache
    {
        const int wPx = juce::jmax (1, juce::roundToInt ((float) getWidth() * pixelScale)), hPx = juce::jmax (1, juce::roundToInt ((float) getHeight() * pixelScale));
        juce::uint64 key = 1469598103934665603ull;
        for (const int v : { juce::roundToInt (yaw * 4000.0f), juce::roundToInt (scale * 100.0f), outside, juce::roundToInt (d * 60.0f),
                              juce::roundToInt (meanDamage() * 50.0f), wPx, hPx })
            key = (key ^ (juce::uint64) (juce::int64) v) * 1099511628211ull;
        if (key != groundKey || groundCache.isNull())
        {
            groundKey = key;
            groundCache = juce::Image (juce::Image::ARGB, wPx, hPx, true);
            juce::Graphics lg (groundCache);
            lg.addTransform (juce::AffineTransform::scale (pixelScale));
            const P2 savedShake = shake;
            shake = {};
            paintGroundBase (lg);
            shake = savedShake;
        }
        g.drawImageTransformed (groundCache, juce::AffineTransform::scale (1.0f / pixelScale).translated (shake.x, shake.y));
    }

    const auto T = planeTransform ({ -kSlabX, -kSlabY, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, false);
    juce::Path top;
    top.addRectangle (0.0f, 0.0f, 2.0f * kSlabX, 2.0f * kSlabY);
    const auto grass = lit (mix (juce::Colour (kGrass), juce::Colour (kGrassDry), smooth (0.3f, 0.9f, d)), up);

    g.saveState();
    g.reduceClipRegion (top, T);
    {
        // shadows: bushes, then the house (with a soft edge), then where the walls meet the grass
        auto onto = [] (V3 p) { return p - kLight * (p.z / kLight.z); };
        {
            juce::Path bushShade;
            struct B { float x, y, r; };
            for (const auto b : { B { -1.45f, -0.95f, 0.26f }, B { -1.15f, -1.02f, 0.2f }, B { 0.42f, -0.95f, 0.22f }, B { 0.68f, -0.98f, 0.18f },
                                  B { 1.85f, 1.55f, 0.28f }, B { 2.0f, 1.28f, 0.2f }, B { -1.95f, 1.65f, 0.22f } })
            {
                const V3 c = onto ({ b.x, b.y, b.r * 0.7f });
                bushShade.addEllipse (c.x + kSlabX - b.r * 1.05f, c.y + kSlabY - b.r * 0.95f, b.r * 2.1f, b.r * 1.9f);
            }
            g.setColour (shadow (0.2f));
            g.fillPath (bushShade, T);
        }
        {
            std::vector<P2> pts;
            for (float x : { -0.5f * kW - kEave, 0.5f * kW + kEave })
                for (float y : { -0.5f * kD - kGable, 0.5f * kD + kGable })
                    for (float z : { 0.0f, kH - kSlope * kEave })
                        pts.push_back (toScreen (onto (houseToWorld ({ x, y, z }))));
            for (float y : { -0.5f * kD - kGable, 0.5f * kD + kGable })
                pts.push_back (toScreen (onto (houseToWorld ({ 0.0f, y, kH + kR + kTz }))));
            const float chimTop = chimneyTop (damageOf (dsp::Ceiling));
            if (damageOf (dsp::Ceiling) < 0.9f)
                for (float x : { kChimX - kChimA, kChimX + kChimA })
                    for (float y : { kChimY - kChimA, kChimY + kChimA })
                        pts.push_back (toScreen (onto (houseToWorld ({ x, y, chimTop }))));
            const auto hull = convexHull (pts);
            g.setColour (shadow (0.07f));
            g.strokePath (hull, juce::PathStrokeType (7.0f * px, juce::PathStrokeType::curved));
            g.setColour (shadow (0.2f));
            g.fillPath (hull);
        }
        {
            juce::Path contact, tight;
            contact.addRectangle (kSlabX - 0.5f * kW - 0.14f, kSlabY + kHouseY - 0.5f * kD - 0.14f, kW + 0.28f, kD + 0.28f);
            tight.addRectangle (kSlabX - 0.5f * kW - 0.05f, kSlabY + kHouseY - 0.5f * kD - 0.05f, kW + 0.1f, kD + 0.1f);
            g.setColour (shadow (0.12f));
            g.fillPath (contact, T);
            g.setColour (shadow (0.16f));
            g.fillPath (tight, T);
        }

        // fallen material banks up against each wall as it goes
        {
            const auto& fin = kFinishes[material];
            for (int w = 0; w < 4; ++w)
            {
                const float k = smooth (0.3f, 1.0f, damageOf (w));
                if (k <= 0.01f) continue;
                const auto& def = kWalls[w];
                const V3 mid = def.o + def.u * (0.5f * def.uLen) + def.n * (0.1f + 0.12f * k);
                const float along = def.uLen * (0.34f + 0.2f * k), out = 0.16f + 0.3f * k;
                const bool alongX = std::abs (def.u.x) > 0.5f;
                const float rx = alongX ? along : out, ry = alongX ? out : along;
                juce::Path mound, core;
                mound.addEllipse (mid.x + kSlabX - rx, mid.y + kHouseY + kSlabY - ry, 2.0f * rx, 2.0f * ry);
                core.addEllipse (mid.x + kSlabX - rx * 0.7f, mid.y + kHouseY + kSlabY - ry * 0.65f, 1.4f * rx, 1.3f * ry);
                const auto dustCol = lit (mix (juce::Colour (fin.debris), juce::Colour (0xff8f897d), 0.35f), up, 0.95f);
                g.setColour (dustCol.withAlpha (0.3f * k));
                g.fillPath (mound, T);
                g.setColour (dustCol.withAlpha (0.38f * k));
                g.fillPath (core, T);
            }
        }

        // fissures (Floor): a trench with soil on its far bank and darkness in the bottom
        juce::Path trench, hair;
        for (const auto& c : ground.cracks)
            crackOutline (c, d, 0.5f + 1.3f * d, trench, hair);
        if (! hair.isEmpty())
        {
            g.setColour (grass.darker (0.55f).withAlpha (0.75f));
            g.strokePath (hair, juce::PathStrokeType (0.9f * px), T);
            g.setColour (lit (juce::Colour (kTopsoil), up, 1.35f));
            g.fillPath (trench, T);
            g.saveState();
            g.reduceClipRegion (trench, T);
            const P2 towardCam = P2 (std::sin (yaw), -std::cos (yaw)) * (0.018f + 0.03f * d);
            g.setColour (juce::Colour (0xff120c08));
            g.fillPath (trench, nudged (towardCam.x, towardCam.y, T));
            g.restoreState();
            g.setColour (grass.brighter (0.4f).withAlpha (0.4f));
            g.strokePath (trench, juce::PathStrokeType (0.7f * px), T);
        }

        // stepping stones from the door to the edge of the lawn, each a low slab
        {
            const float doorX = -0.5f * kW + 0.925f;
            juce::Random stones (8);
            juce::Path slabs;
            for (float y = -0.5f * kD + kHouseY - 0.62f; y > -kSlabY + 0.15f; y -= 0.42f)
            {
                const float jx = (stones.nextFloat() - 0.5f) * 0.12f;
                slabs.addRoundedRectangle (doorX + jx - 0.2f + kSlabX, y - 0.13f + kSlabY, 0.4f, 0.27f, 0.07f);
            }
            const float rise = 0.035f;
            const P2 lift = toScreen ({ 0, 0, rise }) - toScreen ({ 0, 0, 0 });
            g.setColour (shadow (0.22f));
            g.fillPath (slabs, nudged (-kLight.x * rise / kLight.z + 0.012f, -kLight.y * rise / kLight.z + 0.012f, T));
            g.setColour (lit (juce::Colour (kStone).darker (0.32f), { 0, -1, 0 }));
            g.fillPath (slabs, T);
            const auto topT = T.translated (lift.x, lift.y);
            g.setColour (lit (juce::Colour (kStone), up));
            g.fillPath (slabs, topT);
            g.setColour (ink (0.14f));
            g.strokePath (slabs, juce::PathStrokeType (0.7f), topT);
        }

        // the ring a detonation sends out across the grass
        if (blastAge < 0.55f)
        {
            const float t = blastAge / 0.55f, ease = 1.0f - std::pow (1.0f - t, 3.0f);
            const float radius = 0.9f + 3.6f * ease;
            juce::Path ring;
            ring.addEllipse (kSlabX - radius, kSlabY + kHouseY - radius, 2.0f * radius, 2.0f * radius);
            g.setColour (chalk (0.42f * (1.0f - t) * (1.0f - t)));
            g.strokePath (ring, juce::PathStrokeType ((8.0f - 6.0f * t) * px), T);
        }
    }
    g.restoreState();
    hitPaths[dsp::Floor] = top;
    hitPaths[dsp::Floor].applyTransform (T);
    hitVisible[dsp::Floor] = true;

    // door step (attached to the house, so it settles with it)
    {
        const float u0 = -0.5f * kW + 0.43f, u1 = -0.5f * kW + 1.42f, y0 = -0.5f * kD - 0.32f, y1 = -0.5f * kD, hgt = 0.09f;
        const V3 tl = houseToWorld ({ u0, y0, hgt }), tr = houseToWorld ({ u1, y0, hgt }), br = houseToWorld ({ u1, y1, hgt }), bl = houseToWorld ({ u0, y1, hgt });
        const V3 dn = houseDir ({ 0, 0, -hgt });
        if (dot (houseDir ({ 0, -1, 0 }), cam) > 0.0f)
        {
            g.setColour (lit (juce::Colour (kStone).darker (0.12f), houseDir ({ 0, -1, 0 })));
            g.fillPath (quad (toScreen (tl), toScreen (tr), toScreen (tr + dn), toScreen (tl + dn)));
        }
        if (dot (houseDir ({ 1, 0, 0 }), cam) > 0.0f)
        {
            g.setColour (lit (juce::Colour (kStone).darker (0.12f), houseDir ({ 1, 0, 0 })));
            g.fillPath (quad (toScreen (tr), toScreen (br), toScreen (br + dn), toScreen (tr + dn)));
        }
        else
        {
            g.setColour (lit (juce::Colour (kStone).darker (0.12f), houseDir ({ -1, 0, 0 })));
            g.fillPath (quad (toScreen (tl), toScreen (bl), toScreen (bl + dn), toScreen (tl + dn)));
        }
        g.setColour (lit (juce::Colour (kStone), up));
        g.fillPath (quad (toScreen (tl), toScreen (tr), toScreen (br), toScreen (bl)));
    }
}

//==============================================================================
// Walls
//==============================================================================
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

    const auto& f = kFinishes[material];
    const float d = damageOf (w);

    // Where the shadow of something standing 1 m proud of this wall would fall, in the wall's (u, v).
    const float ln = dot (def.n, kLight);
    const bool sunlit = ln > 0.08f;
    const P2 shift = sunlit ? P2 (-dot (def.u, kLight) / ln, juce::jmax (-3.2f, -kLight.z / ln)) : P2 (0.3f, -0.6f);

    g.saveState();
    g.reduceClipRegion (outline, T);

    drawFinish (g, w, T, n);

    // foundation course, and grime where the wall meets the ground
    {
        juce::Path plinth, edge, low;
        plinth.addRectangle (0.0f, 0.0f, def.uLen, kPlinth);
        edge.addRectangle (0.0f, kPlinth - 0.014f, def.uLen, 0.014f);
        low.addRectangle (0.0f, 0.0f, def.uLen, 0.5f);
        g.setColour (lit (mix (juce::Colour (f.wall), juce::Colour (0xff7d786f), 0.55f), n));
        g.fillPath (plinth, T);
        g.setColour (chalk (0.22f));
        g.fillPath (edge, T);
        g.setGradientFill (juce::ColourGradient (ink (0.17f), P2 (0.0f, 0.0f).transformedBy (T), ink (0.0f), P2 (0.0f, 0.5f).transformedBy (T), false));
        g.fillPath (low, T);
    }

    // the roof's shadow down the top of the wall
    const float roofLeft = 1.0f - 0.85f * smooth (0.5f, 1.0f, damageOf (dsp::Ceiling));
    if (sunlit)
    {
        juce::Path cast;
        if (def.gable)
        {
            const P2 s = shift * kGable;
            const P2 a (-kEave, kH - kSlope * kEave), b (0.5f * def.uLen, kH + kR), c (def.uLen + kEave, kH - kSlope * kEave);
            cast.addPath (quad (a, b, b + s, a + s));
            cast.addPath (quad (b, c, c + s, b + s));
        }
        else
        {
            const float edge = kH - kSlope * kEave + shift.y * kEave;
            cast.addRectangle (-1.0f, edge, def.uLen + 2.0f, kH - edge + 0.1f);
        }
        g.setColour (shadow (0.24f * roofLeft));
        g.fillPath (cast, T);
    }
    else
    {
        // out of the sun: just a soft darkening under the eaves
        juce::Path band;
        band.addRectangle (0.0f, kH - 0.35f, def.uLen, 0.35f + kR);
        g.setGradientFill (juce::ColourGradient (ink (0.0f), P2 (0.0f, kH - 0.35f).transformedBy (T), ink (0.13f * roofLeft), P2 (0.0f, kH).transformedBy (T), false));
        g.fillPath (band, T);
    }

    drawOpenings (g, w, T, n, shift, sunlit);
    drawWallDamage (g, w, T, n, shift);
    g.restoreState();

    const float standing = 1.0f - smooth (0.8f, 0.9f, d);
    if (standing > 0.01f)
    {
        g.setColour (ink (0.22f * standing));
        g.strokePath (outline, juce::PathStrokeType (0.9f), T);
    }
}

void HouseView::drawFinish (juce::Graphics& g, int w, const juce::AffineTransform& T, V3 n)
{
    const auto& def = kWalls[w];
    const auto& f = kFinishes[material];
    const auto& s = skins[(size_t) w];
    const auto outline = wallOutline (def);
    const float uLen = def.uLen, vLen = def.height(), px = scale / 60.0f;
    const auto base = lit (juce::Colour (f.wall), n);

    if (f.pattern == Glass)
    {
        // curtain wall: the dim inside shows through, with the sky lying on the glass
        const juce::Colour sky (kBackdrops[outside].top);
        g.setColour (juce::Colour (kInterior));
        g.fillPath (outline, T);
        g.setGradientFill (juce::ColourGradient (mix (base, sky.brighter (0.5f), 0.35f).withAlpha (0.88f), P2 (0.0f, vLen).transformedBy (T),
                                                 mix (base, juce::Colour (0xff3f5f6e), 0.5f).withAlpha (0.8f), P2 (uLen, 0.0f).transformedBy (T), false));
        g.fillPath (outline, T);
        g.setColour (chalk (0.1f));
        g.fillPath (s.toneLight, T);
        g.setColour (ink (0.09f));
        g.fillPath (s.toneDark, T);

        juce::Path streaks;
        for (int k = 0; k < 3; ++k)
        {
            const float u0 = uLen * (0.1f + 0.3f * (float) k) - 0.4f;
            streaks.addQuadrilateral (u0, 0.0f, u0 + 0.2f, 0.0f, u0 + 0.2f + vLen * 0.5f, vLen, u0 + vLen * 0.5f, vLen);
        }
        g.setColour (chalk (0.08f));
        g.fillPath (streaks, T);

        g.setColour (lit (juce::Colour (f.line), n));
        g.strokePath (s.joints, juce::PathStrokeType (2.2f * px), T);
        g.setColour (chalk (0.3f));
        g.strokePath (s.joints, juce::PathStrokeType (0.6f * px), T.translated (-0.8f * px, -0.8f * px));
        return;
    }

    g.setColour (base);
    g.fillPath (outline, T);

    switch (f.pattern)
    {
        case Ribs:
        {
            // corrugation: a lit crest and a shaded trough per rib
            juce::Path crest, trough, seams, runs;
            for (float u = 0.0f; u < uLen; u += f.a)
            {
                crest.addRectangle (u, 0.0f, f.a * 0.36f, vLen);
                trough.addRectangle (u + f.a * 0.55f, 0.0f, f.a * 0.33f, vLen);
            }
            g.setColour (chalk (0.14f));
            g.fillPath (crest, T);
            g.setColour (ink (0.14f));
            g.fillPath (trough, T);

            for (int k = 0; k < 16; ++k)   // streaks running down from the fixings
            {
                const float u = uLen * hash (w, k, 61), v1 = vLen * (0.35f + 0.65f * hash (w, k, 62)), len = 0.3f + 0.9f * hash (w, k, 63);
                runs.addRectangle (u, v1 - len, 0.03f + 0.05f * hash (w, k, 64), len);
            }
            g.setColour (juce::Colour (f.under).withAlpha (material == 3 ? 0.3f : 0.12f));
            g.fillPath (runs, T);

            for (float v = 1.05f; v < vLen; v += 1.05f)
                seams.addRectangle (0.0f, v, uLen, 0.022f);
            g.setColour (ink (0.2f));
            g.fillPath (seams, T);
            g.setColour (chalk (0.14f));
            g.fillPath (seams, nudged (0.0f, 0.022f, T));
            g.setColour (lit (juce::Colour (f.line).darker (0.4f), n));
            g.fillPath (s.detail, T);
            break;
        }
        case Planks:
        {
            g.setColour (ink (0.13f));
            g.fillPath (s.toneDark, T);
            g.setColour (chalk (0.07f));
            g.fillPath (s.toneLight, T);
            g.setColour (chalk (0.1f));
            g.strokePath (s.joints, juce::PathStrokeType (0.7f * px), nudged (0.0f, -0.014f, T));
            g.setColour (lit (juce::Colour (f.line), n, 0.8f));
            g.strokePath (s.joints, juce::PathStrokeType (1.2f * px), T);
            g.setColour (juce::Colour (f.line).darker (0.4f).withAlpha (0.85f));
            g.fillPath (s.detail, T);

            juce::Path soot;   // charred from the ground up
            soot.addRectangle (0.0f, 0.0f, uLen, 1.3f);
            g.setGradientFill (juce::ColourGradient (ink (0.3f), P2 (0.0f, 0.0f).transformedBy (T), ink (0.0f), P2 (0.0f, 1.3f).transformedBy (T), false));
            g.fillPath (soot, T);
            break;
        }
        case Plates:
        {
            g.setColour (ink (0.1f));
            g.fillPath (s.toneDark, T);
            g.setColour (chalk (0.06f));
            g.fillPath (s.toneLight, T);

            juce::Path rust;
            for (int k = 0; k < 7; ++k)
            {
                const float u = uLen * hash (w, k, 71), v = vLen * hash (w, k, 72), rad = 0.12f + 0.2f * hash (w, k, 73);
                rust.addEllipse (u - rad, v - rad * 1.6f, 2.0f * rad, 3.2f * rad);
            }
            g.setColour (juce::Colour (0xff7a4a2e).withAlpha (0.14f));
            g.fillPath (rust, T);

            g.setColour (chalk (0.16f));
            g.strokePath (s.joints, juce::PathStrokeType (0.8f * px), nudged (0.014f, -0.014f, T));
            g.setColour (lit (juce::Colour (f.line), n, 0.85f));
            g.strokePath (s.joints, juce::PathStrokeType (1.9f * px), T);
            g.setColour (lit (juce::Colour (f.wall).brighter (0.25f), n));
            g.fillPath (s.detail, T);
            g.setColour (ink (0.3f));
            g.fillPath (s.detail, nudged (0.008f, -0.008f, T));
            break;
        }
        case Tiles: case Blocks:
        {
            g.setColour (ink (0.05f));
            g.fillPath (s.toneDark, T);
            g.setColour (chalk (juce::Colour (f.wall).getPerceivedBrightness() > 0.85f ? 0.28f : 0.09f));
            g.fillPath (s.toneLight, T);
            const bool coarse = f.a > 0.3f;
            g.setColour (chalk (0.35f));
            g.strokePath (s.joints, juce::PathStrokeType (0.7f * px), nudged (0.007f, -0.007f, T));
            g.setColour (lit (juce::Colour (f.line), n, coarse ? 0.9f : 1.0f));
            g.strokePath (s.joints, juce::PathStrokeType ((coarse ? 1.4f : 0.9f) * px), T);
            break;
        }
        case Plain: case Glass: default: break;
    }

    if (! s.speckDark.isEmpty())
    {
        g.setColour (ink (0.055f));
        g.fillPath (s.speckDark, T);
        g.setColour (chalk (0.3f));
        g.fillPath (s.speckLight, T);
    }
}

void HouseView::drawOpenings (juce::Graphics& g, int w, const juce::AffineTransform& T, V3 n, P2 shift, bool sunlit)
{
    const auto& def = kWalls[w];
    const auto& f = kFinishes[material];
    const float d = damageOf (w);
    const float px = scale / 60.0f;

    // Frames sit back in the wall, so each opening shows its reveal on the side facing us.
    const float setBack = juce::jlimit (0.05f, 0.12f, f.thickness);
    const P2 back = toScreen (houseToWorld (def.o - def.n * setBack)) - toScreen (houseToWorld (def.o));
    const auto Tb = T.translated (back.x, back.y);
    const auto trim = lit (juce::Colour (kFrame), n);
    const auto shade = shadow (sunlit ? 0.26f : 0.12f);
    const juce::Colour sky (kBackdrops[outside].top);
    for (size_t k = 0; k < def.openings.size(); ++k)
    {
        const auto& o = def.openings[k];
        const juce::Rectangle<float> hole (o.u0, o.v0, o.u1 - o.u0, o.v1 - o.v0);
        const float wdt = hole.getWidth(), hgt = hole.getHeight();

        // casing, and the thin shadow it throws on the wall
        auto outer = hole.expanded (0.07f);
        if (o.door) outer.setTop (0.0f);
        juce::Path casing;
        casing.addRectangle (outer);
        g.setColour (shade);
        g.fillPath (sweep (outer, shift * 0.035f), T);
        g.setColour (trim);
        g.fillPath (casing, T);

        if (! o.door)
        {
            juce::Path sill, drip;
            const juce::Rectangle<float> ledge (o.u0 - 0.12f, o.v0 - 0.125f, wdt + 0.24f, 0.065f);
            sill.addRectangle (ledge);
            drip.addRectangle (ledge.withHeight (0.018f));
            g.setColour (shade);
            g.fillPath (sweep (ledge, shift * 0.085f), T);
            g.setColour (lit (juce::Colour (kFrame), { 0, 0, 1 }, 0.98f));
            g.fillPath (sill, T);
            g.setColour (ink (0.16f));
            g.fillPath (drip, T);
        }

        juce::Path opening;
        opening.addRectangle (hole);
        g.saveState();
        g.reduceClipRegion (opening, T);

        // reveals: the cut through the wall, each face lit by which way it points
        const P2 a0 = P2 (o.u0, o.v0).transformedBy (T), a1 = P2 (o.u1, o.v0).transformedBy (T),
                 b0 = P2 (o.u0, o.v1).transformedBy (T), b1 = P2 (o.u1, o.v1).transformedBy (T);
        g.setColour (lit (juce::Colour (kFrame), n, 0.8f));
        g.fillPath (opening, T);
        g.setColour (lit (juce::Colour (kFrame), houseDir (def.u), 0.88f));
        g.fillPath (quad (a0, b0, b0 + back, a0 + back));
        g.setColour (lit (juce::Colour (kFrame), houseDir (def.u * -1.0f), 0.88f));
        g.fillPath (quad (a1, b1, b1 + back, a1 + back));
        g.setColour (lit (juce::Colour (kFrame), { 0, 0, 1 }, 0.96f));
        g.fillPath (quad (a0, a1, a1 + back, a0 + back));

        if (o.door)
        {
            const bool gone = d >= 0.88f;
            g.saveState();
            g.reduceClipRegion (opening, Tb);
            if (d > 0.3f) drawInterior (g);
            else          g.fillAll (juce::Colour (kInterior));
            g.restoreState();

            if (! gone)
            {
                // The door swings in as the wall goes, then drops off its top hinge.
                const float open = smooth (0.3f, 0.8f, d);
                const float hang = -0.16f * smooth (0.6f, 0.86f, d);
                const auto M = juce::AffineTransform::rotation (hang, o.u0 + 0.02f, o.v1 - 0.02f).followedBy (Tb);
                const juce::Rectangle<float> leaf (o.u0 + 0.015f, o.v0 + 0.012f, wdt * (1.0f - 0.55f * open) - 0.03f, hgt - 0.027f);
                const auto wood = lit (juce::Colour (kWood), n, 1.0f - 0.28f * open);

                juce::Path panel, grooves, rails, plate, knob, glint, hinges;
                panel.addRectangle (leaf);
                for (int j = 1; j < 4; ++j)
                    grooves.addRectangle (leaf.getX() + leaf.getWidth() * (float) j / 4.0f - 0.006f, leaf.getY(), 0.012f, leaf.getHeight());
                rails.addRectangle (leaf.getX(), leaf.getY(), leaf.getWidth(), 0.1f);
                rails.addRectangle (leaf.getX(), leaf.getBottom() - 0.1f, leaf.getWidth(), 0.1f);
                rails.addRectangle (leaf.getX(), leaf.getY() + 0.74f, leaf.getWidth(), 0.08f);
                plate.addRoundedRectangle (leaf.getRight() - 0.125f, o.v0 + 0.68f, 0.07f, 0.2f, 0.02f);
                knob.addEllipse (leaf.getRight() - 0.122f, o.v0 + 0.75f, 0.064f, 0.064f);
                glint.addEllipse (leaf.getRight() - 0.112f, o.v0 + 0.785f, 0.022f, 0.022f);
                hinges.addRectangle (leaf.getX(), o.v0 + 0.22f, 0.035f, 0.1f);
                hinges.addRectangle (leaf.getX(), o.v1 - 0.34f, 0.035f, 0.1f);

                g.setColour (wood);
                g.fillPath (panel, M);
                g.setColour (wood.darker (0.1f));
                g.fillPath (rails, M);
                g.setColour (wood.darker (0.5f).withAlpha (0.6f));
                g.fillPath (grooves, M);
                g.setColour (chalk (0.14f));
                g.fillPath (grooves, nudged (0.012f, 0.0f, M));
                g.setColour (juce::Colour (0xff3a3531));
                g.fillPath (hinges, M);
                g.setColour (juce::Colour (0xff6e5632));
                g.fillPath (plate, M);
                g.setColour (lit (juce::Colour (0xffd2aa5e), n));
                g.fillPath (knob, M);
                g.setColour (chalk (0.7f));
                g.fillPath (glint, M);
                g.setColour (ink (0.3f));
                g.strokePath (panel, juce::PathStrokeType (0.7f), M);

                if (sunlit)
                {
                    juce::Path lip;
                    lip.addRectangle (o.u0, o.v1 - 0.08f, wdt, 0.08f);
                    g.setColour (shade);
                    g.fillPath (lip, Tb);
                }
            }
        }
        else
        {
            const bool glassGone = d >= 0.72f;
            if (glassGone)
            {
                g.saveState();
                g.reduceClipRegion (opening, Tb);
                drawInterior (g);
                g.restoreState();

                // a few shards left standing in the frame
                juce::Random r (17 + w * 5 + (int) k);
                juce::Path shards;
                for (int j = 0; j < 5; ++j)
                {
                    const bool vertical = j % 2 == 0;
                    const float t = 0.15f + 0.7f * r.nextFloat();
                    const P2 a = vertical ? P2 (j < 2 ? o.u0 : o.u1, o.v0 + t * hgt) : P2 (o.u0 + t * wdt, j < 2 ? o.v0 : o.v1);
                    const P2 inwards = hole.getCentre() - a;
                    const P2 along = P2 (inwards.y, -inwards.x) * 0.22f;
                    shards.addTriangle (a + along, a - along, a + inwards * (0.3f + 0.35f * r.nextFloat()));
                }
                g.setColour (mix (sky, juce::Colours::white, 0.5f).withAlpha (0.75f));
                g.fillPath (shards, Tb);
                g.setColour (chalk (0.7f));
                g.strokePath (shards, juce::PathStrokeType (0.6f * px), Tb);
            }
            else
            {
                // glass: the sky on it, darker towards the bottom corner, two streaks of glare
                g.setColour (juce::Colour (kInterior));
                g.fillPath (opening, Tb);
                g.setGradientFill (juce::ColourGradient (mix (sky, juce::Colours::white, 0.5f).withAlpha (0.96f), P2 (o.u0, o.v1).transformedBy (Tb),
                                                         mix (sky, juce::Colour (0xff2f4f63), 0.62f).withAlpha (0.93f), P2 (o.u1, o.v0).transformedBy (Tb), false));
                g.fillPath (opening, Tb);

                g.saveState();
                g.reduceClipRegion (opening, Tb);
                juce::Path glare;
                glare.addQuadrilateral (o.u0 + 0.1f * wdt, o.v0, o.u0 + 0.3f * wdt, o.v0, o.u0 + 0.3f * wdt + 0.55f * hgt, o.v1, o.u0 + 0.1f * wdt + 0.55f * hgt, o.v1);
                glare.addQuadrilateral (o.u0 + 0.42f * wdt, o.v0, o.u0 + 0.49f * wdt, o.v0, o.u0 + 0.49f * wdt + 0.55f * hgt, o.v1, o.u0 + 0.42f * wdt + 0.55f * hgt, o.v1);
                g.setColour (chalk (0.22f));
                g.fillPath (glare, Tb);

                if (d > 0.4f)
                {
                    // cracked pane: rays from where it was hit, tied together by a broken ring
                    juce::Random r (31 + w + 3 * (int) k);
                    const P2 hit (o.u0 + wdt * (0.25f + 0.5f * r.nextFloat()), o.v0 + hgt * (0.25f + 0.5f * r.nextFloat()));
                    juce::Path star;
                    P2 ringPts[6];
                    for (int j = 0; j < 6; ++j)
                    {
                        const float a = kTwoPi * ((float) j + r.nextFloat() * 0.6f) / 6.0f;
                        const P2 dir (std::cos (a), std::sin (a));
                        star.startNewSubPath (hit);
                        star.lineTo (hit + dir * (0.25f + 0.5f * r.nextFloat()));
                        ringPts[j] = hit + dir * (0.07f + 0.06f * r.nextFloat());
                    }
                    for (int j = 0; j < 6; ++j)
                    {
                        star.startNewSubPath (ringPts[j]);
                        star.lineTo (ringPts[(j + 1) % 6]);
                    }
                    g.setColour (chalk (0.7f));
                    g.strokePath (star, juce::PathStrokeType (0.6f * px), Tb);
                }
                g.restoreState();
            }

            // frame and glazing bars, set back in the reveal
            if (d < 0.92f)
            {
                juce::Path frame;
                const float fw = 0.045f;
                frame.addRectangle (o.u0, o.v0, wdt, fw);
                frame.addRectangle (o.u0, o.v1 - fw, wdt, fw);
                frame.addRectangle (o.u0, o.v0, fw, hgt);
                frame.addRectangle (o.u1 - fw, o.v0, fw, hgt);
                if (d < 0.8f)
                {
                    frame.addRectangle (hole.getCentreX() - 0.02f, o.v0, 0.04f, hgt);
                    frame.addRectangle (o.u0, hole.getCentreY() - 0.02f, wdt, 0.04f);
                }
                g.setColour (ink (0.22f));
                g.fillPath (frame, nudged (0.012f, -0.012f, Tb));
                g.setColour (trim);
                g.fillPath (frame, Tb);
            }
            if (sunlit && ! glassGone)
            {
                juce::Path lip;
                lip.addRectangle (o.u0, o.v1 - 0.06f, wdt, 0.06f);
                g.setColour (shade);
                g.fillPath (lip, Tb);
            }
        }
        g.restoreState();

        g.setColour (ink (0.18f));
        g.strokePath (casing, juce::PathStrokeType (0.6f), T);
    }

    // louvred vent in the gable
    if (def.gable)
    {
        const P2 c (0.5f * def.uLen, kH + kR * 0.42f);
        juce::Path ring, inner, slats;
        ring.addEllipse (c.x - 0.17f, c.y - 0.17f, 0.34f, 0.34f);
        inner.addEllipse (c.x - 0.12f, c.y - 0.12f, 0.24f, 0.24f);
        for (float y = c.y - 0.1f; y < c.y + 0.12f; y += 0.06f)
            slats.addRectangle (c.x - 0.13f, y, 0.26f, 0.03f);
        g.setColour (shade);
        g.fillPath (ring, nudged (shift.x * 0.018f, shift.y * 0.018f, T));
        g.fillPath (ring, nudged (shift.x * 0.035f, shift.y * 0.035f, T));
        g.setColour (trim);
        g.fillPath (ring, T);
        g.setColour (juce::Colour (kInterior).brighter (0.15f));
        g.fillPath (inner, T);
        g.saveState();
        g.reduceClipRegion (inner, T);
        g.setColour (trim.darker (0.1f));
        g.fillPath (slats, T);
        g.restoreState();
        g.setColour (ink (0.18f));
        g.strokePath (ring, juce::PathStrokeType (0.6f), T);
    }
}

void HouseView::fillUnder (juce::Graphics& g, int w, const juce::AffineTransform& T, V3 n)
{
    // Whatever is behind the finish, drawn across the whole wall; the caller has clipped to the gaps.
    const auto& f = kFinishes[material];
    const auto& s = skins[(size_t) w];
    const auto outline = wallOutline (kWalls[w]);
    const juce::Colour under (f.under);

    switch (f.underKind)
    {
        case Brick:
            g.setColour (lit (juce::Colour (0xffcbbfb0), n, 0.92f));   // mortar
            g.fillPath (outline, T);
            g.setColour (lit (under, n));
            g.fillPath (s.underA, T);
            g.setColour (lit (under.darker (0.22f), n));
            g.fillPath (s.underB, T);
            g.setColour (lit (under.brighter (0.25f), n));
            g.fillPath (s.underC, T);
            break;
        case Rubble:
            g.setColour (lit (under.darker (0.4f), n));
            g.fillPath (outline, T);
            g.setColour (lit (under, n));
            g.fillPath (s.underA, T);
            g.setColour (lit (under.darker (0.14f), n));
            g.fillPath (s.underB, T);
            g.setColour (lit (under.brighter (0.16f), n));
            g.fillPath (s.underC, T);
            break;
        case Concrete:
            g.setColour (lit (under, n));
            g.fillPath (outline, T);
            g.setColour (ink (0.17f));
            g.fillPath (s.underA, T);
            g.setColour (chalk (0.17f));
            g.fillPath (s.underB, T);
            break;
        case Studs:
            g.setColour (lit (under, n, 0.75f));
            g.fillPath (outline, T);
            g.setColour (lit (juce::Colour (0xff6f5039), n));
            g.fillPath (s.underA, T);
            g.setColour (lit (juce::Colour (0xff5d4330), n));
            g.fillPath (s.underB, T);
            break;
        case Nothing: default:
            g.setColour (lit (juce::Colour (f.wall).darker (0.35f), n));
            g.fillPath (outline, T);
            break;
    }
}

void HouseView::drawWallDamage (juce::Graphics& g, int w, const juce::AffineTransform& T, V3 n, P2 shift)
{
    const float d = damageOf (w);
    if (d <= 0.01f)
        return;

    const auto& def = kWalls[w];
    const auto& f = kFinishes[material];
    const auto& surf = walls[(size_t) w];
    const auto& s = skins[(size_t) w];
    const float uLen = def.uLen, vLen = def.height(), px = scale / 60.0f;
    const bool glass = f.pattern == Glass;
    const bool darkWall = juce::Colour (f.wall).getPerceivedBrightness() < 0.45f;
    const juce::Colour sky (kBackdrops[outside].top);

    // unit step away from the light, in the wall's own plane
    const float shiftLen = std::max (0.001f, std::hypot (shift.x, shift.y));
    const P2 lee = shift / shiftLen;

    //---- cracks
    {
        juce::Path fill, hair;
        for (const auto& c : surf.cracks)
            crackOutline (c, d, 1.0f, fill, hair);
        if (! hair.isEmpty())
        {
            if (glass)
            {
                g.setColour (chalk (0.8f));
                g.fillPath (fill, T);
            }
            else
            {
                // a light lip on the side away from the light makes the crack read as cut into the wall
                g.setColour (chalk (darkWall ? 0.13f : 0.55f));
                g.fillPath (fill, nudged (lee.x * 0.012f, lee.y * 0.012f, T));
                g.setColour (juce::Colour (kCrackInk));
                g.fillPath (fill, T);
            }
        }
    }

    // Windows and doors keep their own damage: nothing spalls across glass.
    juce::Path solid = wallOutline (def);
    for (const auto& o : def.openings)
    {
        auto keep = juce::Rectangle<float> (o.u0, o.v0, o.u1 - o.u0, o.v1 - o.v0).expanded (0.07f);
        if (o.door) keep.setTop (0.0f);
        solid.addRectangle (keep);
    }
    solid.setUsingNonZeroWinding (false);

    //---- the finish comes away
    if (f.spall == SpallBlob)
    {
        juce::Path spalls;
        for (const auto& b : surf.spalls)
            if (b.th < d)
                addBlob (spalls, b, 0.5f + 0.5f * smooth (b.th, b.th + 0.22f, d));
        if (! spalls.isEmpty())
        {
            g.saveState();
            g.reduceClipRegion (solid, T);
            // Outlines first: the fill then covers everything but their outer edge, so patches
            // that overlap read as one.
            g.setColour (juce::Colour (kCrackInk).withAlpha (0.42f));
            g.strokePath (spalls, juce::PathStrokeType (1.5f * px), T);
            // A shallow recess. The whole patch is first filled with shade; the bed is then drawn only
            // where the same shape, moved away from the light, still covers it. What is left is the
            // shadow of the finish's edge, and overlapping patches merge without seams.
            g.reduceClipRegion (spalls, T);
            g.setColour (lit (f.underKind == Nothing ? juce::Colour (f.wall).darker (0.35f) : juce::Colour (f.under), n, 0.5f));
            g.fillPath (spalls, T);
            g.reduceClipRegion (spalls, nudged (lee.x * 0.032f, lee.y * 0.032f, T));
            fillUnder (g, w, T, n);
            g.restoreState();
        }
    }
    else if (f.spall == SpallCells && s.rows > 0)
    {
        // piece by piece: each tile, pane or board has its own threshold
        auto gone = [&] (int row, int col)
        {
            return row >= 0 && row < s.rows && col >= 0 && col < s.cols && s.spall[(size_t) (row * s.cols + col)] < d;
        };
        const float lipW = 0.028f;
        juce::Path fallen, edgeShade, stars;
        for (int row = 0; row < s.rows; ++row)
            for (int col = 0; col < s.cols; ++col)
            {
                juce::Rectangle<float> cell;
                if (! gone (row, col) || ! cellBounds (s, row, col, uLen, vLen, cell))
                    continue;
                if (f.hole == HoleCells && cellIsHole (w, row, col, d))
                    continue;

                if (glass)
                {
                    // a cracked pane: a star from where it was hit
                    const P2 hit (cell.getX() + cell.getWidth() * (0.35f + 0.3f * hash (w, row, col + 200)),
                                  cell.getY() + cell.getHeight() * (0.35f + 0.3f * hash (w, row, col + 300)));
                    const float reach = 0.42f * std::min (cell.getWidth(), cell.getHeight());
                    P2 ringPts[6];
                    for (int j = 0; j < 6; ++j)
                    {
                        const float a = kTwoPi * ((float) j + 0.7f * hash (row, col, j + 400)) / 6.0f;
                        const P2 dir (std::cos (a), std::sin (a));
                        stars.startNewSubPath (hit);
                        stars.lineTo (hit + dir * (reach * (0.6f + 0.4f * hash (row, col, j + 500))));
                        ringPts[j] = hit + dir * (reach * 0.32f);
                    }
                    for (int j = 0; j < 6; ++j)
                    {
                        stars.startNewSubPath (ringPts[j]);
                        stars.lineTo (ringPts[(j + 1) % 6]);
                    }
                    fallen.addRectangle (cell);
                    continue;
                }

                fallen.addRectangle (cell.expanded (0.004f));
                // whatever is still in place above, and on the side towards the light, shades the gap
                const float half = 0.5f * s.cw;
                const float x0 = (float) col * s.cw - (s.bond && row % 2 == 1 ? half : 0.0f);
                if (s.bond)
                {
                    const int left = row % 2 == 0 ? col : col - 1;
                    if (! gone (row + 1, left))
                        edgeShade.addRectangle (juce::Rectangle<float> (x0, cell.getBottom() - lipW, half, lipW).getIntersection (cell));
                    if (! gone (row + 1, left + 1))
                        edgeShade.addRectangle (juce::Rectangle<float> (x0 + half, cell.getBottom() - lipW, half, lipW).getIntersection (cell));
                }
                else if (! gone (row + 1, col))
                    edgeShade.addRectangle (cell.getX(), cell.getBottom() - lipW, cell.getWidth(), lipW);
                if (lee.x > 0.0f ? ! gone (row, col - 1) : ! gone (row, col + 1))
                    edgeShade.addRectangle (lee.x > 0.0f ? cell.getX() : cell.getRight() - lipW, cell.getY(), lipW, cell.getHeight());
            }

        if (glass)
        {
            g.setColour (chalk (0.1f));
            g.fillPath (fallen, T);
            g.setColour (chalk (0.8f));
            g.strokePath (stars, juce::PathStrokeType (0.8f * px), T);
        }
        else if (! fallen.isEmpty())
        {
            g.saveState();
            g.reduceClipRegion (fallen, T);
            fillUnder (g, w, T, n);
            g.setColour (shadow (0.42f));
            g.fillPath (edgeShade, T);
            g.restoreState();
        }
    }
    else
    {
        // thin skins scorch and rust (ice frosts) where a thicker wall would spall
        juce::Path outer, inner;
        for (const auto& b : surf.spalls)
            if (b.th < d)
            {
                const float grow = 0.5f + 0.5f * smooth (b.th, b.th + 0.22f, d);
                addBlob (outer, b, grow * 1.35f);
                addBlob (inner, b, grow * 0.8f);
            }
        if (! outer.isEmpty())
        {
            const bool ice = material == 8;
            const auto stain = ice ? juce::Colours::white : juce::Colour (f.under).darker (0.2f);
            g.saveState();
            g.reduceClipRegion (solid, T);
            g.setColour (stain.withAlpha (ice ? 0.3f : 0.28f));
            g.fillPath (outer, T);
            g.setColour (stain.withAlpha (ice ? 0.35f : 0.34f));
            g.fillPath (inner, T);
            g.restoreState();
        }
    }

    //---- holes right through, and the collapse
    const auto holes = wallHolePath (w, d);
    if (holes.isEmpty())
        return;

    const P2 back = toScreen (houseToWorld (def.o - def.n * f.thickness)) - toScreen (houseToWorld (def.o));
    const auto Tb = T.translated (back.x, back.y);
    const auto under = f.underKind == Nothing ? juce::Colour (f.wall).darker (0.3f)
                                              : (f.underKind == Studs ? juce::Colour (0xff6f5039) : juce::Colour (f.under));

    // torn sheet curls outwards around the breach
    if (f.hole == HoleBlob && f.thickness < 0.05f)
    {
        juce::Path flapsLit, flapsShade;
        for (const auto& b : surf.holes)
            if (b.th < d)
            {
                const float grow = 0.45f + 0.55f * smooth (b.th, b.th + 0.25f, d);
                const size_t count = b.shape.size();
                for (size_t k = 0; k + 1 < count; k += 2)
                {
                    const P2 p0 = b.c + b.shape[k] * (b.r * grow), p1 = b.c + b.shape[k + 1] * (b.r * grow);
                    const P2 mid = (p0 + p1) * 0.5f;
                    const P2 outDir = mid - b.c;
                    const float len = std::max (0.001f, std::hypot (outDir.x, outDir.y));
                    const P2 tip = mid + outDir * ((0.45f * p0.getDistanceFrom (p1) + 0.03f) / len);
                    (dot ({ outDir.x, outDir.y, 0 }, { -lee.x, -lee.y, 0 }) > 0.0f ? flapsLit : flapsShade).addTriangle (p0, p1, tip);
                }
            }
        g.setColour (lit (juce::Colour (f.wall), n, 1.14f));
        g.fillPath (flapsLit, T);
        g.setColour (lit (juce::Colour (f.wall), n, 0.74f));
        g.fillPath (flapsShade, T);
        g.setColour (ink (0.35f));
        g.strokePath (flapsLit, juce::PathStrokeType (0.6f * px), T);
        g.strokePath (flapsShade, juce::PathStrokeType (0.6f * px), T);
    }

    if (f.hole == HoleBlob)
    {
        g.setColour (ink (0.45f));   // drawn first: the hole covers all but the outer edge of this line
        g.strokePath (holes, juce::PathStrokeType (1.8f * px), T);
    }
    g.saveState();
    g.reduceClipRegion (holes, T);
    g.setColour (lit (under, n, 0.66f));   // the cut edge of the wall
    g.fillPath (holes, T);

    g.saveState();
    g.reduceClipRegion (holes, Tb);        // the same hole in the wall's back face: what you see through
    drawInterior (g);
    g.restoreState();

    // what was inside the wall still spans the gap
    const auto Tm = T.translated (back.x * 0.5f, back.y * 0.5f);
    if (f.underKind == Concrete)
    {
        juce::Path bars;
        for (int k = 0; 0.15f + (float) k * 0.3f < uLen; ++k)
            if (hash (w, k, 91) > 0.42f)
            {
                const float u = 0.15f + (float) k * 0.3f;
                bars.startNewSubPath (u, 0.0f);
                bars.lineTo (u + 0.08f * (hash (w, k, 93) - 0.5f), vLen);
            }
        for (int k = 0; 0.15f + (float) k * 0.3f < vLen; ++k)
            if (hash (w, k, 92) > 0.42f)
            {
                const float v = 0.15f + (float) k * 0.3f;
                bars.startNewSubPath (0.0f, v);
                bars.lineTo (uLen, v + 0.08f * (hash (w, k, 94) - 0.5f));
            }
        g.saveState();
        if (d > 0.82f)
        {
            // where the wall has come down, the bars end in bent stubs above the broken edge
            juce::Path standing;
            standing.startNewSubPath (0.0f, 0.0f);
            standing.lineTo (uLen, 0.0f);
            for (int k = 40; k >= 0; --k)
            {
                const float u = uLen * (float) k / 40.0f;
                standing.lineTo (u, collapseV (w, u, d) + 0.08f + 0.3f * hash (w, k, 95));
            }
            standing.closeSubPath();
            g.reduceClipRegion (standing, Tm);
        }
        g.setColour (juce::Colour (0xff4e2f22));
        g.strokePath (bars, juce::PathStrokeType (1.8f * px), Tm);
        g.setColour (juce::Colour (0xffb07345).withAlpha (0.65f));
        g.strokePath (bars, juce::PathStrokeType (0.6f * px), Tm.translated (-0.4f * px, -0.4f * px));
        g.restoreState();
    }
    else if (f.underKind == Studs)
    {
        g.setColour (lit (juce::Colour (0xff6f5039), n, 0.85f));
        g.fillPath (s.underA, Tm);
        g.fillPath (s.underB, Tm);
    }
    g.restoreState();

    if (glass && f.hole == HoleCells)
    {
        // shards still held in the corners of empty frames
        juce::Path shards;
        for (int row = 0; row < s.rows; ++row)
            for (int col = 0; col < s.cols; ++col)
            {
                juce::Rectangle<float> cell;
                if (! cellIsHole (w, row, col, d) || ! cellBounds (s, row, col, uLen, vLen, cell))
                    continue;
                if (row == 0 || ! cellIsHole (w, row - 1, col, d))
                    shards.addTriangle (cell.getX(), cell.getY(),
                                        cell.getX() + cell.getWidth() * (0.25f + 0.4f * hash (w, row, col + 600)), cell.getY(),
                                        cell.getX(), cell.getY() + cell.getHeight() * (0.2f + 0.4f * hash (w, row, col + 700)));
                if (row + 1 < s.rows && ! cellIsHole (w, row + 1, col, d))
                    shards.addTriangle (cell.getRight(), cell.getBottom(),
                                        cell.getRight() - cell.getWidth() * (0.2f + 0.4f * hash (w, row, col + 800)), cell.getBottom(),
                                        cell.getRight(), cell.getBottom() - cell.getHeight() * (0.25f + 0.4f * hash (w, row, col + 900)));
            }
        g.setColour (mix (sky, juce::Colours::white, 0.55f).withAlpha (0.72f));
        g.fillPath (shards, T);
        g.setColour (chalk (0.7f));
        g.strokePath (shards, juce::PathStrokeType (0.6f * px), T);
        // the frame itself survives the glass
        g.setColour (lit (juce::Colour (f.line), n));
        g.strokePath (s.joints, juce::PathStrokeType (2.2f * px), T);
    }
}

//==============================================================================
void HouseView::drawInterior (juce::Graphics& g)
{
    // Every opening looks into the same room, so it is painted once per frame and then shown
    // through whatever the caller has clipped to.
    const int wPx = juce::jmax (1, juce::roundToInt ((float) getWidth() * pixelScale)), hPx = juce::jmax (1, juce::roundToInt ((float) getHeight() * pixelScale));
    if (interiorCache.getWidth() != wPx || interiorCache.getHeight() != hPx)
    {
        interiorCache = juce::Image (juce::Image::RGB, wPx, hPx, false);
        interiorFresh = false;
    }
    if (! interiorFresh)
    {
        interiorFresh = true;
        juce::Graphics ig (interiorCache);
        ig.addTransform (juce::AffineTransform::scale (pixelScale));
        const P2 savedShake = shake;
        shake = {};
        paintInterior (ig);
        shake = savedShake;
    }
    g.drawImageTransformed (interiorCache, juce::AffineTransform::scale (1.0f / pixelScale).translated (shake.x, shake.y));
}

void HouseView::paintInterior (juce::Graphics& g)
{
    // Inside the house: dim, lit by the outside's colour, brighter once the roof opens.
    // Far walls show their inner faces; their windows and their own holes show daylight.
    const V3 cam = toCamera();
    const float roofOpen = damageOf (dsp::Ceiling);
    const float glow = 0.62f + 0.4f * smooth (0.3f, 0.9f, roofOpen);
    auto inside = [&] (juce::uint32 argb, float k = 1.0f)
    {
        const juce::Colour c (argb);
        return juce::Colour::fromFloatRGBA (juce::jmin (1.0f, c.getFloatRed() * light.amb[0] * glow * k),
                                            juce::jmin (1.0f, c.getFloatGreen() * light.amb[1] * glow * k),
                                            juce::jmin (1.0f, c.getFloatBlue() * light.amb[2] * glow * k), 1.0f);
    };
    const auto sky = skyGradient();
    const auto& f = kFinishes[material];

    g.fillAll (inside (0xff4a403a));

    // underside of whichever roof plane faces us from inside
    for (int side = 0; side < 2; ++side)
    {
        const bool east = side == 0;
        const float sg = east ? 1.0f : -1.0f;
        if (dot (houseDir ({ sg * std::sin (kPhi), 0.0f, std::cos (kPhi) }), cam) >= 0.0f)
            continue;   // we see that plane's tiles, not its underside
        float uLen = 0.0f, slopeLen = 0.0f;
        const auto R = roofTransform (east, uLen, slopeLen);
        juce::Path plane, rafters;
        plane.addRectangle (0.0f, 0.0f, uLen, slopeLen);
        for (float u = 0.2f; u < uLen; u += 0.45f)
            rafters.addRectangle (u, 0.0f, 0.07f, slopeLen);
        g.setColour (inside (0xff6b5140));
        g.fillPath (plane, R);
        g.setColour (inside (kRafter, 1.25f));
        g.fillPath (rafters, R);
        const auto gaps = roofHolePath (east, roofOpen);
        if (! gaps.isEmpty())
        {
            g.saveState();
            g.reduceClipRegion (plane, R);
            g.setGradientFill (sky);
            g.fillPath (gaps, R);
            g.setColour (inside (kRafter, 1.25f));
            g.fillPath (rafters, R);
            g.restoreState();
        }
    }

    // inner faces of the far walls
    for (int w = 0; w < 4; ++w)
    {
        const auto& def = kWalls[w];
        const V3 n = houseDir (def.n);
        if (dot (n, cam) >= 0.0f)
            continue;
        const auto T = planeTransform (def.o, def.u, { 0, 0, 1 }, true);
        const auto outline = wallOutline (def);
        const float facing = std::max (0.0f, dot (n * -1.0f, kLight));

        g.saveState();
        g.reduceClipRegion (outline, T);
        g.fillAll (inside (0xffd6ccba, 0.82f + 0.4f * facing));

        juce::Path skirting, rail;
        skirting.addRectangle (0.0f, 0.0f, def.uLen, 0.11f);
        rail.addRectangle (0.0f, kH - 0.03f, def.uLen, 0.035f);
        g.setColour (inside (0xfff2ede3, 0.9f));
        g.fillPath (skirting, T);
        g.setColour (inside (0xff8d8172));
        g.fillPath (rail, T);

        for (const auto& o : def.openings)
        {
            juce::Path daylight, bars;
            daylight.addRectangle (o.u0, o.v0, o.u1 - o.u0, o.v1 - o.v0);
            if (o.door)
            {
                g.setColour (inside (0xff8f6639));
                g.fillPath (daylight, T);
                continue;
            }
            g.setGradientFill (sky);
            g.fillPath (daylight, T);
            bars.addRectangle (0.5f * (o.u0 + o.u1) - 0.02f, o.v0, 0.04f, o.v1 - o.v0);
            bars.addRectangle (o.u0, 0.5f * (o.v0 + o.v1) - 0.02f, o.u1 - o.u0, 0.04f);
            g.setColour (inside (0xfff6f3ec));
            if (damageOf (w) < 0.8f)
                g.fillPath (bars, T);
            g.strokePath (daylight, juce::PathStrokeType (std::max (1.0f, 0.03f * scale)), T);
        }

        const auto gaps = wallHolePath (w, damageOf (w));
        if (! gaps.isEmpty())
        {
            g.setGradientFill (sky);
            g.fillPath (gaps, T);
        }
        g.restoreState();
    }

    // floor: boards, a rug, and rubble once things start coming in
    {
        const auto T = planeTransform ({ -0.5f * kW, -0.5f * kD, 0.005f }, { 1, 0, 0 }, { 0, 1, 0 }, true);
        juce::Path floor, boards, rug, rugInner, bits;
        floor.addRectangle (0.0f, 0.0f, kW, kD);
        for (float u = 0.18f; u < kW; u += 0.18f)
        {
            boards.startNewSubPath (u, 0.0f);
            boards.lineTo (u, kD);
        }
        rug.addRoundedRectangle (0.5f * kW - 0.8f, 0.5f * kD - 0.5f, 1.6f, 1.0f, 0.05f);
        rugInner.addRoundedRectangle (0.5f * kW - 0.68f, 0.5f * kD - 0.38f, 1.36f, 0.76f, 0.03f);

        g.setColour (inside (0xffa07a56, 1.1f));
        g.fillPath (floor, T);
        g.setColour (inside (0xff654a33));
        g.strokePath (boards, juce::PathStrokeType (std::max (0.8f, 0.012f * scale)), T);
        g.setColour (inside (0xff9c4b3e));
        g.fillPath (rug, T);
        g.setColour (inside (0xffd2ad82));
        g.strokePath (rugInner, juce::PathStrokeType (std::max (1.0f, 0.02f * scale)), T);

        const float md = meanDamage();
        for (int k = 0; k < 30; ++k)
            if (hash (k, 7) < md)
            {
                const float sz = 0.05f + 0.1f * hash (k, 8);
                bits.addRectangle (kW * hash (k, 9), kD * hash (k, 10), sz, sz * (0.5f + 0.5f * hash (k, 11)));
            }
        g.setColour (inside (f.debris, 1.05f));
        g.fillPath (bits, T);
    }
}

//==============================================================================
// Roof and chimney
//==============================================================================
void HouseView::drawRoof (juce::Graphics& g, bool east)
{
    const float sg = east ? 1.0f : -1.0f;
    const float xe = 0.5f * kW + kEave, y0 = -0.5f * kD - kGable, y1 = 0.5f * kD + kGable;
    const float zr = kH + kR, ze = zr - kSlope * xe;
    const V3 Rt0 { 0, y0, zr + kTz }, Rt1 { 0, y1, zr + kTz }, Et0 { sg * xe, y0, ze + kTz }, Et1 { sg * xe, y1, ze + kTz };
    const V3 Rb0 { 0, y0, zr }, Rb1 { 0, y1, zr }, Eb0 { sg * xe, y0, ze }, Eb1 { sg * xe, y1, ze };
    const V3 cam = toCamera();
    const float d = damageOf (dsp::Ceiling);
    const float px = scale / 60.0f;

    // the edges of the roof slab: barge boards on the gables, fascia along the eave
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
        g.setColour (lit (c, n));
        g.fillPath (p);
        g.setColour (ink (0.2f));
        g.strokePath (p, juce::PathStrokeType (0.8f));
    };
    const float sinP = std::sin (kPhi), cosP = std::cos (kPhi);
    face ({ Rb0, Eb0, Et0, Rt0 }, { 0, -1, 0 }, juce::Colour (kRoofEdge));
    face ({ Rb1, Eb1, Et1, Rt1 }, { 0, 1, 0 }, juce::Colour (kRoofEdge));
    face ({ Eb0, Eb1, Et1, Et0 }, { sg * cosP, 0, -sinP }, juce::Colour (kRoofEdge));

    const V3 nTop { sg * sinP, 0, cosP };
    const V3 n = houseDir (nTop);
    if (dot (n, cam) <= 0.0f)
        return;

    float uLen = 0.0f, slopeLen = 0.0f;
    const auto T = roofTransform (east, uLen, slopeLen);
    juce::Path top;
    top.addRectangle (0.0f, 0.0f, uLen, slopeLen);
    auto& hit = hitPaths[dsp::Ceiling];
    if (! hitVisible[dsp::Ceiling]) hit.clear();
    hit.addPath (top, T);
    hitVisible[dsp::Ceiling] = true;

    const auto tile = lit (juce::Colour (kRoofRed), n, 1.1f);
    g.saveState();
    g.reduceClipRegion (top, T);
    g.setColour (tile);
    g.fillPath (top, T);

    // Each course laps over the one below: a lit lower edge, and the shadow it drops.
    const auto& th = tileThreshold[east ? 0 : 1];
    const int rows = (int) std::ceil (slopeLen / kTileV), cols = (int) std::ceil (uLen / kTileU) + 1;
    juce::Path butts, lights, joints, toneDark, toneLight, bare, slipped;
    for (int i = 0; i < rows; ++i)
    {
        const float v = (float) i * kTileV;
        if (i > 0) butts.addRectangle (0.0f, v - 0.036f, uLen, 0.036f);
        lights.addRectangle (0.0f, v, uLen, 0.024f);
        const float off = i % 2 == 1 ? 0.5f * kTileU : 0.0f;
        for (int j = 0; j < cols; ++j)
        {
            const float u = (float) j * kTileU + off - 0.5f * kTileU;
            const juce::Rectangle<float> cell (u, v, kTileU, kTileV);
            joints.addRectangle (u - 0.007f, v, 0.014f, kTileV - 0.036f);
            const float t = th[(size_t) (i * cols + j)];
            if (t + 0.3f < d)
                continue;                                   // right through: see roofHolePath
            if (t < d)
            {
                bare.addRectangle (cell.expanded (0.004f));   // tile gone, battens showing
                continue;
            }
            if (t < d + 0.07f)
            {
                // about to go: slid down the slope and twisted
                const float k = 1.0f - (t - d) / 0.07f;
                bare.addRectangle (cell.expanded (0.004f));
                juce::Path one;
                one.addRectangle (cell.reduced (0.008f));
                one.applyTransform (juce::AffineTransform::rotation ((hash (i, j, 5) - 0.5f) * 0.6f * k, cell.getCentreX(), cell.getCentreY())
                                        .translated (0.0f, -kTileV * 0.5f * k));
                slipped.addPath (one);
                continue;
            }
            const float tone = hash (i, j, east ? 1 : 2);
            if (tone < 0.2f)       toneDark.addRectangle (cell);
            else if (tone > 0.84f) toneLight.addRectangle (cell);
        }
    }
    g.setColour (ink (0.075f));
    g.fillPath (toneDark, T);
    g.setColour (chalk (0.1f));
    g.fillPath (toneLight, T);
    g.setColour (lit (juce::Colour (kRoofEdge), n).withAlpha (0.55f));
    g.fillPath (joints, T);
    g.setColour (ink (0.22f));
    g.fillPath (butts, T);
    g.setColour (chalk (0.15f));
    g.fillPath (lights, T);

    // darker towards the eave, as a pitched roof reads
    g.setGradientFill (juce::ColourGradient (ink (0.08f), P2 (0.0f, 0.0f).transformedBy (T), ink (0.0f), P2 (0.0f, slopeLen * 0.8f).transformedBy (T), false));
    g.fillPath (top, T);

    // the chimney's shadow lies down the east slope
    const float chimTop = chimneyTop (d);
    if (east && d < 0.9f)
    {
        const float lnTop = dot (kLight, nTop);
        std::vector<P2> pts;
        for (float x : { kChimX - kChimA, kChimX + kChimA })
            for (float y : { kChimY - kChimA, kChimY + kChimA })
            {
                const V3 foot { x, y, roofTopZ (x) }, head { x, y, chimTop };
                pts.push_back (toScreen (houseToWorld (foot)));
                pts.push_back (toScreen (houseToWorld (head - kLight * (dot (head - Rt0, nTop) / lnTop))));
            }
        g.setColour (shadow (0.26f));
        g.fillPath (convexHull (pts));
    }

    if (! bare.isEmpty())
    {
        juce::Path battens;
        for (float v = 0.07f; v < slopeLen; v += kTileV)
            battens.addRectangle (0.0f, v, uLen, 0.045f);
        g.saveState();
        g.reduceClipRegion (bare, T);
        g.setColour (lit (juce::Colour (kFelt), n));
        g.fillPath (top, T);
        g.setColour (lit (juce::Colour (kBatten), n));
        g.fillPath (battens, T);
        g.setColour (ink (0.3f));
        g.fillPath (butts, T);   // the course above overhangs the gap
        g.restoreState();

        g.setColour (ink (0.25f));
        g.fillPath (slipped, nudged (0.0f, -0.02f, T));
        g.setColour (tile);
        g.fillPath (slipped, T);
        g.setColour (ink (0.3f));
        g.strokePath (slipped, juce::PathStrokeType (0.7f * px), T);
    }

    const auto holes = roofHolePath (east, d);
    if (! holes.isEmpty())
    {
        const P2 back = toScreen (houseToWorld (Et0 - nTop * kRoofT)) - toScreen (houseToWorld (Et0));
        juce::Path rafters, laths;
        for (float u = 0.2f; u < uLen; u += 0.45f)
            rafters.addRectangle (u, 0.0f, 0.07f, slopeLen);
        for (float v = 0.07f; v < slopeLen; v += kTileV * 2.0f)
            laths.addRectangle (0.0f, v, uLen, 0.035f);

        g.saveState();
        g.reduceClipRegion (holes, T);
        g.setColour (lit (juce::Colour (kFelt), n, 1.2f));   // the cut edge of the roof
        g.fillPath (holes, T);
        g.saveState();
        g.reduceClipRegion (holes, T.translated (back.x, back.y));
        drawInterior (g);
        g.restoreState();
        // the frame of the roof still spans the gap
        g.setColour (lit (juce::Colour (kRafter), n, 0.9f));
        g.fillPath (rafters, T.translated (back.x, back.y));
        g.setColour (lit (juce::Colour (kBatten), n, 0.85f));
        g.fillPath (laths, T.translated (back.x * 0.4f, back.y * 0.4f));
        g.restoreState();
    }

    // ridge caps, some knocked off as the roof goes
    {
        juce::Path caps, capLines;
        const float loss = smooth (0.35f, 0.95f, d);
        for (int k = 0; (float) k * 0.3f < uLen; ++k)
            if (hash (k, 55) >= loss)
            {
                caps.addRectangle (juce::Rectangle<float> ((float) k * 0.3f, slopeLen - 0.1f, 0.3f, 0.1f).expanded (0.003f, 0.0f));
                capLines.addRectangle ((float) k * 0.3f, slopeLen - 0.1f, 0.012f, 0.1f);
            }
        g.setColour (ink (0.22f));
        g.fillPath (caps, nudged (0.0f, -0.03f, T));   // the caps sit proud of the tiles
        g.setColour (lit (juce::Colour (kRoofEdge).brighter (0.12f), n));
        g.fillPath (caps, T);
        g.setColour (ink (0.25f));
        g.fillPath (capLines, T);
    }
    g.restoreState();

    g.setColour (ink (0.25f));
    g.strokePath (top, juce::PathStrokeType (0.8f), T);

    // a line of light along the ridge
    juce::Path ridge;
    ridge.startNewSubPath (toScreen (houseToWorld (Rt0)));
    ridge.lineTo (toScreen (houseToWorld (Rt1)));
    g.setColour (chalk (0.3f * (1.0f - smooth (0.4f, 0.9f, d))));
    g.strokePath (ridge, juce::PathStrokeType (1.2f * px));
}

void HouseView::drawChimney (juce::Graphics& g)
{
    const float d = damageOf (dsp::Ceiling);
    const float tilt = 0.32f * smooth (0.45f, 0.85f, d);
    const float topZ = chimneyTop (d);
    const float x0 = kChimX - kChimA, x1 = kChimX + kChimA, y0 = kChimY - kChimA, y1 = kChimY + kChimA;
    const V3 pivot { x1, kChimY, roofTopZ (x1) };
    const float ct = std::cos (tilt), st = std::sin (tilt);
    auto tilted = [&] (V3 p)
    {
        const float rx = p.x - pivot.x, rz = p.z - pivot.z;
        return houseToWorld ({ pivot.x + rx * ct + rz * st, p.y, pivot.z - rx * st + rz * ct });
    };
    auto dirT = [&] (V3 nn) { return houseDir ({ nn.x * ct + nn.z * st, nn.y, -nn.x * st + nn.z * ct }); };

    const float zb0 = roofTopZ (x0) - 0.03f, zb1 = roofTopZ (x1) - 0.03f;
    struct Face { V3 a, b, c, e; V3 n; };
    const Face faces[] = {
        { { x1, y0, zb1 }, { x1, y1, zb1 }, { x1, y1, topZ }, { x1, y0, topZ }, { 1, 0, 0 } },
        { { x0, y1, zb0 }, { x0, y0, zb0 }, { x0, y0, topZ }, { x0, y1, topZ }, { -1, 0, 0 } },
        { { x0, y0, zb0 }, { x1, y0, zb1 }, { x1, y0, topZ }, { x0, y0, topZ }, { 0, -1, 0 } },
        { { x1, y1, zb1 }, { x0, y1, zb0 }, { x0, y1, topZ }, { x1, y1, topZ }, { 0, 1, 0 } },
    };
    const V3 cam = toCamera();
    const float uLen = 2.0f * kChimA;
    int faceIndex = 0;
    for (const auto& f : faces)
    {
        ++faceIndex;
        const V3 n = dirT (f.n);
        if (dot (n, cam) <= 0.0f) continue;
        const auto path = quad (toScreen (tilted (f.a)), toScreen (tilted (f.b)), toScreen (tilted (f.c)), toScreen (tilted (f.e)));
        g.setColour (lit (juce::Colour (0xffd9cdbf), n, 0.9f));   // mortar
        g.fillPath (path);

        // bricks in the face's own plane
        const float baseZ = std::min (zb0, zb1) - 0.05f;
        const V3 o = tilted ({ f.a.x, f.a.y, baseZ });
        const V3 uDir = normalised (tilted ({ f.b.x, f.b.y, baseZ }) - o);
        const V3 vDir = dirT ({ 0, 0, 1 });
        const auto s0 = toScreen (o);
        const auto su = toScreen (o + uDir) - s0, sv = toScreen (o + vDir) - s0;
        const juce::AffineTransform T (su.x, sv.x, s0.x, su.y, sv.y, s0.y);
        juce::Path bricksA, bricksB, missing;
        const float loss = smooth (0.3f, 0.9f, d);
        for (int row = 0; (float) row * 0.07f < topZ - baseZ; ++row)
            for (int col = 0; (float) col * 0.14f - 0.07f < uLen; ++col)
            {
                const float u = (float) col * 0.14f - (row % 2 == 1 ? 0.07f : 0.0f);
                const auto brick = juce::Rectangle<float> (u + 0.005f, (float) row * 0.07f + 0.005f, 0.13f, 0.06f).getIntersection ({ 0.0f, 0.0f, uLen, 9.0f });
                if (brick.isEmpty()) continue;
                const float t = hash (faceIndex, row, col);
                if (t < 0.22f * loss) missing.addRectangle (brick);
                else                  (t < 0.7f ? bricksA : bricksB).addRectangle (brick);
            }
        g.saveState();
        g.reduceClipRegion (path);
        g.setColour (lit (juce::Colour (kBrickRed), n));
        g.fillPath (bricksA, T);
        g.setColour (lit (juce::Colour (kBrickRed).darker (0.2f), n));
        g.fillPath (bricksB, T);
        g.setColour (juce::Colour (0xff3a2a24));
        g.fillPath (missing, T);
        g.restoreState();
        g.setColour (ink (0.22f));
        g.strokePath (path, juce::PathStrokeType (0.8f));
    }

    const P2 t0 = toScreen (tilted ({ x0, y0, topZ })), t1 = toScreen (tilted ({ x1, y0, topZ })),
             t2 = toScreen (tilted ({ x1, y1, topZ })), t3 = toScreen (tilted ({ x0, y1, topZ }));
    const float in = 0.06f;
    if (d < 0.4f)
    {
        // stone cap: a slab a little wider than the stack
        const float lip = 0.045f, capH = 0.07f;
        const float cx0 = x0 - lip, cx1 = x1 + lip, cy0 = y0 - lip, cy1 = y1 + lip, cz = topZ + capH;
        const Face cap[] = {
            { { cx1, cy0, topZ }, { cx1, cy1, topZ }, { cx1, cy1, cz }, { cx1, cy0, cz }, { 1, 0, 0 } },
            { { cx0, cy1, topZ }, { cx0, cy0, topZ }, { cx0, cy0, cz }, { cx0, cy1, cz }, { -1, 0, 0 } },
            { { cx0, cy0, topZ }, { cx1, cy0, topZ }, { cx1, cy0, cz }, { cx0, cy0, cz }, { 0, -1, 0 } },
            { { cx1, cy1, topZ }, { cx0, cy1, topZ }, { cx0, cy1, cz }, { cx1, cy1, cz }, { 0, 1, 0 } },
        };
        for (const auto& f : cap)
        {
            const V3 n = dirT (f.n);
            if (dot (n, cam) <= 0.0f) continue;
            g.setColour (lit (juce::Colour (kStone).darker (0.08f), n));
            g.fillPath (quad (toScreen (tilted (f.a)), toScreen (tilted (f.b)), toScreen (tilted (f.c)), toScreen (tilted (f.e))));
        }
        const auto capTop = quad (toScreen (tilted ({ cx0, cy0, cz })), toScreen (tilted ({ cx1, cy0, cz })),
                                  toScreen (tilted ({ cx1, cy1, cz })), toScreen (tilted ({ cx0, cy1, cz })));
        g.setColour (lit (juce::Colour (kStone), dirT ({ 0, 0, 1 })));
        g.fillPath (capTop);
        g.setColour (ink (0.2f));
        g.strokePath (capTop, juce::PathStrokeType (0.7f));
        g.setColour (juce::Colour (0xff2a1f1b));
        g.fillPath (quad (toScreen (tilted ({ x0 + in, y0 + in, cz })), toScreen (tilted ({ x1 - in, y0 + in, cz })),
                          toScreen (tilted ({ x1 - in, y1 - in, cz })), toScreen (tilted ({ x0 + in, y1 - in, cz }))));
    }
    else
    {
        // broken off: bare brick and the open flue
        g.setColour (lit (juce::Colour (kBrickRed).brighter (0.1f), dirT ({ 0, 0, 1 })));
        g.fillPath (quad (t0, t1, t2, t3));
        g.setColour (juce::Colour (0xff2a1f1b));
        g.fillPath (quad (toScreen (tilted ({ x0 + in, y0 + in, topZ })), toScreen (tilted ({ x1 - in, y0 + in, topZ })),
                          toScreen (tilted ({ x1 - in, y1 - in, topZ })), toScreen (tilted ({ x0 + in, y1 - in, topZ }))));
    }
}

//==============================================================================
// Things on the lawn
//==============================================================================
void HouseView::drawBushes (juce::Graphics& g, bool front)
{
    struct Lobe { float x, y, r; };
    static constexpr Lobe lobes[] = {
        { -1.45f, -0.95f, 0.26f }, { -1.15f, -1.02f, 0.2f }, { -1.32f, -0.8f, 0.17f },
        { 0.42f, -0.95f, 0.22f }, { 0.68f, -0.98f, 0.18f }, { 0.55f, -0.82f, 0.15f },
        { 1.85f, 1.55f, 0.28f }, { 2.0f, 1.28f, 0.2f }, { -1.95f, 1.65f, 0.22f }, { -1.75f, 1.8f, 0.16f },
    };
    struct Item { int index; float depth; };
    std::vector<Item> order;
    const float ca = std::cos (yaw), sa = std::sin (yaw);
    for (int i = 0; i < (int) std::size (lobes); ++i)
        if (inFront ({ lobes[i].x, lobes[i].y, 0 }) == front)
            order.push_back ({ i, -lobes[i].x * sa + lobes[i].y * ca });
    std::sort (order.begin(), order.end(), [] (const Item& a, const Item& b) { return a.depth > b.depth; });   // far first

    const V3 up { 0, 0, 1 };
    const P2 sun = lightOnScreen();
    const float dusty = 0.6f * smooth (0.3f, 0.95f, meanDamage());   // dust settles on the leaves
    const juce::Colour dust (0xff9a9479);
    const auto dark = lit (mix (juce::Colour (0xff3d772c), dust, dusty), up, 0.88f);
    const auto mid  = lit (mix (juce::Colour (0xff62a443), dust, dusty), up);
    const auto high = lit (mix (juce::Colour (0xffa6d877), dust, dusty), up);

    for (const auto& item : order)
    {
        const auto& b = lobes[item.index];
        const P2 c = toScreen ({ b.x, b.y, b.r * 0.8f });
        const float r = b.r * scale;

        juce::ColourGradient ball (high, c.x + sun.x * r * 0.45f, c.y + sun.y * r * 0.45f, dark, c.x + sun.x * r * 0.45f + r * 1.5f, c.y + sun.y * r * 0.45f, true);
        ball.addColour (0.5, mid);
        g.setGradientFill (ball);
        g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);

        // clumps of leaves: light ones on the sunny side, dark ones in the shade
        for (int k = 0; k < 7; ++k)
        {
            const float a = kTwoPi * hash (item.index, k, 1), rr = r * (0.2f + 0.55f * hash (item.index, k, 2));
            const P2 off (std::cos (a) * rr, std::sin (a) * rr);
            const bool sunny = off.x * sun.x + off.y * sun.y > -0.1f * r;
            g.setColour ((sunny ? high.brighter (0.15f) : dark.darker (0.15f)).withAlpha (0.42f));
            g.fillEllipse (c.x + off.x - r * 0.2f, c.y + off.y - r * 0.15f, r * 0.4f, r * 0.3f);
        }
        g.setColour (dark.darker (0.4f).withAlpha (0.4f));
        g.drawEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r, 0.8f);
    }
}

void HouseView::drawHeap (juce::Graphics& g, bool front)
{
    // Rubble that has built up: more of it, and bigger pieces, the further a surface has gone.
    const V3 cam = toCamera();
    const V3 up { 0, 0, 1 };
    const V3 side = normalised ({ cam.x, cam.y, 0.0f });
    const auto& f = kFinishes[material];
    const float ca = std::cos (yaw), sa = std::sin (yaw);

    struct Item { const HeapPiece* piece; float depth, grow; };
    std::vector<Item> items;
    for (const auto& h : heap)
    {
        const float d = damageOf (h.wall);
        if (h.th >= d) continue;
        const float facing = h.wall < 4 ? dot (houseDir (kWalls[h.wall].n), cam) : (inFront (h.p) ? 1.0f : -0.05f);
        if (facing < -0.12f)
            continue;   // piled against a wall on the far side: the house hides it
        if ((facing > 0.0f) == front)
            items.push_back ({ &h, -h.p.x * sa + h.p.y * ca, smooth (h.th, h.th + 0.05f, d) });
    }
    if (items.empty())
        return;
    std::sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.depth > b.depth; });

    // all the pieces' shadows in one pass, so the pile doesn't darken where they overlap
    {
        juce::Path shades;
        for (const auto& item : items)
        {
            const auto& h = *item.piece;
            if (h.p.z >= 0.02f) continue;
            const float size = h.size * (0.6f + 0.4f * item.grow);
            for (size_t k = 0; k < 5; ++k)
            {
                const P2 q = toScreen ({ h.p.x + h.shape[k].x * size - kLight.x * h.height / kLight.z,
                                         h.p.y + h.shape[k].y * size - kLight.y * h.height / kLight.z, 0.0f });
                if (k == 0) shades.startNewSubPath (q); else shades.lineTo (q);
            }
            shades.closeSubPath();
        }
        g.setColour (shadow (0.24f));
        g.fillPath (shades);
    }

    for (const auto& item : items)
    {
        const auto& h = *item.piece;
        juce::Colour base;
        if (h.wall == dsp::Ceiling)        base = juce::Colour (h.tone == 2 ? kBatten : kRoofRed);
        else if (h.tone == 1)              base = f.underKind == Nothing ? juce::Colour (f.debris).darker (0.2f)
                                                                         : juce::Colour (f.underKind == Studs ? 0xff6f5039 : f.under);
        else                               base = juce::Colour (f.debris).darker (h.tone == 2 ? 0.12f : 0.0f);

        const float size = h.size * (0.6f + 0.4f * item.grow);
        P2 head[5];
        std::vector<P2> all;
        all.reserve (10);
        for (size_t k = 0; k < 5; ++k)
        {
            const V3 p { h.p.x + h.shape[k].x * size, h.p.y + h.shape[k].y * size, h.p.z };
            head[k] = toScreen (p + V3 { 0, 0, h.height });
            all.push_back (toScreen (p));
            all.push_back (head[k]);
        }
        const auto crown = polygon (head, 5);
        g.setColour (lit (base, side, 0.82f).withAlpha (item.grow));
        g.fillPath (convexHull (all));
        g.setColour (lit (base, up).withAlpha (item.grow));
        g.fillPath (crown);
        g.setColour (ink (0.22f * item.grow));
        g.strokePath (crown, juce::PathStrokeType (0.6f));
    }
}

void HouseView::drawParticles (juce::Graphics& g, bool front)
{
    const V3 up { 0, 0, 1 };
    for (const auto& p : particles)
    {
        if (p.dust || inFront (p.p) != front) continue;
        const auto s = toScreen (p.p);
        const float sz = std::max (1.8f, p.size * scale);
        const float alpha = juce::jlimit (0.0f, 1.0f, p.alpha);
        const bool onLawn = std::abs (p.p.x) < kSlabX && std::abs (p.p.y) < kSlabY && p.p.z >= 0.0f;

        // its shadow on the grass, tighter and darker as it comes down
        if (onLawn)
        {
            const V3 foot { p.p.x - kLight.x * p.p.z / kLight.z, p.p.y - kLight.y * p.p.z / kLight.z, 0.0f };
            if (std::abs (foot.x) < kSlabX && std::abs (foot.y) < kSlabY)
            {
                const auto fs = toScreen (foot);
                const float spread = 1.0f + 0.5f * std::min (p.p.z, 1.5f);
                g.setColour (shadow (0.3f * alpha / (1.0f + 1.6f * p.p.z)));
                g.fillEllipse (fs.x - sz * 0.6f * spread, fs.y - sz * 0.3f * spread, sz * 1.2f * spread, sz * 0.6f * spread);
            }
        }

        // fast pieces leave a streak
        if (! p.settled && dot (p.v, p.v) > 5.0f)
        {
            const auto tail = toScreen (p.p - p.v * (0.03f / std::max (1.0f, std::sqrt (dot (p.v, p.v)) / 5.0f)));
            g.setColour (p.colour.withAlpha (0.28f * alpha));
            g.drawLine (tail.x, tail.y, s.x, s.y, sz * 0.5f);
        }

        // tumbling: the piece turns edge-on and catches the light as it flips
        const float flip = p.settled ? 0.55f : 0.4f + 0.6f * std::abs (std::cos (p.tumble));
        const auto M = juce::AffineTransform::scale (sz, sz * flip).rotated (p.angle).translated (s.x, s.y);
        P2 v[5];
        for (int k = 0; k < p.verts; ++k)
            v[k] = p.shape[(size_t) k].transformedBy (M);
        const float bright = p.settled ? 1.0f : 0.86f + 0.2f * std::cos (p.tumble * 2.0f);
        const bool shard = p.kind == Shard;
        g.setColour (lit (p.colour, up, bright).withAlpha (alpha * (shard ? 0.8f : 1.0f)));
        g.fillPath (polygon (v, p.verts));
        if (p.verts >= 4 && sz > 3.0f)
        {
            g.setColour (ink (0.17f * alpha));   // a second, darker facet
            g.fillPath (polygon (v, 3));
        }
        if (shard && sz > 2.5f)
        {
            g.setColour (chalk (0.75f * alpha));
            g.drawLine (v[0].x, v[0].y, v[2].x, v[2].y, 0.7f);
        }
    }
}

void HouseView::drawDust (juce::Graphics& g, bool front)
{
    for (const auto& p : particles)
    {
        if (! p.dust || inFront (p.p) != front) continue;
        const auto s = toScreen (p.p);
        const float r = p.size * scale;
        const float t = p.age / p.life;
        const float a = 0.55f * (t < 0.1f ? t / 0.1f : std::pow (std::max (0.0f, 1.0f - t), 1.5f));
        juce::ColourGradient puff (p.colour.withAlpha (a), s.x, s.y, p.colour.withAlpha (0.0f), s.x + r, s.y, true);
        puff.addColour (0.55, p.colour.withAlpha (a * 0.6f));
        g.setGradientFill (puff);
        g.fillEllipse (s.x - r, s.y - r, 2.0f * r, 2.0f * r);
    }
}

//==============================================================================
void HouseView::drawAtmosphere (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    // the header's shadow along the top of the view
    g.setGradientFill (juce::ColourGradient (ink (0.22f), 0.0f, 0.0f, ink (0.0f), 0.0f, 9.0f, false));
    g.fillRect (r.withHeight (9.0f));

    // the flash of a detonation
    if (blastAge < 0.14f)
    {
        g.setColour (chalk (0.24f * (1.0f - blastAge / 0.14f)));
        g.fillRect (r);
    }
}

void HouseView::drawOverlay (juce::Graphics& g)
{
    const auto text = light.dark ? juce::Colour (0xffe8e2d4) : juce::Colour (0xff23272b);

    if (hover >= 0 && hitVisible[(size_t) hover])
    {
        g.setColour (chalk (0.1f));
        g.fillPath (hitPaths[(size_t) hover]);
        g.setColour (text.withAlpha (0.75f));
        g.strokePath (hitPaths[(size_t) hover], juce::PathStrokeType (1.4f));

        static const char* names[] = { "North wall", "South wall (front door)", "East wall", "West wall", "Roof (ceiling)", "Lawn (floor)" };
        const float I = live.frame.integrity[(size_t) hover], s = live.frame.stressRatio[(size_t) hover];
        g.setColour (text);
        g.setFont (uiFont (15.0f, true));
        g.drawText (names[hover], 16, 12, 300, 20, juce::Justification::centredLeft);
        g.setFont (monoFont (13.0f));
        g.setColour (text.withAlpha (0.78f));
        g.drawText (juce::String (juce::roundToInt (I * 100.0f)) + "% standing    load " + juce::String (s, 2) + "x yield",
                    16, 32, 320, 18, juce::Justification::centredLeft);
    }

    if (live.frame.frozen)
    {
        g.setColour (text.withAlpha (0.8f));
        g.setFont (uiFont (13.0f, true));
        g.drawText ("Frozen", getWidth() - 116, 12, 100, 20, juce::Justification::centredRight);
    }

    // compass: which way north is after turning the house
    {
        const P2 c (28.0f, (float) getHeight() - 30.0f);
        const P2 north = toScreen ({ 0, 1, 0 }) - toScreen ({ 0, 0, 0 });
        const P2 nd = north / std::max (0.001f, std::hypot (north.x, north.y));
        const P2 across (-nd.y, nd.x);
        g.setColour (text.withAlpha (0.3f));
        g.drawEllipse (c.x - 12.0f, c.y - 12.0f, 24.0f, 24.0f, 1.0f);
        juce::Path needle;
        needle.addTriangle (c + nd * 9.0f, c + across * 3.5f - nd * 3.0f, c - across * 3.5f - nd * 3.0f);
        g.setColour (text.withAlpha (0.78f));
        g.fillPath (needle);
        g.setFont (uiFont (10.5f, true));
        g.drawText ("N", juce::Rectangle<float> (c.x + nd.x * 21.0f - 8.0f, c.y + nd.y * 21.0f - 7.0f, 16.0f, 14.0f), juce::Justification::centred);
    }

    if (isMouseOver() && ! dragging)
    {
        g.setColour (text.withAlpha (0.5f));
        g.setFont (uiFont (11.5f));
        g.drawText ("Drag to turn the house. Double-click to face it again.", 60, getHeight() - 26, getWidth() - 76, 16,
                    juce::Justification::centredRight);
    }
}
} // namespace rb::ui
