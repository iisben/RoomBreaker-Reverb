#pragma once
//==============================================================================
// The room as a small house on a patch of grass.
//
//   walls    North / South / East / West are the four walls (South has the door)
//   roof     the Ceiling: tiles slip, holes open, the chimney leans and falls
//   ground   the Floor: the lawn splits and the house settles off level
//
// Damage is drawn from integrity alone (cracks from window and door corners,
// render falling away to show what is underneath, holes, broken glass), so the
// picture always matches the sound. Fractures throw debris that lands and stays
// until the room heals. Drag to turn the house; double-click to face it again.
//
// Pure orthographic projection: every face is planar, so each one is drawn in its
// own (u, v) coordinates through an affine transform.
//==============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "LiveState.h"

namespace rb::ui
{
class HouseView : public juce::Component
{
public:
    HouseView (LiveState&, juce::AudioProcessorValueTreeState&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** UI timer (30 Hz): turns new fractures into debris, steps the particles and the camera. */
    void tick (float dtSeconds);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    int hoveredWall() const noexcept { return hover; }
    void setViewAngle (float radians) noexcept { yaw = radians; returning = false; yawVelocity = 0.0f; repaint(); }

    struct V3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

    struct Crack { juce::Point<float> a, b; float th; };
    struct Blob  { juce::Point<float> c; float r, th; std::array<float, 18> shape {}; };
    struct Surface
    {
        std::vector<Crack> cracks;
        std::vector<Blob>  spalls, holes;
    };

private:
    struct Particle
    {
        V3 p, v;
        float size = 0.05f, spin = 0.0f, angle = 0.0f, alpha = 1.0f;
        juce::Colour colour;
        bool settled = false, dust = false, fading = false;
        float age = 0.0f, life = 1.0f;
    };

    // camera
    juce::Point<float> toScreen (V3 world) const noexcept;
    V3 toCamera() const noexcept;
    V3 houseToWorld (V3 local) const noexcept;
    V3 houseDir (V3 localDir) const noexcept;
    juce::AffineTransform planeTransform (V3 o, V3 u, V3 v, bool houseSpace) const noexcept;
    bool frontOfHouse (V3 world) const noexcept;

    // drawing
    void drawBackdrop (juce::Graphics&);
    void drawShadow (juce::Graphics&);
    void drawGround (juce::Graphics&);
    void drawWall (juce::Graphics&, int index);
    void drawRoof (juce::Graphics&, bool east);
    void drawChimney (juce::Graphics&);
    void drawBushes (juce::Graphics&, bool front);
    void drawParticles (juce::Graphics&, bool front, bool dustPass);
    void drawSurfaceDamage (juce::Graphics&, const Surface&, float damage, const juce::AffineTransform&,
                            juce::Colour under, int underPattern, float uLen, float vLen);
    void drawInterior (juce::Graphics&);   // what you see through a hole: inner walls, floor, daylight
    void drawOverlay (juce::Graphics&);

    // debris
    void spawnFracture (int wall, float magnitude);
    void spawnDust (V3 at, int count, float spread);
    juce::Colour debrisColour (int wall) const;

    float damageOf (int wall) const noexcept;
    float tiltAngle() const noexcept;

    LiveState& live;
    std::atomic<float>* materialParam = nullptr;
    std::atomic<float>* voidParam = nullptr;

    float yaw, yawVelocity = 0.0f, dragYaw = 0.0f;
    bool dragging = false, returning = false;
    float scale = 60.0f;
    juce::Point<float> centre, shake;
    float shakeAmount = 0.0f;
    int hover = -1;
    float lastMeanIntegrity = 1.0f;
    float warmUp = 0.4f;   // seconds before fractures turn into debris

    std::array<Surface, 4> walls;        // N, S, E, W (dsp::Wall order)
    std::array<Surface, 2> roofPlanes;   // east, west
    Surface ground;
    std::array<std::vector<float>, 2> tileThreshold;

    std::vector<Particle> particles;
    mutable juce::Random rng { 0x5EED };

    // hit testing (rebuilt every paint)
    std::array<juce::Path, dsp::kNumWalls> hitPaths;
    std::array<bool, dsp::kNumWalls> hitVisible {};
};
} // namespace rb::ui
