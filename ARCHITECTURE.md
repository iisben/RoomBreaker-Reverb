# RoomBreaker — Dynamic Structural Destruction Simulator

A reverb whose six boundaries (North, South, East, West, Ceiling, Floor) crack, loosen and
breach under the mechanical stress of the incoming audio, then heal (or don't).

## 1. Project layout

```
CMakeLists.txt                 JUCE 8 via FetchContent; RB_CORE_ONLY=ON builds core + tests only
Source/
  Parameters.{h,cpp}           APVTS layout + wait-free ParameterCache::snapshot()
  Presets.{h,cpp}              48 factory presets with tags (host programs + the browser)
  PresetLibrary.{h,cpp}        Factory + user presets: search, tags, save, modified state (see 5b)
  PluginProcessor.{h,cpp}      AudioProcessor: buses (+ sidechain), oversampling, gains, limiter, bypass, meters, state
  PluginEditor.{h,cpp}         The console: header, rails, display, controls, footer; scaling (see 5d)
  UI/Theme.h                   Design tokens: colours, type, icons; the LookAndFeel declaration
  UI/Widgets.{h,cpp}           Knob, ToggleBox, ActionButton, Segmented, ChoiceBox, IconButton, StereoMeter; LookAndFeel
  UI/PresetBrowser.{h,cpp}     Search + category + tag browser, save form
  UI/Help.h                    Info-line text for every control and display
Resources/fonts/               Barlow Semi Condensed, JetBrains Mono (SIL OFL, licences alongside), embedded
  UI/LiveState.h               UI-side copy of the telemetry: frame, flashes, fractures, history
  UI/HouseView.{h,cpp}         The room as a house: damage generation, debris, camera, mouse (see 5e)
  UI/HousePaint.cpp            Everything the house view draws
  UI/HouseModel.h              The house's measurements, materials and outsides (private to the two above)
  DSP/                         JUCE-free, real-time-safe core (compiled and tested here)
    DspUtils.h                 TPT one-pole / SVF, Catmull-Rom delay, allpass, xorshift RNG, fastSin / fastTan
    Materials.h                11 material profiles (3 core + 8 dark cinematic) + impact voicing and blurbs
    Voids.h                    7 exterior void profiles (what lies beyond a breach)
    WallState.h                Wall enum, damage-phase curves, FractureEvent
    StressTracker.{h,cpp}      Kinetic Stress Analyzer + integrity model
    EarlyReflectionEngine.*    24-tap shoebox image-source ER with jitter + scatter
    DynamicFDN.{h,cpp}         16-line Hadamard FDN with bleed + morphing loop filters, ExteriorTank
    ModalDebrisGenerator.*     32-voice complex-resonator debris, chatter, contact rattle
    ImpactGenerator.{h,cpp}    Fracture physics: warning crackle, crack cascade, chirp, boom, falling
                               pieces + bounces, aftershocks, creak, rubble grains + rumble bed
    Telemetry.h                Triple buffer + atomics, audio → UI
    Engine.{h,cpp}             Owns everything; the processBlock body
Tests/OfflineRender.cpp        Core scenario harness: invariants, telemetry threading, WAV renders, real-time factor
Tests/PluginSmoke.cpp          Headless test of the built AudioProcessor + editor snapshot (no install needed)
```

The DSP core has no JUCE dependency. You can unit-test it, profile it, or reuse it in another host.

## 2. Signal flow

```
 host rate                                 engine rate (1x / 2x / 4x)
 ─────────                                 ──────────────────────────
 in ─► Input Gain ─┬─► [JUCE polyphase IIR up] ─► Engine (wet only) ─► [down] ─┐
                   │   sidechain ─► [linear interp] ─► stress source          │
                   └─────────────────────── dry × cos(mix·π/2) ───────────────┴─► + ─► Output Gain ─► Safety Limiter ─► out

 Engine (rb::dsp, JUCE-free):
            ┌──────── StressTracker (control rate, ~750 Hz at any engine rate) ────────┐
 stress src │ accumulate → finishAnalysis → updateStructure / detonate                 │
 (in / key) │   → integrity[6] (smoothed τ = 25 ms) + FractureEvents                   │
            └──────┬──────────────────────┬─────────────────────┬──────────────────────┘
                   │                      │ events              │ events, shock
 in ─► Pre-Delay ─► EarlyReflections   ModalDebris        ImpactGenerator
         │              │  └ ×0.35 ─┐      │ ×0.4             │ ×0.45
         └── ×0.5 ──────┼───────────┴──────┴──────────────────┴──► DynamicFDN (density, motion, shock, freeze)
                        │                                              └─ vented ─► ExteriorTank (void)
   wet = ER + tank + exterior + debris + impact
       ─► Drive ─► Low Cut / High Cut ─► Width (side HP 120 Hz) ─► Ducking ─► × sin(mix·π/2)
```

Debris and impact go into the tank as well as the wet bus: they happen inside the room, so they
reverberate. Pre-delay is applied to the room's input *and* to its stress source, so cracks and
booms land in time with the delayed reverb. The dry signal never touches the resampling filters.

## 3. Control grid and real-time rules

* **Fixed control grid that carries across host buffers.** `Engine::process` runs `controlTick()`
  every `controlBlockSize()` samples no matter how the host slices audio: 64 at 44.1/48 kHz, scaled
  with the rate (128 at 96 kHz, 256 at 192 kHz, cap 1024) so the control rate stays ~750 Hz and the
  oversampled engines behave like the 1x one (same script: mean integrity 0.306 / 0.305 / 0.304 at
  48 / 96 / 192 kHz). `renderSlice()` renders
  whatever part of a tick the host buffer covers. Each tick acts on the analysis window that just
  closed, so control latency is 1.33 ms at 48 kHz. Output matches across host block sizes up to
  float rounding: the test measured 64- vs 333-sample blocks, max difference 3.9e-7.
* All control values are given to the per-sample code as **target + per-sample increment** spanning
  one tick: delay lengths, tap gains, loop gains, matrix bleed and exterior input gain. Filter
  coefficients change once per tick. TPT one-pole and SVF topologies tolerate that without zipper
  noise or instability.
* There is no allocation, no locking and no system call on the audio thread. Buffers are sized in
  `prepare()` for the largest room (size 2.0, fully breached, plus creak headroom).
* **Click-free automation.** Every user control glides: Size (two smoothers in a row, 0.4 s in all, so the
  glide starts with no jump in speed: every delay line follows Size, and a kink there ticks), low/high cut
  (log glide, 50 ms), tone and wet/width/duck gains (per-sample linear ramps between ticks), drive
  (blend ramp, so 0 % is bit-exact bypass but switching it on never steps), density (allpass gain
  ramped per sample), Mix (equal-power gains taken once per tick and ramped, not a sin/cos per sample).
  Pre-delay changes do not move the read head: two whole-sample taps crossfade over 40 ms, so the
  pre-delay never interpolates and nothing is lost off the top end.
  Test 11 jumps each control mid-signal and compares the worst sample step to the clean signal's.
* **Impact soft knee.** The fracture impact is linear below 0.5 and `0.5 + 0.4·tanh((a−0.5)/0.4)`
  above, so stacked detonations stay under the limiter (worst pre-limiter preset peak 1.14, was 1.30).
* `juce::ScopedNoDenormals` in `processBlock`, because the FDN, exterior tank and resonators all
  decay toward denormals.

## 4. The engines

### 4.1 Kinetic Stress Analyzer — `StressTracker`

Per control tick:

```
LF       = 2nd-order Butterworth LP @120 Hz (TPT SVF), mid channel
lfRms    = sqrt(mean(LF²)) · √2              (a full-scale sub sine reads ≈ 1.0, same scale as peak)
LF_Acc  += (lfRms − LF_Acc)(1 − e^(−dt/1.5 s))      ← fatigue integrator
PeakEnv_w = max(blockPeak_w, PeakEnv_w · e^(−dt/120 ms))

Stress_w = load_w · ( α · PeakEnv_w  +  β · LF_Acc · lfCoupling_w )
α = 1 − bias,  β = bias
load_w   = (1 + 0.8·asym·offset_w) · (1 + 0.35·meanDamage(adjacent walls))
capacity_w = yield · material.strength · (0.35 + 0.65·I_w)          ← weakens as it fails
```

Directional loading assumes the listener faces North with the source in front:

| Wall | Transient load | LF coupling |
|---|---|---|
| North | mid peak | 0.8 |
| South | 0.7·mid + side peak | 0.8 |
| East / West | R / L peak | 0.7 |
| Ceiling | 0.85·max(L, R) | 0.5 |
| Floor | 0.6·mid | 1.0 |

Damage mechanisms:

1. **Brittle fracture.** Fires when a rising transient (block peak > 1.25 × previous envelope)
   exceeds `capacity · lerp(1.6, 0.9, brittleness)`. Integrity drops by
   `chunk · clamp(0.3 + excess, 0.3, 1.5) · rand(0.6–1.4)` at once. Then a 40 ms per-wall cooldown,
   and a brittle `FractureEvent` at the sample of the peak.
2. **Plastic yield / fatigue.** While `Stress/capacity > 1`, integrity creeps down at
   `fatigueRate · (ratio − 1) · ductileShare`. `ductileShare` is the LF part of the load, because
   transients already do their damage as fractures. Every 0.03 of creep emits a ductile "creak" event.
3. **Tectonic healing.** While `ratio < 0.7`: `I += (1 − I)(1 − e^(−dt/τheal))`. Turned off in permadeath.

Load redistribution plus shrinking capacity give the cascading collapse you'd expect: once a wall
goes, its neighbours follow faster.

### 4.2 Damage phases (`WallState.h`)

```
crack(I)  = smoothstep(1.0 → 0.4)                  jitter, scatter, extra HF loss
loose(I)  = smoothstep(0.75 → 0.4) · (1 − smoothstep(0.25 → 0.05))   slab modes, rattle
breach(I) = smoothstep(0.3 → 0.0)                  venting, loss, bleed, exterior send
```

### 4.3 Dynamic Early Reflections — `EarlyReflectionEngine`

Shoebox 14 × 10 × 5.5 m × Size. Taps: 6 first-order + 12 second-order on perpendicular walls
+ 6 second-order on opposite walls (both orders) = 24, all read from one delay line.

```
R_w    = reflectivity · (1 − breach_w)              breached walls stop reflecting
spec   = Π I_w^scatterExp                            share that stays specular
g_spec = geo · ΠR · spec
g_diff = geo · ΠR · √(1 − spec²)                     energy-preserving split → 4-stage allpass diffuser
delay  = imageDelay + jitterMax · dmg^1.5 · max(0, 1 + 3·n(t))
n(t)   = per-tap random walk, low-passed to ~5 Hz
```

Taps are panned from the image's azimuth with an equal-power law. Diffuser feedback rises with mean
damage, from 0.45 to 0.75.

* **Each bounce loses treble.** The source passes a first-order shelf (the material's damping corner,
  depth √hfShelfGain) on its way into the line, and the second-order images pass it once more, so
  later reflections are darker. Corner and depth ramp across a tick; a stepped corner ticks.
* **A tap that is not moving reads a whole sample**: no interpolation and no treble loss. Taps only
  use an interpolated read while a wall is cracked (jitter) or the room is resizing: Catmull-Rom at
  44.1/48 kHz, linear once the engine runs oversampled (under 0.5 dB at 10 kHz there).
* The four mixes (specular L/R, diffuse L/R) are dot products over gain arrays, summed as trees
  rather than one 24-long chain of additions, so they vectorise. Gains, jitter depths and the
  diffusers are only recomputed or run when the walls, the material or the size have moved.

### 4.4 Late Tank — `DynamicFDN`

* **16 lines** of 20.6–91.5 ms × Size × (1 + 0.12·breach), 784 ms in total: 0.78 modes per Hz,
  twice the density of the earlier 8-line tank. The lengths are spread over two octaves and chosen
  so that no pair sits near a simple ratio (2:1, 3:2 …), which would stack modes on the same
  frequencies. Loose lines wobble slowly ("creak"). Each line belongs to two walls: all 15 wall
  pairs, plus floor–ceiling twice.
* **Matrix with diagonal bleed:**
  `A(b) = (1−b)·H + b·I` with `H = H16/4`, applied as a fast Walsh–Hadamard transform: 64 additions,
  no multiplies. Every line feeds every other equally (|h| = 1/4), so echo density grows as
  16^passes. A Householder matrix at this size keeps 1 − 2/N of each line on itself: the lines act
  as weakly coupled combs and density builds slowly (measured: 0.53 at 200 ms, against 0.75 now,
  with the input diffusers off). H is orthogonal and symmetric with eigenvalues ±1, so A(b) has
  eigenvalues 1 and 2b − 1 and `|λ| ≤ 1` for every b. As walls breach, the lines decouple into combs,
  but the matrix never adds energy. `b = clamp(0.9·meanBreach + 0.3·meanLoose, 0, 0.95)`.
  Per-row bleed can have gain > 1, which is why `b` is one global value.
* **Input and output patterns are bent sequences.** A ±1 pattern that is a Walsh function would be
  focused by H into a single line on every other pass. A bent sequence has a flat Walsh spectrum, so
  H maps it to another ±1 pattern and the energy stays spread over all 16 lines. The four patterns
  (in L, in R, out L, out R) are mutually orthogonal, which keeps the outputs decorrelated (0.03).
* **Decay.** Loop gain per line is `10^(−3·D/(T60·fs))`, and the shelf is raised to the same power
  of D, so every line loses the same amount per second at every frequency. Measured against a 3 s
  setting: 3.06 / 3.00 / 2.99 s at 250 / 500 / 1 k.
* **Loop filter per line, morphing in three stages:**
  1. **High-shelf damping** (TPT one-pole shelf). Fades out as `max(loose, breach)` rises.
  2. **Loose-slab bell** at the material's per-wall slab frequency:
     `(1 + (G−1)·k·BP)/G` with `G = 20·loose dB`. Peak gain is exactly 1 and everywhere else 1/G, and
     the line gain is stretched to `g_RT^(1−0.6·loose)`. The slab mode rings on while the rest of the
     tail dies.
  3. **High-pass venting**, 12 Hz as a breach begins up to 912 Hz when void, with broadband
     loss `× (1 − 0.65·breach)`.

  Each stage has |H| ≤ 1 and the line gain is < 1, so the loop stays stable in every state. The test
  harness checks this in each damage phase at Decay 12 s and Size 2.

  Stages 2 and 3 are exact pass-throughs while no wall is loose or breached, so they are skipped for
  an intact room and their state is cleared as they switch off. Filter state is stored
  structure-of-arrays (one array per quantity), so each stage is one element-wise loop over 16
  lines that the compiler turns into SIMD; sums are fixed trees, so they vectorise without relaxing
  floating-point rules. The shelf's corner and depth ramp per sample like the gains do.
* **Control cost.** Loop gains and filter coefficients depend only on the walls, the material, Decay
  and Size. They are recomputed only when one of those moves (a wall by more than 0.02 %), and then
  without `pow()`: every power is an `exp()` of a rate worked out once, pre-warps use a Padé `tan`,
  and the per-line drift oscillators are rotating phasors.
* **Exterior void.** Each line's vented energy, `breach·(x − (1 − 0.65·breach)·hp)`, is summed into
  the `ExteriorTank`, a 4-line FDN configured by a `VoidProfile` (`Voids.h`). Free Field Loss
  discards it: the tank's input ramps to 0 over one tick, and whatever is already outside rings out.
  * Line lengths glide over ~150 ms when the void changes, with a slow per-line LFO for water or
    heat-haze wobble.
  * Loop gains come from the void's RT60, followed by an in-loop low-pass for darkness/distance.
  * One allpass sits inside each loop (24–54 ms, gain 0.55 × diffusion). It is lossless, so
    stability is untouched, but every pass is smeared: a smooth void stays smooth instead of
    repeating what was vented into it, and echo voids (low diffusion) keep their echoes. The loop
    gain accounts for the allpass's delay.
  * When nothing is coming in and nothing above −140 dB is circulating, the tank stops running
    until a breach feeds it again (always, for Free Field Loss).
  * The matrix is `A = I − d·(2/N)·11ᵀ`, with eigenvalues 1 and 1 − 2d, so |λ| ≤ 1 for any
    diffusion d. Low diffusion decouples the lines into discrete echoes (Concrete Canyon) or flutter
    (Industrial Tunnel).
  * Grit (Burning City) is an asymmetric tanh, `(tanh(drive·x + 0.25) − tanh(0.25))/drive`, applied
    **outside** the loop so it can never destabilise the tank. It is followed by a high-pass that
    also removes the shaper's DC.

  | Void | Lines (ms) | RT60 | Damp | Diffusion | Wobble | Grit | Character |
  |---|---|---|---|---|---|---|---|
  | Free Field Loss | — | — | — | — | — | — | Vented energy is lost; a breached room just gets drier |
  | Infinite Secondary Tail | 114–242 | 45 s | 1.4 kHz | 1.0 | 1 ms @ 0.09 Hz | — | Distant, endless wash |
  | Industrial Tunnel | 41–62 | 7 s | 3.8 kHz | 0.35 | 0.3 ms | — | Metallic flutter echoes |
  | Abyssal Drone | 311–691 | 90 s | 600 Hz | 1.0 | 2.5 ms @ 0.07 Hz | — | Bottomless, dark, slowly moving |
  | Burning City | 97–223 | 14 s | 5 kHz | 0.9 | 0.8 ms | drive 6 | Distorted, gritty exterior |
  | Flooded Undercroft | 157–317 | 22 s | 350 Hz | 1.0 | 4 ms @ 0.21 Hz | — | Muffled underwater wobble |
  | Concrete Canyon | 187–433 | 6 s | 2.5 kHz | 0.1 | — | — | Discrete slapback echoes off far buildings |

### 4.5 Modal Debris — `ModalDebrisGenerator`

* 32 voices. Each is a **complex one-pole resonator** `z ← r·e^{jω}·z + x`, output `Im z`: a
  second-order bandpass that stays numerically clean at very high Q. Each voice has its own RNG, so
  the output doesn't depend on how the host slices audio. The quietest ringing voice is stolen when
  all are busy.
* **Fracture** → 2–8 modes from the material's ratio table × per-wall tuning (±3.5 % detune), with
  T60 ∝ 1/√ratio, stretched by looseness. Excitation is a 0.8–4 ms noise burst for a crack, or
  6–20 ms for a ductile groan. Hard hits may add a **rebar ping**, a high-Q mode at `pingHz`.
* **Chatter.** A Poisson scheduler per wall: `rate = 30·chatterEnergy + 18·loose·inputActivity`
  events/s. Each event is a small high-pitched fragment. Chatter energy decays with τ = 0.6–1.8 s.
* **Contact rattle.** A one-sided soft clip at a gap that shrinks with looseness (the slab slapping its
  frame), mixed in by `rattleAmount · looseness`, then a 20 Hz DC blocker.
* **Voices run side by side.** The resonators are independent, so the ringing ones are packed into
  arrays and every sample advances all of them at once (SIMD), instead of each voice running its own
  serial loop over the block. With all 32 ringing this costs 22 ns per sample (it was 121).

### 4.6 Impact — `ImpactGenerator`

The modal debris gives a fracture its pitch; this layer gives it weight. Voiced per material by
`MaterialVoicing` (boom pitch and decay, crack band and length, rubble band):

* **Boom**: a sine whose pitch falls an octave (f0 → f0/2, τ ≈ 90 ms) with an exponential decay
  and a touch of 2nd harmonic. It is mono, so the low end stays solid. Walls failing in the same
  40 ms are one collapse: they merge into one boom (energy sum, capped) instead of stacking coherent
  sines. That merge took the hottest preset peaks from +3.5 dB to +2.3 dB.
* **Warning crackle**: real structures click before they fail (acoustic emission). Once a wall's
  stress ratio passes 0.55, faint ticks start, getting denser and louder up to the breaking point
  (1.05), so a slow swell is heard straining the room before anything gives.
* **Crack**: not one burst but a propagating crack. A fracture fires a cascade of micro-cracks over
  the material's `spreadMs` (3 ms glass … 60 ms timber), sizes drawn from a power law (many small,
  few big), each a band-passed noise click panned to the wall. Bigger breaks go deeper and longer.
