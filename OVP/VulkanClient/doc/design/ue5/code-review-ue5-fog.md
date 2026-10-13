# Code review: height and valley fog (HeightFog), UE5 batch

Repo Orbiter-linux-custom, branch ue5-batch.
- **Scope:**
  - `git diff 35e2949c` on the fog owner's files. SceneFxFog.cpp, FogMath.h/.cpp, shaders/HeightFog.glsl and Tests/FogMath.Test.cpp were read whole.
  - Surfmgr2.cpp ElevGrids and Tilemgr2.h.
  - The listed one-liners: SceneFx.h FogDist, Scene.cpp:3993, Cloudmgr2.cpp:349.
- Paths are under OVP/VulkanClient/ unless shown otherwise.
- Read-only review: nothing was built or run.
- **Which tree:** the tree as marked `<!-- DONE -->` (about 17:20). After that, the owner had already started on SHOULD 1-2 (FogMath::GridBox, the retry back-off, the shell clip in Transmit at SceneFxFog.cpp:181-193). Those are noted where they apply. Line numbers are from the DONE tree.

**Verified.**
- **Switch off (HeightFog = 0).**
  - SceneFx.cpp:42 makes no FogState, so `fog` is NULL.
  - FogAndClouds is then `if (!fog || ...) { if (clouds) Clouds(target, vp, wV); return; }`, which is the integrator's stub: no macro, no pass, no buffer, no job.
  - HeightFog.glsl is compiled only in InitFog.
  - FogTowardSun returns 1.0f, so `glare *= 1.0f` (Scene.cpp:3993) is exact.
  - Cloudmgr2.cpp:349 and Scene.cpp:1605 are unchanged when the key is 0.
  - The temporary timing code (FOG_GPUTIME, the query pool, EndRendering, the chrono on the off path) is gone.
