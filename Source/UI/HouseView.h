#pragma once
//==============================================================================
// The room as a small house on a patch of grass.
//
//   walls    North / South / East / West are the four walls (South has the door)
//   roof     the Ceiling: tiles slip, holes open, the chimney leans and falls
//   ground   the Floor: the lawn splits and the house settles off level
//
// Damage is drawn from integrity alone, so the picture always matches the sound:
// cracks run from window and door corners, the finish comes away the way that
// material does (render spalls, tiles and panes drop one by one, stone leaves
// stepped edges, sheet metal tears), holes show the wall's thickness and the
// inside of the house, and past ~85 % damage a wall collapses from the top.
// Fractures throw debris that lands and stays until the room heals.
// Drag to turn the house; double-click to face it again.
//
// Pure orthographic projection: every face is planar, so each one is drawn in its
// own (u, v) coordinates through an affine transform.
//
//   HouseModel.h    measurements, materials, outsides (private to the two .cpp files)
//   HouseView.cpp   damage generation, debris, camera, mouse
//   HousePaint.cpp  drawing
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

    // For what is drawn around and over the plate (leader lines, the Size dimension, the north point).
    /** Where a surface is on screen, in this component's coordinates: the point a leader line lands on. */
    juce::Point<float> wallAnchor (int wall) const noexcept;
    /** False when that wall is on the far side of the house (its leader is drawn dashed, as hidden work is). */
    bool wallFacesViewer (int wall) const noexcept;
    /** Left and right screen extent of the lawn at the current view angle. */
    juce::Range<float> plotSpan() const noexcept;
    /** Unit vector on screen that points north. */
    juce::Point<float> northOnScreen() const noexcept;
    void setViewAngle (float radians) noexcept { yaw = radians; returning = false; yawVelocity = 0.0f; repaint(); }

    struct V3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
    using P2 = juce::Point<float>;

    /** A crack: `th[i]` is the damage at which it has grown as far as `pts[i]`. */
    struct CrackLine { std::vector<P2> pts; std::vector<float> th; float width = 0.03f; };
    /** A weak spot: an angular patch around `c` that appears at damage `th` and grows. */
    struct Blob { P2 c; float r = 0.1f, th = 1.0f; std::vector<P2> shape; };
    struct Surface
    {
        std::vector<CrackLine> cracks;
        std::vector<Blob> spalls, holes;
        std::array<float, 25> ragged {};   // the broken top edge once the wall collapses
    };

    /** Per-wall drawing data for the current material, rebuilt when the material changes. */
    struct Skin
    {
        float cw = 0.0f, ch = 0.0f;
        int cols = 0, rows = 0;
        bool bond = false;
        std::vector<float> spall, hole;        // damage thresholds per cell
        juce::Path joints, toneDark, toneLight, detail, speckDark, speckLight;
        juce::Path underA, underB, underC;     // what shows where the finish has gone
    };

