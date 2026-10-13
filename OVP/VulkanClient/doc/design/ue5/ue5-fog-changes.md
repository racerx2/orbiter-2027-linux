# UE5 batch: height and valley fog (HeightFog), the fog owner's changes

Repo: /home/racerx/Projects/Orbiter-linux-custom, branch ue5-batch (from 35e2949c). Not committed. Design: design-height-fog.md, with "Rev 2 (implementation)" at its end (Rev 2 overrides the body).

## Files
### Own files
- **OVP/VulkanClient/SceneFxFog.cpp** (was a stub). `struct SceneFx::FogState` holds:
  - the IPI and its targets: tFH[2] (half-res RGBA16F) and tFD (half-res R32F);
  - the map textures tMap[near, far][2 slots] (RGBA16F 256²) and tCld (R8 256² cloud coverage);
  - the CPU map copies, the GfxPool box and the grid cache.
  - Its helpers are FogState members, because SceneFx.h is the integrator's file.
- **SceneFxFog.cpp functions:**
  - `InitFog`: targets, textures and the 3 shader entries at scene start. On failure: one LogErr and the fog stays off.
  - `FreeFog`.
  - `FogAndClouds` (INT-5): fog off → only `if (clouds) Clouds(...)`, as before. Fog on: camera above `cloudalt + 500` with clouds → fog, then clouds; otherwise clouds, then fog.
  - `FogState::Update`:
    - checks: atmosphere, not PLT_GIANT, surfmgr2, scatter constants and all 5 tables (review SHOULD 6), depth valid, below 45 km;
    - collects finished bakes into the free texture of each pair (a 1 s cross-fade; a bake that arrives mid-fade waits);
    - a bake with < 90 % coverage is refused and asked again in 1 s (NIT 8);
    - rebuild requests through `FogMath::NeedRebuild`, plus a grid-signature check every 1 s (near) or 2 s (far);
    - `FogMath::Frame` → per-frame params; wF (12-30 km fade, 2 s ramp, deck fallback);
    - the CPU sun transmittance;
    - takes the rotation sense from dλs/dt (NIT 6).
  - `FogState::Grids`: the ready elevation grids via `ElevGrids`, at level 9 (near) or 7 (far), one level coarser above 25 tiles. Cached by horGen, so only new tiles are copied (NIT 10); entries unused for 60 s are dropped.
  - `FogState::Submit`: one GfxPool::FOGMAP job per cascade. The far job also fills the cloud coverage map (`VolTiles` + `VolCloud::Fill`) and its mean within 50 km.
  - `FogState::Draw`: uniforms (map frames ×4, cloud frame, planet axes + drift, params, shell, march), then PSFogMarch (MRT tFH[0] + tFD) → PSFogBlur → PSFogComposite (`Execute(1, true)`). Each entry binds only the samplers it uses (checked by reflection).
  - `FogAlongRay`, `FogTowardSun`, `FogTex`, `FogDist`: see "For other owners".