- **Fog maths, checked by hand against the design and Rev 2.**
  - Plane: the U/E/N axes, gnomonic ToMap/FromMap, row 0 north in Bake, Sample and MapUV.
  - Map frames:
    - Axes() turns the planet-local frames to geocentric global axes with mul(R, ·), U.w = R and E.w = 1/half, so MapUV gives the same r as Map::Sample;
    - the cloud frame is turned by cloudrot exactly as SceneFx::VolParams:424, with Rp as VolCloud.glsl:72;
    - Const.CamPos is geocentric, as the frames are.
  - Height profile:
    - RayAlt is the float-stable form;
    - LidMean is the closed form of the smoothstep's integral (SInt = u³ − u⁴/2) with the small-dh branch;
    - ExpMean is right on either side of the ground, and ExpBelow is its mirror;
    - Bound ≥ DensitySeg for every n: (0.6 + 0.8n) ≤ 1.4, (0.5 + n) ≤ 1.5, L + Δ ≤ F + 1.4D + 8 < BoundLid, Ls + Dls ≤ SeaLid.
  - Segment scan:
    - Split cuts each interval at the BoundLid and SeaLid crossings, so Bound's mid-altitude test cannot miss a lid inside an interval;
    - `extra` caps the layout at MAXP = 25, so lt/lw never overflow;
    - Steps and the GLSL cumulative search are the same code, the edges tile [t0, t1], and the last edge is exactly t1;
    - the CPU and GLSL layouts match line for line.
  - Sun transmittance and light:
    - the sun column (haze σh·Hh·e^−…/μs plus the two 0.8·ExpBelow columns) and the 3 octaves with the phases once per pixel;
    - Vt = 1 − LidMean(Hs, 30) is the lit fraction, and DARK/LIT (±1e4) stay exact in half floats;
    - GetSunColor(·, h − MinAlt) and GetLandView(|pm|, n̂pm) have TerrainPS's conventions;
    - HDR() has only _FILMIC, which is passed;
    - Transmit (FogTowardSun) shares the layout and step code with the GPU march (test 8).
  - Shadow sweep: the upstream column and row shift (dj = −uN/|uE|), drop = dU·tan e0 and the r²/2R back-conversion. The near map's upstream heights come from the far map's Hs, moved to the near plane.
  - Diurnal: Smooth(0, night, b) is reached only when !up, so night > 0 and there is no 0/0 at the polar limits.
  - DayNoise: Σw² ≥ 1/16.
  - ToHalf: the overflow clamp and round-to-nearest-even.
  - Drift: the `& 63` wrap is the same in C++ and GLSL (two's complement, bit-preserving uint()).
  - The post-DONE GridBox: the cap half-width asin(sin dl / cos φ), and all longitudes once dl ≥ 90° − |φ|, is right.
- **Threading of the GfxPool jobs.**
  - The jobs capture only shared_ptr<Box>, shared_ptr<BakeIn> (shared_ptr<const Grid>, shared_ptr<const Map> outer) and VolCloud::Src values (shared_ptr densities). They never capture `this`.
  - So a job that is still running at ~FogState → Unregister is safe, and queued jobs go without dropped(). There is at most one job per cascade, so the per-kind MaxQueue never drops one.
  - Every box field is read and written under box->mx. A map is immutable once published.
  - ElevGrids copies `elev` on the render thread under loader->WaitForMutex(), with the elevReady acquire, as HorizonSubmit does.
  - horGen is a global atomic counter (Surfmgr2.cpp:113-115), so the cache keys are unique across tiles and planets.
  - Ancestors are de-duplicated, longitude wraps in NodeAt/ElevGrids, and grids in `have` come back without heights.
- **Resources and Vulkan state.**
  - Every VkTex/VkSurf/IPI is freed in ~FogState.
  - A map is uploaded only into the slot not bound this frame. The last frame's read of that slot is ordered by Upload's transition barrier, as tCov is.
  - MRT 1 is unbound after the march, also on failure.
  - Each entry binds only its own samplers. Unused map slots get the current map with weight 0, and tCldF gets tf without coverage.
  - AfterOpaque restores the main target, viewport and state after FogAndClouds (SceneFx.cpp:291-293), also when Draw returns early.
- **Edge cases.**
  - No atmosphere, PLT_GIANT, no SurfMgr2, a missing scatter const or any of the 5 tables, no depth, above 45 km: Update returns false and only Clouds run.
  - Planet switch: the obj checks, prev = NULL, the ramp reset.
  - Shell(): correct for a camera below r_in, above r_out, and for grazing rays.
  - MapUV never flips sign: all samples are within about 300 km of the map centres.
  - NaN guards: alpha ≥ 1e-3 before col/alpha, max(ts, 1e-6), μs ≥ 0.08, Δ ≥ 8 m, Dls ≥ 5 m, sqrt(max(·)). FogAlongRay rejects dist ≤ 0 and NaN.
- **Plume and SSR contract.**
  - FogAlongRay: global frame in, 1 − wF(1 − T) out, valid this frame or the last.
  - FogTex = tFH[1] (premultiplied, wF and the deck mask in) and FogDist = tFD (tw/ts, 0 = none), only in the frame the fog drew.
  - FogTowardSun is 1 unless the fog drew this frame.
- **Rules.** One-line comments only, `// not upstream` in the shared files. The hash (Teschner primes plus the Murmur3 finaliser), IGN, HG and ToHalf are written from their published definitions. No GPL or vendor sample code.

## MUST

1. **Bake, SceneFxFog.cpp:199 and 240-256, FogMath.cpp:545, 662, 732, 744: maps baked from wrong or missing heights are accepted, and they give fog-free rectangles.**
   - The owner's own run fogF1 shows it:
     - At 8 s, the first far map had cover 1.00, but floor 0 m, mean ground 124 m and water 0.238 in the Alps (real values: 162 m and 1039 m).
     - At 28 s, the near valley lost its fog over a straight-edged region while the town under it was clear.
   - **Why the code shows it this way:** two paths in Bake set m = 0 over whole tiles, and nothing checks the heights against the terrain that is drawn.
     - A texel at h ≤ 0.5 m is sea (FogMath.cpp:545), and sea gets `(1 − sea) = 0` strength (744). A grid that holds zeros or flat placeholders therefore becomes a tile-shaped "sea" with no radiation fog. This is the 8 s map: about 24 % "water".
     - Uncovered texels get m = 0 (732), and Collect accepts a map with up to 10 % uncovered (199). One tile that is not ready, or whose ancestor walk fails, is a fog-free rectangle anywhere in the near map, even under the camera.
     - The horGen cache (240-256) then reuses that grid in every later bake for as long as it is used.
   - **Fix (all three):**
     1. **Validate a grid before it is cached.** In Grids(), probe 5 nodes (the centre and 4 inner points) against the core's `oapiSurfaceElevation(hP, lng, lat)`.
        - Do not cache or use a grid that disagrees by more than max(50 m, 10 % of its own range).
        - Do not cache a grid that is flat while the core is not.
        - Such a grid is asked again on the next sig check.
     2. **Probe each bake at the centre.** Log the grid levels per bake (the owner's own suspect check).
     3. **Keep near-map holes out of the near map.** Uncovered near-map texels should fall back to the far map, not to m = 0:
        - mark them (for example m = −1);
        - in Fields() and FogState::Field, use `wn *= saturate(1 + n.w)` (NIT-sized code in both);
        - or require full coverage inside r < 0.8 for the near map.
   - **Re-run Alps Dawn** with shots at 8, 16 and 28 s. The 28 s rectangle must be gone, and the 8 s far map must not be used (or must show the real mean ground).

## SHOULD

1. **SceneFxFog.cpp:228: near the poles the grid box loses the far side of the pole. The fix is in progress (GridBox, back-off).**
   - **Problem:** `dg = min(PI, dl / max(0.02, cos lat))` keeps lng ± dg, even when the box reaches over the pole.
   - A box simulation of Plane::FromMap gives these uncovered fractions:

     | latitude | far map | near map |
     |---|---|---|
     | 89.0° | 5 % | 0 % |
     | 89.3° | 16 % | 1 % |
     | 89.5° | 23 % | 14 % |
     | 90° | 41 % | 89 % |

   - **Consequence:**
     - Above about 89.2°, every far map is refused (cover < 0.9) and baked again every second, so the fog never draws.
     - Between 89.0° and 89.2°, the maps have holes.
   - **Fix:** the post-DONE GridBox (all longitudes once the cap holds the pole) is right. Keep the back-off, and add 89.5° and 90° with the 1.415 box to test "planes and tile ranges".

2. **SceneFxFog.cpp:182, 515-520: FogAlongRay costs a full 17-point layout plus 16 steps per call, with no shell clip. PostPlume calls it about 100-130 times per frame. The fix is in progress.**
   - **Problem:** PostPlume.cpp:511 calls it per plume (up to 32). GatherPuffs (752, 784, 817) calls it per column, per sample up to 96, and per ground cloud.
     - Each call runs 17 Fields through std::function, each 1-4 bilinear samples, then 16 × (Field + 8-hash Drift + DensitySeg).
     - That is about 8-15 µs, so 1-2 ms of render-thread time per launch frame with fog. The design's fog CPU budget is ≤ 0.05 ms per frame, and it did not count these calls.
   - **Fix:**
     - The post-DONE shell clip in Transmit (181-193) matches Shell(). Make sure rIn/rOut are set before Update's sunT (line 427 in the new tree), not only in Draw. Otherwise the first fog frame clips against a zero shell, and every later frame uses the last frame's shell.
     - Then log the mean FogAlongRay time over 600 frames in a launch with fog, as PostPlume logs Collect.
     - Use 8 steps for these calls if the time stays above about 0.2 ms.

3. **Tests the finish phase requires are missing (fog notes "Not run" / "Not measured").**
   - Coast Dawn (sea fog) and the HeightFog = 0 comparison shot did not run. The GPU and CPU cost was never measured: design 10's ≤ 0.6 ms GPU target is unverified, and the march does about 17 + 16 Fields with up to 4 taps each per half-res pixel.
   - The off path is right by construction (verified above), but the rules ask for the shot.
   - **Fix:** in the fix phase, with at most 3 runs:
     - Alps Dawn on at 8, 16 and 28 s (MUST 1);
     - Coast Dawn on;
     - Alps Dawn off at the same times.
     - Time the three passes with a temporary timestamp pair or the frame interval (on and off), then remove the timing again.

## NIT

1. **SceneFxFog.cpp:151-152: a failed InitFog keeps a dead FogState.**
   - Design 8 says `fog = nullptr` on failure. Here the targets and map textures stay allocated, and the non-NULL `fog` keeps AfterOpaque's `more` true (SceneFx.cpp:281), so the DepthPass runs for no reader when TAA, SSR and VolPlumes are off.
   - **Fix:** `if (!s.ok) { LogErr(...); FreeFog(fog); fog = nullptr; }`.

2. **SceneFx.cpp:281 (integrator line): `more` includes `fog` without altitude or body.**
   - With HeightFog = 1, the DepthPass runs in orbit and on airless bodies, where Update returns false. SSR = 1 and VolPlumes = 1 make `more` true anyway today, so there is no cost at the defaults.
   - **Fix:** a FogState predicate (atmosphere, not giant, hc < 45 km), for when those defaults change.

3. **SceneFxFog.cpp:30, 235: at mid latitudes the far cascade runs on level 6.**
   - At 45° the level-7 box is 4-5 × 6 tiles, over the 25 cap, so it falls to level 6: 1.2 km nodes, about the 1.17 km texel.
   - **Fix:** correct the "610 m nodes" comment, or allow more far tiles (they are cached).

4. **SceneFxFog.cpp:247, 363: the grid cache and the CPU maps stay after the fog stops.**
   - Entries are dropped only inside Grids(), which no longer runs above 45 km. Up to about 50 × 268 KB of grids plus 4 MB of float maps stay for the session.
   - **Fix:** clear them when Grids() has not run for 60 s. The new gridUse member looks meant for this.

5. **SceneFxFog.cpp:213, 286-291: the deck coverage map is rebuilt only with the far map (20 km or 120 s).** Rev 2 SHOULD 3 says 20 km in the cloud frame or 60 s. This is harmless with a static cloudrot.
   - **Fix:** note it in the design, or also ask a far bake after 60 s.

6. **Cross-owner, already in the fog notes (not fog's files):**
   - PostLook.cpp:236 has no × FogTowardSun(), so lens-flare ghosts stay at full strength with the sun in thick fog.
   - PostPlume does not read FogDist(), so a plume in front of a distant fog bank is fogged as if it were behind it.
   - The integrator should close or record both before the commit.

Counts: MUST 1, SHOULD 3, NIT 6.