* **Dispersion chirp**: in ice, steel and glass the crack's sound runs through the sheet and arrives
  as a falling whistle (an exponential sweep, `chirpHiHz → chirpLoHz` over `chirpMs`), the "pew" of
  lake ice and the boom-roll of a buckling steel panel.
* **Falling pieces**: a break frees pieces that land after the time it takes them to fall,
  `sqrt(2h/g)` with h = 2.8 m × Size (so a big room's aftermath arrives later), then bounce: each
  bounce comes `2·e·v/g` after the last, with e the material's `restitution`. Brittle materials
  shatter into a spray of fragments on landing.
* **Aftershocks**: after a big break the structure keeps settling. Small follow-up cracks fire at an
  Omori-law rate, `k/(t+0.15)^1.15`, for up to 5 s.
* **Creak**: ductile damage (plastic yield, fatigue) is a stick-slip pulse train through a resonance
  (`creakHz`, `creakQ`), at `creakRate` pulses per second, with grit, instead of a noise scrape.
* **Rubble**: Poisson noise grains plus a low rumble bed, both driven by a rubble energy that every
  fracture and every landing adds to. Loose walls under load keep it trickling. It settles with a
  time constant of 2.5 s × the rubble left (clamped 0.3–1), so a pile settles faster as less is
  left to fall, and the last grains are smaller as well as rarer. Below 0.03 everything has come to
  rest: the bed and trickle reach zero there and the rubble is cleared. (An exponential alone never
  ends: grains used to keep dropping at a fixed size for 10–25 s after a break, past the reported tail.)

Landings, bounces and aftershocks wait in a queue of up to 384 events, timed on the control grid,
so the result is the same at any host block size.

**Reported tail.** `getTailLengthSeconds` is the room tail (1.5 × Decay + 2.25 s) plus, whenever
Impact, Rumble or Debris is above zero, `ImpactGenerator::kMaxSettleSeconds` (7 s: aftershocks run
5 s and rubble at its cap settles in about 6 s), or the outside space's own tail when it collects
energy (1.2 × its RT60), whichever is longer. Hosts stop processing once it has passed.
* **Shock** (in `Engine`): a fracture envelope (τ 0.35 s) that stretches every FDN line by up to
  1.6 %, so the room's pitch flexes on big hits.

The crack and rubble voices (up to 24 band-passed noise bursts) are advanced together the same way
as the debris voices: noise generators, filters and envelopes in vector steps. The booms use a
polynomial sine (error below −120 dB) instead of two library calls per sample. A busy collapse
costs 47 ns per sample (it was 136).

Impact scales boom, crack, chirp, warning and shock; Rumble scales the rubble and the falling
pieces. DETONATE (`StressTracker::detonate`) removes a material-scaled chunk from every wall and
emits a full-magnitude event per wall. Like a real demolition the charges fire in a random order,
30–110 ms apart, so it is heard as a rolling chain of bangs running into the collapse.

The modal debris follows the same physics: a bigger break frees bigger pieces, which ring lower
(×1.25 down to ×0.85), are struck harder (a shorter, brighter excitation) and are reached by the
crack over up to 21 ms instead of all at once. Chatter fragments follow a power law: mostly faint
ticks, a few loud pieces.

**Onset gate** (`StressTracker`). A brittle fracture also needs an onset: the block's RMS, or the
RMS of its first difference (slope, which tracks the attack of a kick over a held sub), must jump
1.3× above its own held envelope (0.1 s release), and the gate stays open 20 ms so a kick whose
peak lands after its attack still counts. Noise risers and crash tails, whose peaks wander up
without any real onset, no longer break walls by chance.

### 4.7 Tank quality controls (`DynamicFDN::TankControl`)

* **Density**: 4 Schroeder allpasses per side (1.1–4.7 ms, L/R offset) before injection; gain
  0.75 × density. Echo density over 30–120 ms rises from 0.30 (Density 0) to 0.60 (70 %).
* **Motion**: each line drifts on its own sine (0.23–0.81 Hz) plus a smoothed random walk, up to
  ±1.2 ms. Loop gain uses the unmodulated length so the decay doesn't pump. Fixed-peak ringing in the
  0.5–1.6 s tail falls from 5.0 dB (Motion 0) to 4.4 dB (25 %) and 4.1 dB (60 %).
* **Freeze**: each line crossfades to its raw tap at gain 0.99998, with no filters, no injection
  and no venting. It is a convex mix of two paths with |H| ≤ 1, so it cannot become unstable. The
  structure also stops: no damage, no healing.

### 4.8 Wet tone and output

Drive is `(tanh(g·x + 0.2) − tanh 0.2) / (g·sech²0.2) · g^0.3`, with g up to +30 dB: unity slope at
zero and a gentle make-up. It sits before the 12 dB/oct Low Cut, which also removes the asymmetry's
DC. After that come High Cut, Width (the side channel is high-passed at 120 Hz, so the low end is
always mono) and Ducking (a 4 ms / 250 ms follower on the dry input, up to −20 dB). In the plugin:
Input Gain before everything (it also drives destruction), Output Gain after the mix, then a soft
knee limiter from −1 to 0 dBFS (default on).

### 4.9 Oversampling (plugin layer)

One `Engine` is prepared per factor (1x, 2x, 4x) in `prepareToPlay`, so switching never allocates
on the audio thread. On a switch, the new engine resets, inherits the old one's integrity, and the
two crossfade over 40 ms. In the test, the largest sample-to-sample step during six switches under
a tone was 0.04. `juce::dsp::Oversampling` (polyphase IIR) handles the wet path only. The sidechain
key is linearly interpolated, which is enough for peaks and sub-120 Hz energy. No latency is
reported: the dry path is untouched, and the wet path's few samples of filter delay are well under
the room's own pre-delay. With Drive at 100 % on a 7 kHz sine, energy off the harmonic series is
−14.5 dB at 1x, −33.3 dB at 2x and −78.5 dB at 4x. Default: 2x.

## 5. Parameters (APVTS)

| ID | Name | Range / default | Maps to |
|---|---|---|---|
| `material` | Material Archetype | 11 choices, from `kMaterialProfiles` (see 5c) | `MaterialProfile` |
| `yield` | Yield Threshold | −48…0 dB, −12 | `capacity = dB→lin × strength` |
| `bias` | Brittle / Ductile Bias | 0–100 %, 35 % | α = 1 − bias, β = bias |
| `asymmetry` | Asymmetry Ratio | 0–100 %, 30 % | per-wall susceptibility spread |
| `voidType` | Exterior Void Type | 7 choices, from `kVoidProfiles` (see 4.4) | `VoidProfile` → ExteriorTank |
| `heal` | Tectonic Healing | 0.25 s…600 s (skewed, centre 20 s); 600 = **Permadeath** | τheal |
| `reset` | Manual Reset | bool, momentary, rising edge | `Engine::resetStructure()` |
| `size` | Room Size | 0.4–2.0×, 1.0 | image geometry, FDN lengths, slab pitch |
| `decay` | Decay | 0.3–12 s, 2.8 s | pristine FDN RT60 |
| `debris` | Debris Level | 0–100 %, 60 % | debris wet gain |
| `mix` | Dry / Wet | 0–100 %, 35 % | equal-power crossfade |
| `impact` *(v2)* | Impact | 0–100 %, 50 % | boom + crack level, shock depth |
| `rumble` *(v2)* | Rumble | 0–100 %, 30 % | rubble grains + rumble bed |
| `detonate` *(v2)* | Detonate | bool, momentary, rising edge | `Engine::detonate()` |
| `stresssrc` *(v2)* | Stress Source | Input / Sidechain / Input + Sidechain | what feeds `StressTracker` |
| `predelay` *(v2)* | Pre-Delay | 0–250 ms, 0 | delays room input and stress source |
| `density` *(v2)* | Density | 0–100 %, 70 % | input allpass gain |
| `motion` *(v2)* | Motion | 0–100 %, 25 % | FDN line drift depth |
| `freeze` *(v2)* | Freeze | bool | lossless tank, structure paused |
| `drive` *(v2)* | Drive | 0–100 %, 0 | wet saturation (0…+30 dB into tanh) |
| `lowcut` *(v2)* | Low Cut | 20–1000 Hz, 20 Hz | wet HP 12 dB/oct |
| `highcut` *(v2)* | High Cut | 1–20 kHz, 20 kHz | wet LP 12 dB/oct |
| `width` *(v2)* | Width | 0–200 %, 100 % | wet side gain (above 120 Hz) |
| `duck` *(v2)* | Ducking | 0–100 %, 0 | wet gain vs dry envelope |
| `input` *(v2)* | Input Gain | −24…+24 dB, 0 | before everything |
| `output` *(v2)* | Output Gain | −24…+12 dB, 0 | after the mix |
| `limiter` *(v2)* | Safety Limiter | bool, on | soft knee −1…0 dBFS |
| `oversampling` *(v2)* | Oversampling | Off / 2x / 4x, 2x | active engine |
| `bypass` *(v3)* | Bypass | bool, off | 20 ms crossfade to the untouched input; the host's bypass (`getBypassParameter`) |

The v2 parameters use `ParameterID` version hint 2, so hosts keep the v1 automation order. Presets
set the sound controls (Impact through Ducking). They leave Oversampling, Input/Output Gain, the
limiter and Stress Source alone. A v1 session loads with the v2 controls at their defaults.

Size, Decay and Debris Level go beyond the original spec. A reverb needs them to be usable.
`ParameterCache` caches the `std::atomic<float>*` handles once, and `snapshot()` reads them with
relaxed loads.

## 5b. Factory presets

There are 48 presets, defined in `Source/Presets.cpp`: 28 for everyday jobs and 20 in the
Signature category, each built around one behaviour only a breaking room can do. They appear in the editor's preset browser
and in the host's own preset menu (AU factory presets / VST3 programs). Loading a preset sets every
sound parameter (`kPresetParamIds`) and gives you a fresh room: pristine, or the preset's own
starting damage for the presets that start as ruins. The session remembers which preset it was based on.

Every preset carries tags from a fixed vocabulary of 17 (`presetTagVocabulary()`), in three groups:
use (Vocals, Drums, Bass, Keys & Pads, Sound FX), behaviour (Unbreakable, Fragile, Self-healing,
Permadeath, Starts ruined) and sound (Short, Long tail, Dark, Bright, Metallic, Gritty, Wide).

`PresetLibrary` (owned by the processor, message thread) lists the factory presets and the user's
own `.rbpreset` files (small XML: name, tags, plain parameter values) from
`~/Library/Application Support/RoomBreaker/Presets` on macOS (not Documents: macOS asks permission
before a plug-in may read it) and `%APPDATA%\RoomBreaker\Presets` on Windows. Search matches every
word against name, category, tags and description, with name matches first; tags combine with AND;
the category narrows further. The library remembers the values it loaded, so the header can mark the
preset as modified. Presets never touch Input/Output gain, oversampling, the limiter, the stress
source or bypass, so browsing never jumps the level or the CPU load.

| # | Preset | Use it on | What it does (from the 10 s audition) |
|---|---|---|---|
| 1 | Mix · Default Bunker | anything, starting point | Holds at mix levels, cracks a little on loud bass, heals in ~20 s |
| 2 | Mix · Vocal Plate (Unbreakable) | vocals, leads | Never breaks; a plain bright reverb |
| 3 | Mix · Ceramic Bathroom | vocals, guitars, small-room feel | Short tiled room; chips only on hard hits, repairs in ~1.5 s |
| 4 | Drums · Drum Room – Hairline Cracks | drum buses, overheads | Tight room; loud hits leave hairline cracks (≈99 % intact), so the space moves slightly |
| 5 | Drums · Snare Shatter Chamber | snares, claps, percussion | Splinters on snare hits with glassy debris, recovers within seconds |
| 6 | Drums · Breathing Bunker | grooves, loops | Gives a little on each hit and rebuilds in 0.5 s, so the reverb pumps with the beat |
| 7 | Drums · Gunshot Bunker | gunshots, foley, hits | Hard concrete slap; transients knock chunks off with debris |
| 8 | Bass · Sub-Bass Silo Groan | 808s, sub bass | Untouched by drums; sustained bass dents the metal into groaning resonances |
| 9 | Bass · Underground Dub Bunker | dub sirens, bass stabs | Long dark tail; bass cracks the walls and vents into an endless exterior echo |
| 10 | Cinematic · Trailer Hit Demolition | impacts, braams | Huge hall that holds through drums and caves in on big hits; press Rebuild between hits |
| 11 | Cinematic · Collapse to Open Sky | transitions, risers | Never heals; anything you play erodes the room until it's nearly dry |
| 12 | Cinematic · Earthquake Room | rumbles, drones, low hits | Ignores transients; long low notes shake it apart into continuous creaking |
| 13 | Ambient · Bombed Cathedral | pads, choirs, vocals | Starts as a roofless ruin with sky beyond; too sturdy to crumble much further |
| 14 | Ambient · Haunted Silo | pads, textures | Starts with loose panels: metallic resonances and rattles; stays as it is |
| 15 | Ambient · Infinite Void Drone | drones, washes | Room almost gone; nearly everything feeds the endless exterior tail |
| 16 | Sound Design · Tin Shed Rattle | FX, percussion, bass hits | Small shed that buckles on loud bass and springs back within a couple of seconds |
| 17 | Sound Design · Slow Erosion (Permadeath) | full songs | Wears down gradually over a track and never recovers; automate Rebuild |
| 18 | Sound Design · Lopsided Vault | stereo FX | The weak walls shatter while the others hold, so the image tilts as it breaks |
| 19 | Dark Cinematic · Ash Cathedral | pads, vocals, slow chords | Burnt-out stone nave (starts ruined) over the Abyssal Drone; crumbles a little further under drums |
| 20 | Dark Cinematic · Hangar Bass Dread | distorted sub, reese bass | Drums leave it alone; sustained bass dents rusted steel, and the groans leak into Burning City |
| 21 | Dark Cinematic · Monolith Footsteps | slow heavy drums | Huge brutalist hall; only hard hits crack it, and broken walls throw Concrete Canyon slapbacks |
| 22 | Dark Cinematic · Drowned Signal | bass, drones, FX | Submarine hull in the Flooded Undercroft: muffled wobble, sonar pings, groans when the low end pushes |
| 23 | Dark Cinematic · Glass Rain Collapse | impacts, percussion | Weak panes shatter on drums and the rest on the big hit, raining shards into a long cold tail |
| 24 | Dark Cinematic · Black Ice Requiem | pads, strings | Starts fractured; sustained notes make the ice ring over the Abyssal Drone |
| 25 | Dark Cinematic · Tunnel Pressure Wave | kicks, risers, trap-style drums | Tiled tunnel with metallic flutter; kicks crack tiles and push energy down the tunnel |
| 26 | Dark Cinematic · Burning Rafters | textures, bass, FX | Charred timber creaks and splinters under load, with a distorted fire-lit city beyond |
| 27 | Dark Cinematic · Sub Collapse Sequence | builds, drops | Sustained sub slowly crushes a monolith into the abyss over a build (never heals); Rebuild on the drop |
| 28 | Dark Cinematic · Dread Engine Room | gritty leads, synth stabs | Starts with loose steel panels; everything you play sets the structure buzzing and rattling |
| 29 | Signature · Consonant Shards | vocals | Glass only plosives can break: sung notes stay smooth, consonants throw shards into the tail |
| 30 | Signature · Kick-Torn Cathedral | pads, with the kick on the sidechain (Break by = Key) | Every kick tears the nave open into the outer hall; the stone seals between kicks |
| 31 | Signature · Chorus Memory | full songs | Heals over 75 s: the chorus wrecks it, the next verse plays in the wreck, then it rebuilds |
| 32 | Signature · Growing Canyon | builds, drums | Starts cracked onto a canyon; each big hit opens more for good, multiplying the slapbacks |
| 33 | Signature · Garage Amp Shed | guitars, bass | Small driven steel shed: soft playing stays clean, digging in buckles and rattles the panels |
| 34 | Signature · Porcelain Dollhouse | music box, keys, plucks | Toy porcelain room; accents chip it for good and the cue sinks into the abyss |
| 35 | Signature · High Vault Aftermath | hits, toms | Double-height vault: the stone a hit shakes loose lands up to a second later, on its own beat |
| 36 | Signature · Tile Snare Exciter | snares, claps | Accents crack tiles and ghost notes don't; heals in 0.25 s, so each accent gets a new crack |
| 37 | Signature · Riser Tension | risers, swells, drops | Never breaks on a swell but crackles louder and faster under it; the drop's first hit breaks it |
| 38 | Signature · Singing Lake Ice | percussion, FX | Every hit sends a falling crack whistle through lake ice, refreezing in ~2.5 s |
| 39 | Signature · Controlled Demolition | anything; press Detonate | Music can't break it; Detonate fires a rolling chain of charges, collapse and aftershocks |
| 40 | Signature · Hearth Crackle | acoustic, keys, vocals | Charred timber that splinters and re-sets on every note: the reverb crackles like a fire |
| 41 | Signature · Breath Abyss | spoken word, vocals | A near-gone hull, fully ducked: the abyss rises only in the pauses |
| 42 | Signature · Sinkhole | 808s, bass, kicks | Floor caved into a flooded cavern: the low tail wobbles under water, the top stays tight |
| 43 | Signature · Thunder Sheet | claps, snares, foley | A hanging steel sheet roars and buckles on every hit, rolling back off the canyon walls |
| 44 | Signature · Rising From Rubble | intros, pads | Starts as rubble and rebuilds while the music is quiet, closing into a bright room |
| 45 | Signature · Booth to Stadium | vocals, drums | Dead booth at verse level; chorus hits breach it and a stadium blooms through, then seals |
| 46 | Signature · Breakage Foley Printer | drum loops (print it) | 100 % wet glass that breaks on every hit and re-glazes: a glass layer locked to the groove |
| 47 | Signature · Glacier Groan | bass, drones | Only long low notes strain it: groans, pops and falling whistles over a bottomless drone |
| 48 | Signature · Distant Front Line | drums, FX | Your sound a kilometre away: 250 ms of travel, highs lost, hits answered by distant blasts |

Audition renders of every preset (the same drums → bass → impact → tail script) are written to
`renders/presets/` by `rb_plugin_smoke`. The smoke test also checks that every preset is finite and
peaks below 1.5, that loading sets the parameters and the starting room, and that the preset index
survives save/recall.

Tweaks survive session reload. JUCE's VST3 wrapper only calls `setCurrentProgram` when the
program index actually changes, and `currentProgram` is restored from the session before the host
sees it. The AU wrapper applies a preset only when the user picks one (`NewFactoryPresetSet`); its
session restore goes straight to `setStateInformation`. Checked against the JUCE 8.0.4 wrapper
sources.

## 5c. Materials

| # | Material | Strength | Brittle | Fatigue | HF damping | Slab modes | Debris voice | Character |
|---|---|---|---|---|---|---|---|---|
| 0 | Concrete Bunker | 1.6 | 0.45 | 0.22 | 3.5 kHz | 52–96 Hz | 180 Hz, rebar ping 3.1 kHz | Balanced default |
| 1 | Ceramic Chamber | 0.9 | 0.90 | 0.07 | 8 kHz | 170–310 Hz | 900 Hz bar modes | Shatters on transients, ignores LF |
| 2 | Corrugated Silo | 1.1 | 0.15 | 0.90 | 6 kHz | 33–55 Hz | 140 Hz, long ring | Dents and groans under sustained LF |
| 3 | Rusted Hangar | 1.0 | 0.25 | 0.70 | 4.5 kHz | 26–44 Hz | 95 Hz, 2.8 s ring | Vast corroded steel, maximum rattle |
| 4 | Brutalist Monolith | 2.4 | 0.40 | 0.15 | 1.8 kHz | 35–58 Hz | 70 Hz thud | Strongest and darkest; falls like a mountain |
| 5 | Shattered Glass Atrium | 0.6 | 1.00 | 0.04 | 11 kHz | 290–450 Hz | 1.4 kHz shards, ping 8.2 kHz | Weakest; explodes into bright shards |
| 6 | Cathedral Stone | 1.9 | 0.55 | 0.12 | 5.2 kHz | 44–66 Hz | 120 Hz blocks | Grand limestone, crumbles in blocks |
| 7 | Subway Tile Tunnel | 1.2 | 0.70 | 0.12 | 6.5 kHz | 100–160 Hz | 520 Hz clacks | Clacky, mid-forward tile |
| 8 | Cracked Ice Sheet | 0.8 | 0.95 | 0.05 | 9.5 kHz | 130–190 Hz, Q 18 | 650 Hz, 2.2 s "singing" | Cold; fractures ring for seconds |
| 9 | Burnt Timber Frame | 0.8 | 0.30 | 0.80 | 2.2 kHz | 80–120 Hz | 210 Hz, short | Warm, absorbent, creaks and splinters |
| 10 | Sunken Bulkhead | 1.3 | 0.10 | 0.65 | 1.2 kHz | 20–33 Hz, Q 20 | 60 Hz, sonar ping 880 Hz / 5 s | Submarine steel under pressure |

Strength multiplies the Yield Threshold. Brittle sets how close to yield a transient must get to
fracture. Fatigue is the creep rate under sustained low-frequency load.

Automation compatibility: the Material and Exterior Void menus grew from 3 and 2 choices to 11 and 7.
Saved sessions are unaffected, because APVTS stores the choice index. Host automation recorded
before this change stores normalised values, so it would land on a different choice. RoomBreaker
hasn't been released, so nothing in use is affected. Once it ships, only append new choices to the
end of these tables.

## 5d. Editor

One console, laid out once at **1200 × 750 design units on an 8 px grid** and scaled as a whole.
The house is the only bright, busy thing on it; everything else is quiet so the picture and the
numbers read first. Level flows left to right: input rail, the room, the controls, output rail.

```
+--------------------------------------------------------------------------------+
| ROOMBREAKER 2.5.0      [<][>]  Preset name ........ CATEGORY v  [save]  ACTIVE 100% (o) |  48
+------+-------------------------------------+---------------------------+-------+
| IN   |                                     | ROOM   Material  Outside  | OUT   |
| -3.2 |  the house                          | STRUCTURE    o  o  o  o   | -0.4  |
| ||   |  condition readout (top left)       | DESTRUCTION  o  o  o  o   | ||    |
| ||   |  hovered surface, turn hint         | SPACE        o  o  o  o   | MIX   |
| INPUT|  BYPASSED when bypassed             | TONE         o  o  o  o   | DUCK  |
| BREAK|                                     | [FREEZE] [REBUILD] [DETONATE] | OUTPUT|
+------+-------------------------------------+---------------------------+-------+
| info line: what the mouse is over             OVERSAMPLING [1x 2x 4x] [LIMITER] 48 kHz  cpu |  32
+--------------------------------------------------------------------------------+
   88                    624                              392              88
```

**Hierarchy.** Three levels and no more: the house (the only colour image); values (13 pt mono,
brightest text); everything else (labels, hairlines, chrome) in two steps of grey. Sixteen sound
knobs sit in four labelled sections of four, in the order you would set them: what the room is,
how it breaks, its size and time, its colour. The I/O gains, Mix and the meters are on the rails at
the two ends of the signal, always visible, never behind a menu. Engine settings (oversampling,
limiter) live in the footer because you set them once.

**Colour** (`Theme.h`, namespace `C`). Six greys do all the structure. Two colours carry meaning,
and they mean the same thing everywhere.

| Token | Hex | Used for | Contrast on `bg1` |
|---|---|---|---|
| `bg0` | `#111315` | window, rails, wells (meters, fields) | |
| `bg1` | `#181B1E` | panels: header, controls, footer, browser | |
| `bg2` | `#202428` | raised: buttons, knob caps | |
| `bg3` | `#2A2F34` | raised under the mouse, selected rows | |
| `line` | `#2C3136` | hairlines between areas | |
| `edge` / `edgeHi` | `#3A4046` / `#4D555C` | control outlines and knob track / the same under the mouse | |
| `text0` | `#E9E5DD` | values, names: anything you read | 13.8 : 1 |
| `text1` | `#A7ADB2` | labels | 7.6 : 1 |
| `text2` | `#868E95` | units, hints, scales, disabled | 5.2 : 1 |
| `accent` | `#F0A43C` | what you are touching or have selected; caution (bypassed, −6…0 dBFS) | 8.3 : 1 |
| `danger` | `#EF5B4F` | damage, over 0 dBFS, Detonate | 5.2 : 1 |
| `meter` | `#9FC2B6` | meter body below −6 dBFS | 8.9 : 1 |

Every text colour passes WCAG AA (4.5 : 1) on `bg0`, `bg1` and `bg2`; on `bg3`, used only under the
mouse and for the selection, `text2` drops to 4.1 : 1, so highlighted text uses `text1` or brighter. Nothing glows. The knob caps are the one gradient
(two stops, lit from above), because the cap is the one place where a physical cue helps:
it says "grab here".

**Type.** Two embedded families, so Windows and macOS render identically; three sizes, nothing else.

| Role | Face | Size (pt) |
|---|---|---|
| Labels, section heads, buttons (always capitals, tracked +7 %) | Barlow Semi Condensed SemiBold | 11 |
| Body: help line, menus, fields, preset list | Barlow Semi Condensed Medium | 13 |
| Preset name, wordmark | Barlow Semi Condensed SemiBold | 16 |
| Values | JetBrains Mono | 13 |
| Meter scales, readouts, status | JetBrains Mono | 11 |

Barlow is a grotesque drawn from road signage: condensed enough for 16 labels in a column, and
legible at 11 pt. Values are monospaced so they don't jitter while you drag.

**Icons.** Nine (prev, next, caret, search, save, close, power, folder, trash) on a 16 × 16 grid,
1.5 px stroke, square caps, mitred joins, no fills, on 32 px click targets (`iconPath`).

**Shape.** 3 px radius on controls, 0 on areas. Areas are separated by 1 px hairlines, not boxes
or shadows.

**Controls** (`Widgets.h`). Each one talks to its parameter through a JUCE attachment, so every
edit reaches the host as one begin / set / end gesture (touch automation, undo and latch work).

| Control | Used for | Interaction |
|---|---|---|
| `Knob` | all 20 continuous parameters | Drag up/right, 250 screen points for the full range, in screen points so it feels the same at every scale; Shift = 10x finer, incremental so pressing Shift mid-drag never jumps. The pointer hides while dragging and returns where it went down. Wheel: 2 % per notch, Shift 0.2 %; trackpads scroll continuously; fractional steps add up on stepped parameters. Double-click: type a value, units optional ("120 ms", "4.5k", "-6", "permadeath"); Return or clicking away applies, Escape cancels, nonsense is ignored, values clamp. Alt- or Cmd/Ctrl-click: default. Right-click: type, reset, and the host's own items for the parameter (`getContextMenuForParameter`). Hover or drag: the arc and the value turn amber and the default is ticked on the scale. Bipolar parameters (Width, the gains) draw the arc from their centre. Knobs never take keyboard focus, so the host's space bar keeps working. |
| `Segmented` | Oversampling, Break by | All options visible; the selected one is lit with an amber bar. |
| `ChoiceBox` | Material (11), Outside (7) | Drop-down; the wheel steps through it. |
| `ToggleBox` | Freeze, Limiter | A switch with an LED: amber when on. The body stays neutral, because "Limiter on" is normal and must not shout. |
| `ActionButton` | Rebuild, Detonate (red outline) | Momentary: the parameter is 1 while held, as one gesture. |
| `StereoMeter` | input (after Input gain), output (after the limiter) | −60…+6 dBFS, linear in dB. Solid = RMS (300 ms), light = peak falling 24 dB/s, white line = 1.5 s hold. Colour by zone, never by gradient: sage, amber from −6, red over 0. The number above is the highest peak so far, boxed red after a true over; click to reset. |

**Status.** Header: ACTIVE / BYPASSED next to the power switch (BYPASSED in amber); the display
dims and says BYPASSED. Preset: an amber dot before the name once anything moved since it was
loaded. Footer: session rate, the room engine's rate when oversampled, CPU.

**Preset browser** (`PresetBrowser`). Opens over the display and controls from the preset name;
the meters stay visible. Search field (focused on open), categories with live counts, tag chips
(AND), a list of name / tags / category, and the selected preset's description. Each preset loads
as it is selected, so Up/Down audition; Return keeps it, Escape clears the search then closes,
double-click loads and closes. Save as... asks for a name and tags (pre-filled from the current
preset); Delete moves a user preset's file to the Trash; the folder button reveals the presets
folder. The header's ‹ › step through every preset without opening it.

**Scaling** (`RoomBreakerAudioProcessorEditor::resized`). Everything lives in one `Canvas` component
at design size; resizing only changes its `AffineTransform`. The constrainer locks the aspect
ratio (75–200 %, corner handle or the size menu), and a host that forces another shape gets the
largest fit, centred. Because JUCE renders text and paths as vectors through the transform, nothing
is a stretched bitmap; the house's cached layers render at the physical pixel density (half steps,
up to 3x), so Retina and Windows scaling stay sharp. Drag sensitivity is measured in screen
points, so knobs feel the same at every size. The scale is saved with the session.