private:
    struct Particle
    {
        V3 p, v;
        float size = 0.05f, spin = 0.0f, angle = 0.0f, tumble = 0.0f, tumbleRate = 0.0f, alpha = 1.0f;
        float age = 0.0f, life = 1.0f;
        juce::Colour colour;
        int kind = 0, verts = 4;
        std::array<P2, 5> shape {};
        bool settled = false, dust = false, fading = false, low = false;
    };
    struct HeapPiece
    {
        V3 p;                       // world, on the lawn
        float size = 0.1f, height = 0.05f, th = 1.0f;
        int wall = 0, tone = 0;
        std::array<P2, 5> shape {};
    };
    struct Tuft { float x, y, h, lean; };
    struct SceneLight { float amb[3] { 0.7f, 0.7f, 0.7f }, sun[3] { 0.4f, 0.4f, 0.4f }; bool dark = false; };

    // camera
    P2 toScreen (V3 world) const noexcept;
    V3 toCamera() const noexcept;
    V3 houseToWorld (V3 local) const noexcept;
    V3 houseDir (V3 localDir) const noexcept;
    juce::AffineTransform planeTransform (V3 o, V3 u, V3 v, bool houseSpace) const noexcept;
    juce::AffineTransform roofTransform (bool east, float& uLen, float& slopeLen) const noexcept;
    bool inFront (V3 world) const noexcept;     // not hidden behind the house body
    P2 lightOnScreen() const noexcept;

    // frame set-up
    void prepareFrame();
    void rebuildSkins (int material);
    juce::Colour lit (juce::Colour base, V3 normal, float gain = 1.0f) const noexcept;
    juce::ColourGradient skyGradient() const;

    // damage shapes for the current damage
    float collapseV (int wall, float u, float damage) const noexcept;
    bool cellBounds (const Skin&, int row, int col, float uLen, float vLen, juce::Rectangle<float>&) const noexcept;
    bool cellIsHole (int wall, int row, int col, float damage) const noexcept;
    juce::Path wallHolePath (int wall, float damage) const;
    juce::Path roofHolePath (bool east, float damage) const;

    // drawing
    void drawBackdrop (juce::Graphics&);
    void paintBackdrop (juce::Graphics&);      // rendered once into backdropCache
    void drawSlabShadow (juce::Graphics&);
    void drawGround (juce::Graphics&);
    void paintGroundBase (juce::Graphics&);    // rendered once per view into groundCache
    void drawGroundSide (juce::Graphics&, V3 a, V3 b, V3 normal, int index);
    void drawWall (juce::Graphics&, int index);
    void drawFinish (juce::Graphics&, int index, const juce::AffineTransform&, V3 normal);
    void drawOpenings (juce::Graphics&, int index, const juce::AffineTransform&, V3 normal, P2 shadowShift, bool sunlit);
    void drawWallDamage (juce::Graphics&, int index, const juce::AffineTransform&, V3 normal, P2 shadowShift);
    void fillUnder (juce::Graphics&, int index, const juce::AffineTransform&, V3 normal);
    void drawInterior (juce::Graphics&);   // what you see through a hole: inner walls, floor, daylight
    void paintInterior (juce::Graphics&);  // rendered once per frame, then shown through every opening
    void drawRoof (juce::Graphics&, bool east);
    void drawChimney (juce::Graphics&);
    void drawBushes (juce::Graphics&, bool front);
    void drawHeap (juce::Graphics&, bool front);
    void drawParticles (juce::Graphics&, bool front);
    void drawDust (juce::Graphics&, bool front);
    void drawAtmosphere (juce::Graphics&);
    void drawOverlay (juce::Graphics&);

    // debris
    void spawnFracture (int wall, float magnitude);
    void spawnDust (V3 at, V3 drift, int count, float spread, bool low);
    V3 breakPoint (int wall, V3& outward);
    juce::Colour debrisColour (int wall) const;

    float damageOf (int wall) const noexcept;
    float meanDamage() const noexcept;
    float tiltAngle() const noexcept;

    LiveState& live;
    std::atomic<float>* materialParam = nullptr;
    std::atomic<float>* voidParam = nullptr;

    float yaw, yawVelocity = 0.0f, dragYaw = 0.0f;
    bool dragging = false, returning = false;
    float scale = 60.0f;
    P2 centre, shake;
    float shakeAmount = 0.0f;
    int hover = -1;
    float lastMeanIntegrity = 1.0f;
    float warmUp = 0.4f;          // seconds before fractures turn into debris
    float blastAge = 10.0f;       // seconds since the last detonation-sized hit (drives the ground ring)

    // this frame
    int material = 0, outside = 0;
    SceneLight light;

    std::array<Surface, 4> walls;        // N, S, E, W (dsp::Wall order)
    Surface ground;
    std::array<std::vector<float>, 2> tileThreshold;   // east, west
    std::array<Skin, 4> skins;
    int skinMaterial = -1;

    std::vector<HeapPiece> heap;
    std::vector<Tuft> tufts;
    juce::Path flowersA, flowersB;
    std::vector<Particle> particles;
    mutable juce::Random rng { 0x5EED };

    // Layers that only change when the view or the scene does, kept as images.
    juce::Image slabShadow, backdropCache, groundCache, interiorCache;
    bool interiorFresh = false;
    float slabShadowYaw = -100.0f, slabShadowScale = 0.0f, slabShadowTilt = -1.0f;
    juce::uint64 backdropKey = 0, groundKey = 0, paintedState = 0;
    float pixelScale = 1.0f;     // device pixels per point, so cached layers stay sharp
    juce::uint64 stateSignature() const noexcept;

    // hit testing (rebuilt every paint)
    std::array<juce::Path, dsp::kNumWalls> hitPaths;
    std::array<bool, dsp::kNumWalls> hitVisible {};
};
} // namespace rb::ui