- **OVP/VulkanClient/FogMath.h/.cpp** (pure, no client types; the GLSL mirrors the density, the layout and the noise):
  - Plane (gnomonic tangent frame);
  - Map + Sample (bilinear as the GPU);
  - Density, DensitySeg (closed-form mean over a step: lid smoothstep integral, exponential haze), Bound, BoundLid/SeaLid, LidTop;
  - Hash3/Drift (Teschner primes + Murmur3, 700 m cells, x periodic over 64 cells);
  - RayAlt (float-stable), ScanPoints, MakeLayout (16 asinh scan intervals, split at the ray's lowest point and the bound-lid crossings, weights `(1-e^-τ)e^-0.5Στ + 0.02 len/total`), Steps, Transmittance;
  - Sun/SunTime, Diurnal (peak and trough scale with night and day length; polar constants blended over the last hour), FogDayProb, DayNoise (variance-normalised 4D value noise through Φ: near uniform everywhere), FogDay, SeaDay, Frame;
  - Bake: 257² corner samples from the finest covering grid; Floor (min r2 km, box r1.5 km, fractional radii so the cascades match, never above the ground); Water (sea h ≤ 0.5 m, flat lakes); ShadowSweep (Timonen-Westerholm on the tangent plane, upstream from the far map); a fog-day lattice; stats;
  - GridHeight, TileRange (used by Surfmgr2's ElevGrids), ToHalf, NeedRebuild.
- **OVP/VulkanClient/shaders/HeightFog.glsl** (new, compiled only with HeightFog = 1):
  - block FogPS at binding 2 (29 vec4);
  - samplers 13-19 plus Scatter's tSun, tLndRay/Mie/Atn, tAmbient;
  - PSFogMarch: shell clip, MakeLayout, 16 lit steps. Each step's density is the closed-form mean over its altitude range. The light is also means over the step (Wrenninge 3 octaves through the flat layers, terrain shadow Hs, fog-top occlusion), with the sun colour and the 2D-deck cloud shadow at the step middle. Only the fields and the drift noise use the jittered point;
  - PSFogMarch output: haze to the mean fog distance as TerrainPS, HDR, premultiplied (×wF ×deck mask); MRT 1 = the fog distance;
  - PSFogBlur and PSFogComposite as VolBlur/VolComposite.
- **Tests/FogMath.Test.cpp**: 14 cases, 75698 assertions (list below).

### Shared files (edit_block, small regions)
- **Surfmgr2.cpp**: `#include "FogMath.h"`, and `TileManager2<SurfTile>::ElevGrids` after HorizonAt. It takes the loader mutex, walks the box's tiles at one level (else the finest ready ancestor), and returns the keys (horGen) and grids. Heights are copied only when asked and not cached. It returns -1 above maxn.
- **Tilemgr2.h**: the ElevGrids declaration, and `struct Grid;` added to the `namespace Horizon` forward declaration.

### Integrator / other files touched (smallest edits, listed for the integrator)
- **SceneFx.h**: one line, `VkTex *FogDist () const;` (the plume contract's fog-distance target, INT-5). No other change; the FogAlongRay/FogTex signatures are unchanged.
- **Scene.cpp** RenderGlares: `if (pFx) glare *= pFx->FogTowardSun();`. With HeightFog = 0 this is × 1.0.
- **Cloudmgr2.cpp:349**: `|| Config->HeightFog` in the density condition, so the cloud tiles keep a density for the fog's deck mask, f_clear and the cloud shadow even with VolClouds and LensFlare off. With HeightFog = 0 it is unchanged.

## Switch off
- `fog` is NULL: FogAndClouds calls only `Clouds`, as before. Nothing is created and HeightFog.glsl is not compiled. FogTowardSun returns 1 (glare × 1.0). Cloudmgr2 is unchanged.

## Tests
- **Build:** /tmp/earth/fog/b7.log BUILD EXIT=0 (after removing the temporary timestamp code: no `TEMP`, `getenv` or query pool left in SceneFxFog.cpp; no fog warnings).
- **Unit test** (build-tree binary Tests/FogMath.Test, not ctest): all passed, 75709 assertions in 15 cases (planes and tile ranges; grids finest-first and across the date line; sunrise and local time; diurnal; floor and valley depth; shadow sweep; water; step layout and transmittance; sun transmittance through a flat fog; fog days; frame parameters; half floats; rebuild thresholds; drift noise; bake on a synthetic valley).
- **Offline shader compile** (/tmp/earth/fog/glsl.sh, VkShader's preamble, glslangValidator vulkan1.3): PSFogMarch/Blur/Composite rc=0, with and without `_PERFORMANCE _FILMIC`.
- **Run fogA1** (Gfx Fog Alps Dawn, HeightFog=1, other UE5 switches 0, 16:24 code): far map 8.4 ms bake, cover 1.00, floor 162 m, mean ground 1039 m; near map 16.3 ms, cover 1.00. Shot: the valleys filled with a flat-topped fog, ridges and the peak out, warm dawn sky, no seams. It showed a fine IGN dither in the fog, which the later step-mean lighting (patch6) addresses. Client log: 0 shader errors; the 84 `[ERROR]` lines are the exit-time "UnDeleted Surfaces" list, identical in every run of every owner (ssrW0, giMid0, plmlook1), not from the fog.
- **Finish-phase runs** (queued 16:57 as fogF1 Alps Dawn fog on, fogF2 Coast Dawn fog on, fogF0 Alps Dawn fog off; shots at 8, 16, 28 s, other UE5 switches 0). The queue was blocked 16:57-17:14 by an Orbiter on the user's desktop (:0) from the Custom tree, then the machine rebooted at about 17:17, which wiped /tmp/earth (build.sh, run.sh, shots, logs). Only fogF1 ran (final code, 17:14):
  - client log: 0 shader errors, 84 `[ERROR]` exit-time surface-leak lines only (as every run); first far map 8.9 ms, near 16.9 ms, both cover 1.00;
  - 8 s (sim 4 s): terrain not loaded yet (flat). The first far map was baked from these grids: floor 0 m, mean ground 124 m, water 0.238 (Alps: wrong, flat or zero heights accepted as ready). The fog then sits as a low sheet over the flat ground;
  - 16 s: correct: valleys filled to a flat top, ridges out, warm dawn sky, no seams; the IGN dither is now faint;
  - **28 s: defect**: in the foreground valley the fog is missing over a region bounded by two straight lines meeting at a corner (a tile or map rectangle), with the town under it clear, where the 16 s shot had fog. A rebuild between 16 and 28 s baked part of the map from wrong heights.
  - fogF2 (coast) and fogF0 (off comparison) did not run.

## Open items
- **Defect seen in fogF1 at 28 s (fog's own code, being fixed in the review-fix phase):** a later bake drops the fog over a straight-edged region of the near valley. Suspects, in order: (1) the bake accepts grids from tiles whose heights are interpolated from a coarse ancestor (TileInterpolatedFromParent) or flat-offset, and the horGen cache keeps them; a finer tile with its own file then sits next to them, and the floor filter (min r2km) on the coarse side lies below the real valley floor, so the lid is under the terrain; (2) a level-mixing error in the finest-first GridHeight at tile edges. Check: log per bake the grid levels and a height probe at the camera, compare the 16 s and 28 s maps.
- **Not run:** Coast Dawn (sea fog) and the HeightFog=0 comparison (the queue was blocked, then the reboot). The off path is unchanged by construction (fog NULL → `if (clouds) Clouds(...)`, no shader compiled, FogTowardSun 1.0, Cloudmgr2 unchanged), but not re-verified by a shot in this phase.
- **Not measured:** GPU and CPU cost (the temporary timestamp code was removed before the runs could start; the plan was FOG_GPUTIME runs).
- Otherwise none in the fog's own files. Not done by design (13): exhausts, particles, beacons and glass are not fogged; city lights do not glow in fog; the 2D deck mask needs the fog's own coverage map (else the wF fade over cloudalt..+500 m).
- Calibration constants (σf, D0, kv, σh, Hinv, ambient gain 2, phase 0.75/0.25, g 0.7) are first values, to be art-tuned (HeightFog default 0).

## For other owners
- **plumes (PostLook owner), PostLook.cpp:236** (flare, design 11 and Rev 2 "Glare"): not done yet. The exact change: `flare = float(0.25 * edge * ter * (1.0 - cld) * mx * e * (scn->pFx ? scn->pFx->FogTowardSun() : 1.0f));`. FogTowardSun() is valid in the frame the fog drew (PostLook::Final runs after AfterOpaque) and 1.0 with HeightFog = 0.
- **plumes, PostPlume.cpp:1126-1233**: the composite uses FogTex() and FogAlongRay() but not FogDist(). Rev 2 MUST 4's contract is `w = smoothstep(0.75·tf, 1.25·tf, t_plume)` with tf = FogDist() at the plume pixel (half-res R32F, 0 = no fog), `rgb' = P.rgb·(1 − αf·w) + FogTex.rgb·w·P.a`. Without tf a plume in front of a distant fog bank is fogged as if behind it.
- **ssr**: SceneSsr.cpp:235 reads FogTex() to strip the fog from hit radiance: `hit_clear = (hit − F.rgb)/max(1 − F.a, 0.05)` (Rev 2). FogTex is NULL unless the fog drew this frame.
- **integrator**: SceneFx.h one line (FogDist), Scene.cpp RenderGlares one line (× FogTowardSun), Cloudmgr2.cpp:349 one token (`|| Config->HeightFog`). Docs (design 9: VulkanClient.html, README.md, NEW.tex, LAUNCHPAD.tex) are the integrator's. Build-tree scenarios Scenarios/Tests/Gfx Fog Alps Dawn/Coast Dawn/Plateau Dawn/Alps 20km.scn are not in git.


## Review fixes
Review: code-review-ue5-fog.md (MUST 1, SHOULD 3, NIT 6). All in the fog's own files (SceneFxFog.cpp, FogMath.h/.cpp, HeightFog.glsl, Tests/FogMath.Test.cpp); no shared file touched.
- **MUST 1 (wrong or missing heights give fog-free rectangles), fixed in code, not yet re-run:**
  - Grids(): a new grid is validated before it is cached or used: 5 inner nodes against `oapiSurfaceElevation`, tolerance max(50 m, 10 % of the grid's range), 3 of 5 must agree. Flat or zero placeholders (the 8 s map: floor 0, "water" 0.238) are refused. A refused grid is not cached; the cascade's signature is set to 0 and the next check (5 s later) asks a new bake.
  - Uncovered texels now get m = −1 (FogMath::Bake). HeightFog.glsl Fields() and FogState::Field use `wn *= saturate(1 + n.w)`, so near-map holes fall back to the far map instead of a fog-free rectangle. Every density/bound use clamps m at 0, so the far map's holes are still fog-free (and a far map under 90 % cover is refused).
  - Each bake logs (first 3, and any with refusals): grid count, level range, refused count, the core's ground at the centre.
- **SHOULD 1 (poles), fixed:** new `FogMath::GridBox` (a spherical cap's exact longitude half-width asin(sin d / cos φ), all longitudes when the cap holds a pole) used by Grids(). A refused bake now backs off 1 → 2 → … 30 s, reset when a map is accepted or the camera moves 0.2 × half side. Test "planes and tile ranges" adds 89.0/89.3/89.5/90/−89.7° × both cascades × the date line: every texel corner inside the box.
- **SHOULD 2 (FogAlongRay cost), fixed:** Transmit clips [0, dist] to the shell exactly as Shell() (returns 1 for an empty stretch). rIn/rOut are computed in Update before sunT (Draw uses them, so the first frame is right). Scratch bench (/tmp/earth/fog/bench.cpp, this machine, -O3, two-map field as Field): 3.3 µs per call with 16 steps, 2.3 µs with 8; 130 calls ≈ 0.43 vs 0.30 ms. FogAlongRay now uses 8 steps; FogTowardSun keeps 16. The in-game 600-frame log was not possible (no runs).
- **SHOULD 3 (runs), not done:** the shared run infrastructure (/tmp/earth/vfx/run.sh, /tmp/earth/cs/cfgrun.sh, Xvfb :58) has not existed since the 17:17 reboot, so the Alps Dawn re-run (MUST 1 check at 8/16/28 s), Coast Dawn and the HeightFog = 0 comparison, and the GPU timing are still open. To do once run.sh is back: `run.sh "Tests/Gfx Fog Alps Dawn" 30 fogR1 "8 16 28" HeightFog=1 SceneShadows=0 SSR=0 VolPlumes=0 SSGI=0 NightLook=0`, the same with "Tests/Gfx Fog Coast Dawn" (fogR2) and with HeightFog=0 (fogR0); check the "height fog … bake" lines for refused grids and the 28 s shot for the rectangle.
- **NIT 1, fixed:** a failed InitFog frees the state, `fog = nullptr`.
- **NIT 3, fixed:** the LVL comment says the far map is mostly level 6 at mid latitudes.
- **NIT 4, fixed:** when Grids() has not run for 60 s, Update drops the grid cache and the previous maps.
- **NIT 2 (integrator, SceneFx.cpp:281 `more`), NIT 5 (deck map rebuilt with the far map, 20 km / 120 s instead of 60 s; harmless with a static cloudrot), NIT 6 (PostLook flare, PostPlume FogDist; see "For other owners"):** recorded, not in the fog's files.
- **Checks after the fixes:** build BUILD EXIT=0 (/tmp/earth/fog/b8.log, through the shadows owner's stand-in build script with the shared /tmp/earth/vfx/build.lock, since vfx/build.sh was lost; no fog warnings); syntax check of SceneFxFog.cpp with the tree's flags clean; Tests/FogMath.Test all passed, 180253 assertions in 15 cases; HeightFog.glsl compiles (6 variants, glslangValidator); the build tree's HeightFog.glsl matches the source.
- **State at 17:39 CDT:** run.sh, cfgrun.sh and Xvfb :58 are still missing, so the fog's three verification runs (fogR1/fogR2/fogR0 above) remain the one open item. The code, build and unit tests are final.