**Info line.** The footer explains whatever the mouse is over (the first sentence of `Help.h`), and
otherwise how the knobs work. It follows the mouse through a listener on the canvas.

## 5e. The house

`HouseView` draws the room as a small cottage on a slab of lawn, in a pure orthographic projection
(30° elevation, drag to turn, double-click to face it again). Every face is planar, so it is drawn in
its own (u, v) metres through one `AffineTransform`, clipped to its outline; back faces are culled
and the rest are painted far to near. Patterns, cracks and shadows therefore foreshorten correctly
at any angle.

| Surface | House part | What damage does |
|---|---|---|
| North / South / East / West | the four walls (South has the door) | cracks grow from window and door corners; the finish comes away; holes open; past ~82 % the wall collapses from the top |
| Ceiling | the roof | tiles slip, then go missing (battens and felt show), then holes open to the rafters; ridge caps fall; the chimney loses its cap, leans and breaks off |
| Floor | the lawn | fissures open as trenches, the grass dries, and the house settles up to ~3° off level |

**Damage is drawn from integrity alone** (d = 1 − I against thresholds generated once from a fixed
seed), so the picture always matches the sound and heals in reverse.

* **Cracks** are polylines that wander about a mean direction, kink and fork. Each point has the
  damage at which the crack reaches it, so cracks lengthen smoothly and widen with age; they are
  filled as tapered outlines with a light lip on the side away from the light.
