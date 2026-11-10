# RoomBreaker — Dynamic Structural Destruction Simulator

A reverb whose six boundaries (North, South, East, West, Ceiling, Floor) crack, loosen and
breach under the mechanical stress of the incoming audio, then heal (or don't).

## 1. Project layout

```
CMakeLists.txt                 JUCE 8 via FetchContent; RB_CORE_ONLY=ON builds core + tests only
Source/
  Parameters.{h,cpp}           APVTS layout + wait-free ParameterCache::snapshot()
  Presets.{h,cpp}              28 factory presets (host programs + editor preset bar)
  PluginProcessor.{h,cpp}      AudioProcessor: buses (+ sidechain), oversampling, gains, limiter, state
  PluginEditor.{h,cpp}         Header, Room page (house + controls), Engine page, info column
  UI/Theme.h                   Palette tokens, platform fonts (DIN / Bahnschrift), LookAndFeel
  UI/Widgets.h                 Knob (3 sizes) / ChoiceBox / Segmented / Chip / MomentaryButton
  UI/Help.h                    Info column text for every control and display
  UI/LiveState.h               UI-side copy of the telemetry: frame, flashes, fractures, history
  UI/HouseView.{h,cpp}         The room as a house: damage generation, debris, camera, mouse (see 5e)
  UI/HousePaint.cpp            Everything the house view draws
  UI/HouseModel.h              The house's measurements, materials and outsides (private to the two above)
  DSP/                         JUCE-free, real-time-safe core (compiled and tested here)
    DspUtils.h                 TPT one-pole / SVF, Catmull-Rom delay, allpass, xorshift RNG
    Materials.h                11 material profiles (3 core + 8 dark cinematic) + impact voicing and blurbs
    Voids.h                    7 exterior void profiles (what lies beyond a breach)
    WallState.h                Wall enum, damage-phase curves, FractureEvent
    StressTracker.{h,cpp}      Kinetic Stress Analyzer + integrity model
    EarlyReflectionEngine.*    24-tap shoebox image-source ER with jitter + scatter
    DynamicFDN.{h,cpp}         8-line Householder FDN with bleed + morphing loop filters, ExteriorTank
    ModalDebrisGenerator.*     32-voice complex-resonator debris, chatter, contact rattle
    ImpactGenerator.{h,cpp}    Fracture weight: sub boom, crack transient, rubble grains + rumble bed
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
* **Click-free automation.** Every user control glides: Size (log glide, 0.4 s), low/high cut
  (log glide, 50 ms), tone and wet/width/duck gains (per-sample linear ramps between ticks), drive
  (blend ramp, so 0 % is bit-exact bypass but switching it on never steps), density (allpass gain
  ramped per sample). Pre-delay changes do not move the read head: two taps crossfade over 40 ms.
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
+ 6 second-order on opposite walls (both orders) = 24, all read from one Catmull-Rom delay line.

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

### 4.4 Late Tank — `DynamicFDN`

* 8 lines of 31.7–79.3 ms × Size × (1 + 0.12·breach). Loose lines wobble slowly ("creak").
  Each line belongs to two walls (N–S, E–W, C–F, N–E, S–W, C–N, F–E, W–C).
* **Matrix with diagonal bleed:**
  `A(b) = (1−b)·H + b·I` with `H = I − (2/N)·11ᵀ`, applied as `y = x − (1−b)(2/N)Σx` (O(N)).
  The eigenvalues are 1 (N−1 times) and 2b − 1, so `|λ| ≤ 1` for every b. As walls breach, the lines
  decouple into combs and the uniform mode turns lossy, but the matrix never adds energy.
  `b = clamp(0.9·meanBreach + 0.3·meanLoose, 0, 0.95)`.
  Per-row bleed (`diag(1−b_k)H + diag(b_k)`) can have gain > 1, which is why `b` is one global value.
* **Loop filter per line, morphing in three stages:**
  1. **High-shelf damping** (TPT one-pole shelf). Fades out as `max(loose, breach)` rises.
  2. **Loose-slab bell** at the material's per-wall slab frequency:
     `(1 + (G−1)·k·BP)/G` with `G = 20·loose dB`. Peak gain is exactly 1 and everywhere else 1/G, and
     the line gain is stretched to `g_RT^(1−0.6·loose)`. The slab mode rings on while the rest of the
     tail dies.
  3. **High-pass venting**, 12 Hz (a DC guard when intact) up to 912 Hz when void, with broadband
     loss `× (1 − 0.65·breach)`.

  Each stage has |H| ≤ 1 and the line gain is < 1, so the loop stays stable in every state. The test
  harness checks this in each damage phase at Decay 12 s and Size 2.
* **Exterior void.** Each line's vented energy, `breach·(x − (1 − 0.65·breach)·hp)`, is summed into
  the `ExteriorTank`, a 4-line FDN configured by a `VoidProfile` (`Voids.h`). Free Field Loss
  discards it: the tank's input ramps to 0 over one tick, and whatever is already outside rings out.
  * Line lengths glide over ~150 ms when the void changes, with a slow per-line LFO for water or
    heat-haze wobble.
  * Loop gains come from the void's RT60, followed by an in-loop low-pass for darkness/distance.
  * The matrix is `A = I − d·(2/N)·11ᵀ`, with eigenvalues 1 and 1 − 2d, so |λ| ≤ 1 for any
    diffusion d. Low diffusion decouples the lines into discrete echoes (Concrete Canyon) or flutter
    (Industrial Tunnel).
  * Grit (Burning City) is an asymmetric tanh, `(tanh(drive·x + 0.25) − tanh(0.25))/drive`, applied
    **outside** the loop so it can never destabilise the tank. It is followed by a high-pass that
    also removes the shaper's DC.

  | Void | Lines (ms) | RT60 | Damp | Diffusion | Wobble | Grit | Character |
  |---|---|---|---|---|---|---|---|
  | Free Field Loss | — | — | — | — | — | — | Vented energy is lost; a breached room just gets drier |
  | Infinite Secondary Tail | 114–242 | 45 s | 1.4 kHz | 1.0 | — | — | Distant, endless wash |
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

### 4.6 Impact — `ImpactGenerator`

The modal debris gives a fracture its pitch; this layer gives it weight. Voiced per material by
`MaterialVoicing` (boom pitch and decay, crack band and length, rubble band):

* **Boom**: a sine whose pitch falls an octave (f0 → f0/2, τ ≈ 90 ms) with an exponential decay
  and a touch of 2nd harmonic. It is mono, so the low end stays solid. Walls failing in the same
  40 ms are one collapse: they merge into one boom (energy sum, capped) instead of stacking coherent
  sines. That merge took the hottest preset peaks from +3.5 dB to +2.3 dB.
* **Crack**: band-passed noise burst at the material's crack frequency, panned to the wall, plus a
  smaller splinter on big hits.
* **Rubble**: Poisson noise grains plus a low rumble bed, both driven by a rubble energy that every
  fracture adds to (it settles over 2.5 s). Loose walls under load keep it trickling.
* **Shock** (in `Engine`): a fracture envelope (τ 0.35 s) that stretches every FDN line by up to
  1.6 %, so the room's pitch flexes on big hits.

Impact scales boom, crack and shock; Rumble scales the rubble. DETONATE (`StressTracker::detonate`)
removes a material-scaled chunk from every wall and emits a full-magnitude event per wall.

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

The v2 parameters use `ParameterID` version hint 2, so hosts keep the v1 automation order. Presets
set the sound controls (Impact through Ducking). They leave Oversampling, Input/Output Gain, the
limiter and Stress Source alone. A v1 session loads with the v2 controls at their defaults.

Size, Decay and Debris Level go beyond the original spec. A reverb needs them to be usable.
`ParameterCache` caches the `std::atomic<float>*` handles once, and `snapshot()` reads them with
relaxed loads.

## 5b. Factory presets

There are 28 presets, defined in `Source/Presets.cpp`. They appear in the editor's preset bar
(grouped menu, ‹ › buttons, one-line description) and in the host's own preset menu (AU factory
presets / VST3 programs). Loading a preset sets every parameter and gives you a fresh room:
pristine, or the preset's own starting damage for the presets that start as ruins. The session remembers
which preset it was based on.

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

1280 × 800 design size, scaled with a fixed aspect ratio (60–160 %). Three columns: the house and
its read-outs (600 px), the sound controls (400 px) and the info column (280 px).

* **Header**: wordmark, Room / Engine tabs, preset menu with ‹ ›, Detonate and Rebuild, Input /
  Dry/wet / Output, Oversampling (segmented), Freeze (switch), engine status and In/Out meters.
* **Room page**: the house (5e), a six-column wall survey (integrity and load vs yield per surface)
  and a 12 s stress history. On the right: Material and Outside, then four rows (Structure,
  Destruction, Space, Tone), each led by one large knob, and the preset notes.
* **Engine page**: a plain settings sheet. Oversampling, Safety limiter, Ducking with the live room
  level, and What breaks the walls (Input / Sidechain / Both) with the key meter.
* **Info column** (every page): explains whatever the mouse is over, using each widget's `info` key,
  `Help.h`, the live value and default. Otherwise it shows the room's status and how to read the house.

**Visual rules** (written against the common "AI-made UI" complaints: purple gradients, glowing
cards, coloured side bars, emoji, rainbow status colours, every control the same size):

* One accent colour, hot orange, and it means destruction only (Detonate, fractures, overload).
  Everything else is warm greys plus one ink colour.
* No cards, no drop shadows on panels, no coloured stripes. Sections are a sentence-case heading
  and a hairline.
* Hierarchy by size: one large knob per row for the control that matters most.
* Every option visible where there are few: segmented buttons instead of menus for 2–3 choices,
  switches for on/off, text tabs with an underline.
* Text contrast ≥ 4.5:1 against its panel. Fonts are platform native (DIN on macOS, Bahnschrift on
  Windows), not a web default.

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
atomics. A wrecked or permadeath room reopens exactly as it was left. `setStateInformation` strips
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
| CPU (core, 1x) | ~120× real time, one core, Apple Silicon (v1: ~184×; the new stages cost the difference) |
| CPU (plugin, 10 s noise) | 1x 159×, 2x 79×, 4x 39× real time |
| Quality: Motion | tail ringing 5.0 → 4.4 → 4.1 dB (Motion 0 / 25 / 60 %) |
| Quality: Density | echo density 30–120 ms 0.30 → 0.60 (Density 0 / 70 %) |
| Sanitizers | core harness under ASan + UBSan: all checks pass, 0 reports |
| Rate independence | mean integrity on the same script 0.306 / 0.305 / 0.304 at 48 / 96 / 192 kHz |
| Automation clicks (test 11): 17 controls jumped mid-signal | worst step ≤ 3× clean (Drive 0→100 % ≤ 6×, Size ≤ 4.5×, Material ≤ 4×) |
| Fuzz (test 12): 40 s of random parameter jumps on noise and hits | finite, bounded |
| `auval -strict -v aufx RmBk Rbrk` (installed AU) | AU VALIDATION SUCCEEDED, 0 warnings, 28 factory presets listed |

Plugin smoke test (`rb_plugin_smoke`, the real `AudioProcessor`):

* **Parameters and buses:** 28 parameters with correct defaults and display text. Stereo,
  mono-to-stereo and a stereo sidechain are accepted; 5.1 is rejected.
* **Processing and state:** random host block sizes (1–1024) stay finite and bounded. Damage and
  parameters survive save/recall. Rebuild fires once on its rising edge.
* **Presets:** every preset renders a 10 s audition. The limited peak stays ≤ 0 dBFS, and the
  pre-limiter peak stays < 2 (worst 1.14).
* **New controls:**
  * DETONATE damages every wall, fires once and is audible on silence.
  * FREEZE holds the tail (86 % after 7 s), and release lets it decay.
  * The limiter holds a +12 dB detonation at 1.000.
  * Oversampling lowers aliasing and switches click-free (largest step 0.041) while keeping the room's damage; the old tail fades over 1.2 s.
  * A sidechain key breaks the room while Input mode ignores it.
* **Editor:** it builds and renders six snapshots after pumping audio through the plugin:
  pristine, damaged, detonated, from behind, info column and engine page. It then poses the house
  at set damage levels in every material for the gallery and times a repaint.

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
* The INFO panel follows the mouse. Keyboard focus does not drive it yet.
* SIMD: the 8-line FDN and the 24 ER taps map directly onto 4- or 8-wide vectors if CPU ever matters.
* Possible additions: preset morphing between materials, MIDI-triggered demolition (DETONATE is
  already automatable), user preset save/load.