* **Each material fails its own way** (`Finish` in `HouseModel.h`):

  | Finish comes away as | Used by | Looks like |
  |---|---|---|
  | patches (`SpallBlob`) | concrete, brutalist, stone | angular recesses showing aggregate, brick or rubble core |
  | cells (`SpallCells`) | ceramic, subway tile, glass, timber | tiles and boards drop one by one; panes craze, then fall out |
  | stains (`SpallStain`) | corrugated sheet, bulkhead plate, ice | scorch, rust or frost where a thicker wall would spall |

  Holes are either ragged breaches (`HoleBlob`) or whole cells going missing (`HoleCells`: stepped
  stone, empty glazing frames, a stud skeleton). Cell thresholds come from the same weak spots as
  the patches, so damage sits in the same places whatever the material.
* **Depth without 3D.** A hole is drawn twice: once in the wall's front face (filled with the colour
  of the cut edge) and once more, shifted by the wall's thickness, as a clip for the interior. The
  sliver between the two is the wall's section. Windows and doors use the same trick for their
  reveals; rebar or studs are drawn half-way between. Concrete that has collapsed leaves rebar stubs
  above the broken edge.
* **The interior** (far walls with skirting, floorboards, a rug, rafters, daylight through far windows
  and through the far walls' own holes) is painted once per frame into an image and shown through
  every opening.
* **Light.** Each Outside sets an ambient colour and a sun colour (`Backdrop`); every surface is lit
  by its normal. Cast shadows are computed, not painted on: the roof's shadow down each sunlit wall,
  sills and casings, the chimney across the roof, bushes and the house across the lawn. A wrecked
  house weakens the sun and greys the ambient light instead of drawing a haze over the picture.
* **Debris.** Fractures throw pieces from where the wall is failing at that moment (the weak spot
  whose threshold the damage has just crossed). Pieces are shaped by material (chunks, tiles, shards,
  splinters, scrap), tumble, cast shadows on the lawn, bounce and settle, and fade once they leave
  the lawn. Dust puffs are soft radial fills. Rubble heaps at the foot of each wall are generated
  once and revealed by damage, so a state recalled from a session shows the same wreck. A hit across
  several surfaces at once sends a ring across the grass.
* **Cost.** The view repaints only when its state signature changes (view angle, integrity, material,
  hover, particles), so an untouched room costs nothing. The backdrop, the static lawn and the soft
  slab shadow are cached as images at device resolution. A repaint takes about 2.7 ms with an intact
  house and 7–8 ms with a wrecked one (Apple Silicon, CoreGraphics).
* Hovering a wall names it and shows its integrity and load; a compass shows north after turning.

`rb_plugin_smoke` writes a gallery (`renders/gallery/`): contact sheets of every material at three
damage levels in each outside, close-ups, and three frames of a detonation.

## 6. Threads and telemetry

| Data | Producer → consumer | Mechanism |
|---|---|---|
| Parameters | host/UI → audio | APVTS atomics, one snapshot per host block |
| Manual Reset | UI/automation → audio | bool parameter; audio thread acts on the rising edge; UI button is momentary inside one change gesture |
| Wall integrity (latest) | audio → any | `std::atomic<float>[6]` (`Engine::currentIntegrity`, `TelemetryBus::integrity`) |
| Telemetry frames | audio → UI | `TripleBuffer<TelemetryFrame>` at ~120 Hz; the UI always gets the newest complete frame and neither side blocks |
| Fracture flashes | audio → UI | per-wall `std::atomic<float>`: the audio thread fetch-maxes, the UI takes with `exchange(0)`, so no crack is lost when a frame is overwritten |
| Structure recall | message → audio | `requestStructureRestore`: atomics + `restorePending` flag, applied at the next block |

The UI timer (30 Hz) reads the newest frame. A FIFO would be the wrong structure here: with no
editor open it fills with old frames and then drops every new one, so the editor would open on
stale data. The first build had exactly that bug, and the plugin smoke test's editor snapshot
exposed it. Tearing across walls is possible on the plain atomics, which is harmless for display.
Code that needs a consistent snapshot of all six walls uses frames.

## 7. State persistence

`getStateInformation` stores the APVTS tree plus a `<Structure wall0..wall5>` child read from the
atomics, the factory program, the user preset in use (`presetUser`, `presetName`) and the editor
size (`uiScale`). A wrecked or permadeath room reopens exactly as it was left. `setStateInformation` strips
that child, hands it to the audio thread, and replaces the APVTS state. `Engine::prepare` keeps
current damage across sample-rate changes, unless a recalled state is already waiting.

## 8. Verification (offline harness)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # fetches JUCE 8.0.4 (RB_CORE_ONLY=ON skips it)
cmake --build build -j8                          # VST3, AU, Standalone, both test programs
./build/rb_offline_render renders                # DSP core scenarios
./build/rb_plugin_smoke renders                  # the real AudioProcessor, headless
```

| Check | Result |
|---|---|
| Silence → output exactly 0, walls pristine | pass |
| Drums −36 → 0 dBFS, then 42 Hz fatigue, then silence (Concrete, heal 6 s) | first cracks at ~7 s, mean 0.30 at 14 s, 0.94 at 30 s |
| Permadeath freezes integrity | pass |
| Manual reset restores all walls | pass |
| Worst case: 4 damage states × 2 core voids, every new void fully breached, every new material at peak looseness into the 90 s Abyssal Drone (Decay 12 s, Size 2, full-scale noise) | 21 runs, all bounded (peak < 1.9) and decaying |
| 64- vs 333-sample host blocks | max diff 3.9e-7 |
| Telemetry: late UI gets newest frame; overwritten cracks still delivered; 1.28 M concurrent reads | no torn or out-of-order frames |
| AddressSanitizer + UndefinedBehaviorSanitizer, ThreadSanitizer | clean |
| CPU (core, 1x) | ~135× real time, one core, Apple Silicon. Per sample: 105 ns intact (was 123), 105 ns while being destroyed (was 125), 144 ns cracked but static (was 131: 24 interpolated reflections plus twice the tank lines) |
| CPU (plugin, 10 s noise) | 1x 155×, 2x 95×, 4x 45× real time |
| Quality: Motion | tail ringing 4.6 → 4.5 → 4.0 dB (Motion 0 / 25 / 60 %) |
| Quality: Density | echo density 30–120 ms 0.34 → 0.70 (Density 0 / 70 %). Tank alone at 70 %: fully dense (1.0) by 120 ms, was 200 ms |
| Sanitizers | core harness under ASan + UBSan: all checks pass, 0 reports |
| Rate independence | mean integrity on the same script 0.306 / 0.305 / 0.304 at 48 / 96 / 192 kHz |
| Automation clicks (test 11): 17 controls jumped mid-signal | worst step ≤ 3× clean (Drive 0→100 % ≤ 6×, Size ≤ 4.5×, Material ≤ 4×) |
| Fuzz (test 12): 40 s of random parameter jumps on noise and hits | finite, bounded |
| `auval -strict -v aufx RmBk Rbrk` (installed AU, 2.5.0) | AU VALIDATION SUCCEEDED, 28 factory presets listed |

Plugin smoke test (`rb_plugin_smoke`, the real `AudioProcessor`):

* **Parameters and buses:** 29 parameters with correct defaults and display text; the bypass
  parameter is the host's. Stereo,
  mono-to-stereo and a stereo sidechain are accepted; 5.1 is rejected.
* **Processing and state:** random host block sizes (1–1024) stay finite and bounded. Damage and
  parameters survive save/recall. Rebuild fires once on its rising edge.
* **Presets:** every preset renders a 10 s audition. The limited peak stays ≤ 0 dBFS, and the
  pre-limiter peak stays < 2 (worst 1.09).
* **Signature presets do what they say:** one check per preset against its description. Examples:
  Consonant Shards breaks on plosives but not on sung vowels; Kick-Torn Cathedral breaks only from
  the sidechain; Riser Tension survives a swell and breaks on the drop; Controlled Demolition holds
  through 0 dBFS drums and falls only to Detonate; Breath Abyss's wet level is 19 dB higher in the
  gaps than under the words; Rising From Rubble goes from 3 % to 71 % over a 30 s quiet intro; the
  rest break on −6 dBFS hits as described.
* **Silence in, silence out:** every preset outputs digital zero for digital zero in. After 7 s of
  drums, sub bass and a 0 dBFS hit, every preset is below −80 dBFS once its reported tail has
  passed (loudest: −91 dBFS, Infinite Void Drone's outside space; before the rubble fix, Breakage
  Foley Printer was still at −28 dBFS there).
* **New controls:**
  * DETONATE damages every wall, fires once and is audible on silence.
  * FREEZE holds the tail (86 % after 7 s), and release lets it decay.
  * The limiter holds a +12 dB detonation at 1.000.
  * Oversampling lowers aliasing and switches click-free (largest step 0.041) while keeping the room's damage; the old tail fades over 1.2 s.
  * A sidechain key breaks the room while Input mode ignores it.
* **Bypass and meters:** both meter channels follow the signal; after the 20 ms fade the bypassed
  output equals the input (largest difference 1.5e-8); un-bypassing stays finite.
* **Preset library** (in a temporary folder, never the user's): search, tag AND, category, the
  tag vocabulary, factory load sets the host program, the modified mark, save / reload a user
  preset, gains untouched by presets, a session that remembers its user preset.
* **Knobs:** typed values with and without units, nonsense ignored, clamping; wheel 2 % per notch
  and 0.2 % with Shift, fine steps adding up on a 0.1 dB parameter; reset; every edit one complete
  host gesture (18 begun, 18 ended).
* **Scaling:** 150 % is 1800 × 1125 with the canvas still 1200 × 750 inside; the size survives a
  save / load; it clamps at 75 % and 200 %.
* **Browser:** search and tag filtering through the UI.
* **Editor:** snapshots after pumping audio through the plugin (pristine, damaged, detonated, from
  behind, the browser, the info line) plus 2x close-ups of the controls, header and meter, and a
  150 % crop. It poses the house at set damage levels in every material for the gallery and times
  a repaint.

Material personalities on the same script, with permadeath:

| Material | First crack | First breach | Fracture frames | Mean integrity @14 s |
|---|---|---|---|---|
| Concrete Bunker | 8 s | 11 s | 93 | 0.30 |
| Ceramic Chamber | 6 s | 8 s | 56 | 0.00 |
| Corrugated Silo | 8 s | 10 s | 191 (mostly creaks) | 0.00 |

## 9. Open items

* The plugin builds against JUCE 8.0.4 with no warnings in project code, passes the headless smoke
  test, and passes `auval -strict` (`aufx RmBk Rbrk`, 0 warnings, ~1 ms warm open). On macOS, the
  CMake file ad-hoc signs the AU and Standalone bundles after each build. JUCE leaves them signed
  on the binary only, which `codesign -v` rejects. It has not been run through pluginval or tested
  in a DAW.
* All voicing constants are first-pass values: material tables, wet gains, phase curves. Tune them by
  ear with the WAV renders and the WallMonitor.
* Oversampling switches hand over to a fresh engine: the old one gets silence and fades over 1.2 s,
  so its tail is not carried into the new rate.
* Windows: CI (GitHub Actions, `windows-latest`, MSVC) builds the VST3 and Standalone and runs both
  test programs. The plugins are unsigned, and Windows has not been tested in a DAW.
* The info line follows the mouse only; keyboard focus does not drive it. Knobs deliberately take
  no keyboard focus (so the host keeps its shortcuts), which also means no arrow-key adjustment.
* Not yet tried by hand in a DAW: the hidden-pointer drag (`enableUnboundedMouseMovement`), the
  host context menu items, and text entry in hosts that keep keyboard focus for themselves.
* SIMD is left to the compiler: the hot loops are written as element-wise passes over arrays (tank
  filters, tap mixes, debris and impact voices). Hand-written intrinsics could still gain on the 64
  scattered delay reads per sample in the tank, which is now its largest cost.
* In the tank, Catmull-Rom reads lose up to 1 dB at 12 kHz per pass at 48 kHz when a line sits
  between samples. The materials' own damping is several times larger, and the loss vanishes at
  2x/4x, so it is left alone.
* Possible additions: preset morphing between materials, MIDI-triggered demolition (DETONATE is
  already automatable), A/B compare and undo in the header, a "lock Mix while browsing" option.
