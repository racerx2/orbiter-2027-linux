# Design: night look (city lights on the ground, building windows, exposure at night)

Repo: Orbiter-linux-custom (/home/racerx/Projects/Orbiter-linux-custom), branch ue5-batch (HEAD 35e2949c). Switch `NightLook` (default 1, already in D3D9Config.h:128, D3D9Config.cpp:128/259/388, dialog IDC_FX_NIGHT 3059, VideoTab.cpp:942/1045). Owner: night. Line numbers are Custom's as of 2026-10-13 (SceneFx.cpp moves while others edit it: re-read before each edit).

## 0. Rules
- NightLook = 0 is the legacy image: shader code only under `_NIGHTLOOK` (added only when the key is 1), C++ paths behind `Config->NightLook`, the legacy textures byte-identical (facade_emis.dds keeps its md5; the new map is a new file loaded only when on).
- Daytime is unchanged with the key on: every night rule is scaled by a night weight that is exactly 0 while the sun is 3 deg or more above the camera's horizon; the ground and window terms are multiplied by the existing sun fades, which are 0 by day.
- Own code from published maths: Reinhard 2002 log-average key, Krawczyk 2005 partial (key-lowered) adaptation for dark scenes, Wyman 2013 style Gaussian point spread, blackbody sRGB values from the CIE 1931 observer (table data), stochastic (expected-value preserving) texture minification after Pharr et al. 2024. ClsHash is the already credited Hoskins "Hash without Sine" (MIT, CREDITS). No GPL or vendor sample code.
- One-line comments, `// not upstream` in upstream files. No new UBO, no new sampler, no new pass, no new buffer. 2D panel, HUD, GDI never touched.

## 1. What the user gets
- At night over a city: streets drawn by rows of warm lamps (sodium orange, warm and neutral LED, varied per street) with light pools on the pavement and dark blocks, parks and water between; city fringes fade out instead of a flat grey-white sheet.
- Buildings: dark walls and dark glass with a scatter of lit windows (warm homes 2700-3500 K, cool offices 4000-5000 K, bright shopfronts), coming on window by window through dusk.
- Exposure at night stays near neutral: lights are not pushed into white; a rising exposure brakes when part of the frame is near saturation.

## 2. Root cause (evidence; shots /tmp/earth/shots/cNn-85.png AE=1, cNx-85.png AE=0, "Gfx Shot New York Night", MJD 51982.08, sun -32.7 deg at NYC)
### 2.1 Windows "uniformly white"
- The night map is right: facade_emis.dds (2048, DXT1, own mips with gutters) decoded: 12.4 % of texels non-black, walls black, lit windows scattered (preview /tmp/earth/ngt/facade_emis_prev.png). Per style, upper floors: lit share of area / mean luminance = residential 0-7, 12: 3-12 % / 0.015-0.055; offices 10-11: 18 % / 0.10; glass towers 8-9: 35 % / 0.19-0.22 (lit texel luminance 0.55-0.62).
- The path is right: the facade has maps (D3D9Surface.cpp:156-165 adds them, AddMap sets OAPISURFACE_MAPS at :585), so CheckMeshStatus (Mesh.cpp:1387-1431) clears bCanRenderFast and D3D9Mesh::Render binds MAP_EMISSION (Mesh.cpp:1837-1847); PBR_PS does `cDiff = max(cDiff, cEmis * emission2)` (PBR.fx:368) then `*= gExposure` (:375). BldgMgr::Night (BldgMgr.cpp:392-415) has Emission2 = 1 everywhere below -6 deg.
- Roofs are black in the same shot (no _emis on roof.dds) and near the camera the look is right (taaN0-28.png, KSC: black walls, lit shopfronts): no lighting term lights the walls; all wall light is emission.
- At 2-6 km a window is 1-2 px, so trilinear/anisotropic sampling returns the wall average: 0.10-0.22 for offices and towers (Midtown and Downtown are towers, h > 45 m). AE=0 shot facades: p50 30/255, p90 68/255 (x transmission ~0.7): matches.
- Auto-exposure converged at about +2.3 EV (x4.9): 0.15 x 4.9 = 0.74, window peaks 2-3: PBR.fx has no tone curve, so everything clips to flat white (AE crop: p50 142, p90 231, p99 255).
- The saturation rule never fires: Measure counts a w/8 texel (an 8x8 box average) as saturated only at >= 254/255 (SceneFx.cpp:522), and the limit is 15 % (AutoExpo.h:20); the worst crop has 5 % of full-res pixels >= 250, the whole frame 0.9 %.
- Root cause: (a) exposure: +2.3 EV with no saturation brake; (b) emission authored display-referred and dense (towers 35 % of the wall lit) whose box mips become a uniform wall glow at range; (c) all eligible windows of a cell switch together (Emission2 per cell), so dusk is a fade, not windows coming on. Not causes: map loading, Emission2 scale, shader path, sRGB/linear (all display space), mip/atlas bleed (gutters re-padded each level, bldgtex.cpp:379-395).
### 2.2 Ground lights "flat grey-white blanket"
- The light texture (Mask RGB) at NYC is saturated over all land: Mask/14/000280/000603.dds median 248/255, colour (255, 251, 241), 0 on water, an orange fringe only at the city edge (/tmp/earth/ngt/ms603.png). No street structure (500 m source resampled).
- TerrainPS uses it as illumination of the day texture: `cNgt = cMsk.rgb * (1 - CamSpace) * fNgt` (NewPlanet.glsl:815) added to the light factor (:866) next to the sun's `cL * 2`. So the city is drawn as its daylight image at about half of full sun: AE=0 ground p50 44-49/255 with p95/p50 1.3-1.5 (no contrast); AE=1 p50 169-179, p95/p50 1.15.
- Root cause: a structureless, saturated light level used as a uniform light on the day albedo, at a daylight-like level, then x4.9 by the exposure.

## 3. Where it hooks (Custom file:line)
| What | Where |
|---|---|
| exposure controller | AutoExpo.h:13-21 Params, :29-46 Controller; AutoExpo.cpp:33-45 Target, :47-63 Update |
| night weight in, saturation threshold out | SceneFx.cpp:119-122 InitExposure params, :493-513 BeginFrame, :522 Measure `prm` |
| ground lights | NewPlanet.glsl:808-817 (`_NIGHTLIGHTS` block), helper before TerrainPS (:479, after ClsNoise :472-478) |
| macro, planet | VPlanetAtmo.cpp:167 (after `_NIGHTLIGHTS`) |
| windows shader | PBR.fx:367-368 (emission line in PBR_PS) |
| macro, meshes | D3D9Effect.cpp:328 (BaseMacros, after `_IRRADIANCE`) |
| window map | BldgMgr.cpp:55 (load), :392-415 Night(); BldgMgr.h:71 (member) |
| night map generation | Utils/bldgtex/bldgtex.cpp:51 Px, :54 Canvas, :175 NightLight, :188 Upper, :242 Ground1, :397 WriteDds, :467 main; Utils/bldgtex/CMakeLists.txt |

## 4. Exposure (AutoExpo, NightLook only)
- Night weight n from the sun at the camera's ground point: s = dot(unit(vp->CameraPos()), vp->SunDirection()) for the camera's proxy planet; `n = NightWeight(s)` = smoothstep from sin(+3 deg) (n = 0) to sin(-9 deg) (n = 1); 0 when there is no proxy. Computed in SceneFx::BeginFrame every frame, set with `expo.SetNight(n)`.
- Saturation measure: Measure's threshold becomes `expo.SatThreshold()` = 254/255 when NightLook is off or n = 0 (legacy value, bit-identical), else mix(254/255, 0.90, n): the share of w/8 texels whose max channel is >= 0.9, "near-saturated".
- Controller (Params gain `night` (bool), `nightDropEV` 3.0, `nightMaxEV` 1.0, `satNight` 0.05, `brake0` 0.005, `brake1` 0.03):
  - `!prm.night || n <= 0`: the legacy Target and Update code, unchanged (tested equal to the bit).
  - Band lowered by n x nightDropEV: log2 lo' = log2 lo - 3n, log2 hi' = log2 hi - 3n (at full night lo' = 0.015, hi' = 0.069): a night scene is shown as a night scene (partial adaptation for dark scenes, Krawczyk 2005), not normalised to the day band.
  - Saturation limit satLimit' = mix(0.15, 0.05, n): above it e only falls (legacy formula with hi').
  - Rising is braked by the near-saturated share: up = toLo' x (1 - n x smoothstep(brake0, brake1, sat)); at 3 % near-saturated the exposure no longer rises.
  - Upper clamp: min(maxEV, topEV, mix(maxEV, nightMaxEV, n)) (orbit keeps its +1 cap, which is already lower).
  - In the band: drift toward 0 as now.
- Expected: NYC night e about 1-1.3 (legacy 4.9); dark countryside at most +1 EV; dusk partly lowered; day e unchanged.

## 5. Ground lights (NewPlanet.glsl, `_NIGHTLOOK` inside the `_NIGHTLIGHTS` block)
- Kept: the sun fade `fNgt = clamp(-fDPS*4 + 0.05)*Prm.fBeta` (:811), the blue dirt filter (:813), the orbital term `cNgt2 = cMsk.rgb*CamSpace*4*fNgt` (:816) unchanged.
- Light level with contrast: I = max channel of cMsk; fC = I^2.2 (dim suburbs and city fringes go dark, cores stay full); hue from the texture: cHue = mix(1, cMsk.rgb / I, 0.5).
- Near-surface weight fNL = fNgt x (1 - CamSpace) (the legacy near weight).
- Land class (when Flow.bClass): w = texture(tClass, vUVSrf).rgb; fUrb = 1 - 0.75 x clamp(max(w.r, w.g) - w.b, 0, 1): trees, grass and fields at 25 %, built-up and bare at 100 % (Central Park dark).
- Street lamps (helper `NightLamps`, under `_NIGHTLOOK` before TerrainPS), only when Flow.bClass (the 8 m lattice vMSc[0] is set, camera below 20 km with a Class tile, Surfmgr2.cpp:1135-1143):
  - q = (texUV x vMSc[0].xy + vMSc[0].zw) / 16: blocks of 16 cells (128 m N-S, ~98 m E-W at 40 deg); period 1024 blocks (the lattice's 16384), so no seam.
  - Warp: qw = q + 0.5 x (vec2(ClsNoise(q/8, 128), ClsNoise(q/8 + (37, 71), 128)) - 0.5): streets bend by up to ~30 m over ~1 km, not a ruler grid.
  - Two street families: lines at integer qw.x and qw.y; for the nearest line of each: perpendicular offset dp, the block segment along it lit with probability pSeg = mix(0.45, 0.95, fC) (hash of line and segment); its palette from the same hash: sodium HPS 2000 K (1.00, 0.54, 0.07) 50 %, warm LED 3000 K (1.00, 0.71, 0.42) 30 %, neutral LED 4000 K (1.00, 0.82, 0.64) 20 %.
  - Lamps every 1/4 block (32 m) along a line, alternate sides at +-0.035 block, present with probability 0.85 and intensity x(0.8 + 0.4 hash); the current lamp and the nearer neighbour are evaluated, each with its own segment's lit test and colour.
  - Pool (light on the ground): Gaussian sigma_p = 0.07 block (~9 m). Head (lamp glare): sigma_h = 0.012 block (~1.5 m). Street spill: 1 - smoothstep(hw, 2hw, |dp|), hw = 0.04 block, on lit segments.
  - Anti-aliasing (TAA off stable): fw = max(fwidth(q)) (computed at the block's top level, STAGE_PS only, uniform control flow); each feature width is max(width, 0.7 fw) with its height scaled by (width/width')^2, so energy is kept and nothing is thinner than ~1.4 px; blended to the analytic area mean over fw 0.06-0.125 block; beyond 0.125 the means only (no lamp code runs).
  - Means per block^2 (both families): pool = 8 x pSeg x 0.85 x pi sigma_p^2, head = 8 x pSeg x 0.85 x pi sigma_h^2, spill = 2 x pSeg x 3 hw; palette mean 0.5 S + 0.3 W + 0.2 N. Without Flow.bClass (high, or no Class tile) the means are used directly, so the change at 20 km is continuous (lamps are below 2 px there).
- Assembly (legacy cNgt/cNgt2 names, so lines 866 and 892 stay):
  - cNgt = cHue x fC x fUrb x fNL x (kAmb x palMean + kPool x cPool + kSpill x cSpill): light on the day albedo; streets (lighter pavement) and pools read brighter, blocks between dark. kAmb 0.03, kPool 0.8, kSpill 0.12.
  - cNgt2 = legacy orbital term + cHue x fC x fUrb x fNL x kHead x cHead x GetLandView(rad, vPlN).atn: lamp glare as emission, attenuated by the air to the camera (the later `sct` call has the same arguments). kHead 2.0.
  - Targets at e = 1 after HDR(): pool centre on pavement ~0.35, between pools ~0.02, lamp head ~0.85, city mean at range ~0.07 (legacy 0.18, 0.5 at ~2 km). Constants tuned in the implementation against the shots and recorded.
- Everything is static (no time term): stable without TAA, nothing for TAA to average.

## 6. Windows
### 6.1 Night map: Utils/bldgtex writes `Textures/Bldg/facade_nite.dds` (new file; facade_emis.dds unchanged)
- 2048 x 2048 BC3 (DXT5), full mips. RGB: the window's night colour; A: the window's on-time a.
- Per window (the existing hashes): u = F01(H(wh x 7 + 3)) as the legacy lit test; lit at full night when u < lit'(style): residential 0.40, offices 0.45, glass towers 0.40, industrial 0.12, shops 0.75, lobbies 0.60, entrances 0.70. a = 0.05 + 0.85 u / lit' for those, 1.0 for the others (rgb 0). Every pixel of a window's bay and floor cell (frame, wall) carries that window's a, so BC3 alpha blocks are near exact and box mips keep a.
- Colours (sRGB of blackbody white points, CIE 1931 data): residential 2700 K (255, 167, 87) 45 %, 3000 K (255, 180, 107) 30 %, 3500 K (255, 196, 137) 15 %, 4000 K (255, 209, 163) 7 %, TV blue (140, 170, 255) x0.5 3 %; offices 4000 K 50 %, 5000 K (255, 228, 206) 40 %, 3500 K 10 %; shops and lobbies 3000-4000 K. Intensity k = 0.40-1.00 by hash, curtains and blinds as today (x1.2 top, x0.5 slats).
- Mips (`NightLook::NiteMip`, shared with the tests): levels 1-3 box rgb, alpha luminance-weighted (shapes kept, gutters re-padded as the other maps); levels >= 4 (a texel >= 1/4 of a window cell): each texel takes one level-0 pixel of its footprint: lit with probability n_lit/N (hash), then a uniformly chosen lit pixel's rgb and a; else rgb 0, a 1. The expected value of every texel is the box mean, also at any dusk progress, but each texel is a lit window or dark: a scatter of points at range instead of a uniform glow (stochastic minification, Pharr 2024, precomputed).
### 6.2 BldgMgr (NightLook, map loaded)
- Ctor (after :55): `if (Config->NightLook && texFacade) if (VkTex *n = NatLoadSpecialTexture("Bldg/facade.dds", "nite")) { SURFACE(texFacade)->AddMap(MAP_EMISSION, n); nite = true; }`: the facade's emission map is replaced (AddMap frees the _emis one). Missing file: nite stays false and the legacy windows run.
- Night(): with nite, per cell D = `NightLook::CellDusk(el, nightAt/0.6)` = clamp(1.2 x clamp((2 - el)/10, 0, 1) - 0.2 x h, 0, 1) (0 at +2 deg, all eligible windows on by about -8 deg; cells offset by up to 0.2); set only when it moved by > 0.01 (or reached 0/1): `SetMaterialEx(0, MatProp::SpecialFX, (0, D, 1.0, 0.75))`. Emission2 stays 0 (Make, :385). w = 0.75 tags "streamed building night"; y = D; z = intensity.
### 6.3 PBR.fx (`_NIGHTLOOK`, at :367-368)
- `if (gCfg.Emis && abs(gMtrl.specialfx.w - 0.75) < 0.01) { vec4 n = texture(EmisS, uv); float w = min(0.03 + 0.03 * textureQueryLod(EmisS, uv).y, 0.15); cDiff.rgb += n.rgb * smoothstep(n.a - w, n.a + w, D) * smoothstep(0.0, 0.05, D) * K; } else` the legacy max line.
- Additive (light from inside adds to the glass reflection); unlit windows keep the diffuse dark glass under the night ambient; the wider step at coarse lods follows the averaged on-times; the second smoothstep keeps day (D = 0) dark at every lod. The branch is uniform per draw (material constants).
- Then `*= gExposure`, transmission, inscatter as before.

## 7. Passes and buffers
- None new. One texture: facade_nite.dds 5.6 MB on the GPU, replacing facade_emis.dds (2.8 MB) for the buildings when on.

## 8. Shaders, macros, bindings
- `_NIGHTLOOK` in the planet flags (VPlanetAtmo.cpp:167, only with `_NIGHTLIGHTS`) and in D3D9Effect BaseMacros (D3D9Effect.cpp:328; PbrMesh.fx gets it too, unused there, same SPIR-V).
- NewPlanet.glsl: no new uniform or sampler (tMask 14, tClass, tDiff, the land-view tables already bound). PBR.fx: EmisS and gMtrl (FxParams) only.
- Exposure.glsl unchanged (threshold from vPrm.w).

## 9. C++ per file
- **AutoExpo.h/.cpp** (own): Params fields of section 4; `Controller::SetNight(float)`, `Night()`, `SatThreshold()`; `float NightWeight(float sinElev)`. Namespace `NightLook` (pure, mirrors of the shader and generator maths for tests): `CellDusk`, `WindowOn`, `CityLevel`, `LampField` (C++ copy of NightLamps with the CPU ClsHash/ClsNoise), `LampMeans`, `NiteMip`. One unit, no new CMake entry (AutoExpo.Test already registered, Tests/CMakeLists.txt:201).
- **SceneFx.cpp** (integrator; 3 one-line edit_blocks): InitExposure `p.night = Config->NightLook != 0;`; BeginFrame `if (Config->NightLook) expo.SetNight(...)` (section 4); Measure `prm.w = expo.SatThreshold()`.
- **BldgMgr.cpp/.h** (shadows' file; night parts by edit_block): ctor load (6.2), Night() branch, `#include "AutoExpo.h"`, member `bool nite = false;` next to texFacade (BldgMgr.h:71).
- **VPlanetAtmo.cpp**, **D3D9Effect.cpp**: one macro line each.
- **Utils/bldgtex/bldgtex.cpp**: Px gets `C nite {}; float thr = 1;`; Canvas a `nite` RGBA image (Set/Copy/Pad carry it); `NiteLight()` and the on-time in Upper/Ground1 (emi untouched); `WriteDds` gains a BC3 variant (FourCC DXT5, 16-byte blocks, stb alpha); main writes facade_nite.dds with NiteMip. **Utils/bldgtex/CMakeLists.txt**: AutoExpo.cpp in the sources, facade_nite.dds in BLDGTEX_FILES (output and install).

## 10. Config, dialog, docs
- Key and dialog exist. Docs (integrator): VulkanClient.html Effects list: "NightLook (1): night city look: street lamps and lit windows from the light map and the building night map, windows coming on through dusk, exposure that keeps a night scene dark and stops rising when lights near saturation. 0: the previous night lights and windows." README key list the same line.

## 11. Cost (RTX 5070 Ti, 1080p)
- TerrainPS at night: ~150-200 ALU and no new fetch except GetLandView's atn (CSE with the later call) and tClass (already fetched in the class block): target <= 0.2 ms with the city filling the screen; 0 by day (fNgt 0 skips the lamps).
- PBR_PS buildings: one fetch on the same texel and a lod query: < 0.02 ms. AutoExpo: a few flops per frame. bldgtex: about +2 s at build.
- Measure: no GPU timer is reachable from the owners' files; the build-tree run is compared by Orbiter's frame interval if available, else the estimate stands (open item for the integrator: timestamp the planet pass).

## 12. Interplay
- TAA: everything static; the lamp AA uses fwidth of the jittered frame (sub-pixel only). SSAO darkens pools at building feet as on any ground. Volume clouds and their shadows: unaffected (sun down; clouds are not lit from below: open).
- Light shafts and flare: off at night (sun below horizon). LightBlur (PostProcess 1): bright windows and lamp heads glow, fine; the measure stays after it (bMeasureLate).
- Filmic: the ground goes through HDR(), which becomes the ACES fit under _FILMIC; PBR.fx has no curve either way. Exposure: section 4; the windows and lamps are scaled by e like everything lit.
- Water: no lights on water (Mask RGB 0 there); WaterFx unaffected. SSR (owner ssr) will reflect lamps and windows in water; fog (owner fog) attenuates them; SSGI may bounce lit pavement; plumes, shadows: no contact (shadows also edits PBR.fx and BldgMgr in other regions).

## 13. PBR/glTF/XR notes
- glTF/PBR groups use PbrMesh.fx (not changed; `_NIGHTLOOK` unused there). Legacy vessel groups only enter the new branch with SpecialFX.w = 0.75 and an emission map: add-ons use SpecialFX.x (heat) only; collision is unlikely and documented. XR vessels unaffected. Base meshes (KSC) keep their own emission (no tag).

## 14. Risks and open questions
- The procedural streets do not follow real roads (no road data); under 3D buildings they are mostly hidden; Manhattan's 29 deg grid is not matched.
- No Class tile: no near lamps (the means only); the Earth stream makes Class tiles for land it covers.
- Orbit night views may sit up to 1 EV lower than legacy (lowered band); checked.
- SurfNative::Reload (texture file changed while running) puts facade_emis back: windows go dark until restart (dev case only).
- The night map's TV-blue and lit fractions are a taste choice; constants tuned on the shots.
- Shared files are edited by others at the same time: small edit_blocks, re-read first.
- First implementation step V1: a build-tree-only PBR.fx debug copy that outputs the lit diffuse in R and emission x Emission2 in G for the night shot (then removed) confirms 2.1 before tuning.

## 15. Checks (tags ngt*, /tmp/earth/vfx/run.sh, 90 s, shots 60 85; UE5 switches SceneShadows/SSR/VolPlumes/SSGI/HeightFog = 0 and TAA = 0 in the A/B pairs)
| Tag | Scenario, keys | Pass if |
|---|---|---|
| ngtL0 | NYC Night, NightLook=0, first run of phase 3 (before my build) | baseline |
| ngtN0 | NYC Night, NightLook=0, final build | vs ngtL0: buildings and ground regions mean abs diff < 1 level, p99 < 4 |
| ngtN1 | NYC Night, NightLook=1, AE=1 | building region: < 3 % pixels >= 250; dark (< 40) >= 40 %, bright (> 150) 3-25 %; ground region mean < 60/255 (legacy 150-170) and p95/p50 >= 2.5; e (from the ratio to ngtX1 on unclipped pixels) <= 1.6 |
| ngtX1 / ngtX0 | NYC Night, AE=0, NightLook 1 / 0 | X1: lamps as points and rows near the camera, dark between; X0 = legacy look |
| ngtD1 / ngtD0 | new "Gfx Shot New York Dusk" (MJD 51981.967, sun about -3 deg), NightLook 1 / 0 | D1: part of the windows lit, ground lights partly on, no errors |
| ngtY1 / ngtY0 | "Gfx Shot New York" (day, +37 deg), NightLook 1 / 0 | identical within run noise (mean abs < 1 level) |
| ngtO1 / ngtO0 | "Gfx Shot Orbit Europe Night", NightLook 1 / 0 | city lights present, no errors; EV difference reported |
- Client logs: no "[ERROR]" line except the pre-existing "UnDeleted Surfaces" list at exit (same count as the legacy runs).
- Offline: md5 of facade.dds, facade_emis.dds, facade_norm.dds, roof*.dds unchanged after the bldgtex change; glslangValidator TerrainPS with the Earth flags without `_NIGHTLOOK`: SPIR-V identical to the pre-change file; with it: compiles.
- Copies to /home/racerx/Pictures/orbiter-vfx/: night-before.png (ngtN0 or ngtL0), night-after.png (ngtN1), night-dusk.png (ngtD1).

## 16. Maths unit and tests (AutoExpo.h/.cpp, Tests/AutoExpo.Test.cpp; run the test binary, not ctest)
- Legacy equality: every existing scenario with `night = true` and n = 0 gives the same EV sequence to the bit as `night = false`.
- NightWeight: 0 at >= +3 deg, 1 at <= -9 deg, monotone, ~0.5 at -3 deg.
- Night city (L0 0.012, n 1): converges to <= log2(0.015/0.012) + 0.05 EV, no overshoot; legacy params go to the 2.5 clamp.
- Dark country (L0 0.001, n 1): stops at nightMaxEV 1.0.
- Brake: L0 0.005, n 1, sat 0.04: EV never rises; sat 0.0175: rises less than with sat 0.
- Night saturation: sat 0.1 > 0.05 lowers e. SatThreshold: 254/255 at n 0 or off, 0.9 at n 1, monotone.
- Dusk n 0.5: band 1.5 EV lower.
- CellDusk: 0 above +2 deg, 1 below -9.5 deg for every hash, monotone in elevation. WindowOn: 0 at D 0 for any a and lod; 1 at D 1 for a <= 0.9; monotone in D.
- LampField: the area average over 400 x 400 blocks at fw 0.001 equals LampMeans within 3 % (pool, head, spill); at fw >= 0.125 it equals the means exactly; at fw 0.06-0.125 the average stays within 5 % (no jump); periodic: q + (1024, 0) and (0, 1024) give the same value; lit-segment share = pSeg within 2 %.
- NiteMip: every texel of levels >= 4 is 0 or a lit level-0 pixel of its footprint (rgb and a); the level mean equals the box mean within 2 % (64 x 64 texels) at full night and at D = 0.5 (counting a < D); levels 1-3 equal the box mean exactly; alpha of a single-window footprint is that window's a.

## 17. Touched files
- Own: OVP/VulkanClient/AutoExpo.h, AutoExpo.cpp, Tests/AutoExpo.Test.cpp; Utils/bldgtex/bldgtex.cpp, Utils/bldgtex/CMakeLists.txt; NewPlanet.glsl `_NIGHTLIGHTS` block and the `_NIGHTLOOK` helper before TerrainPS; PBR.fx emission line (:367-368); BldgMgr.cpp ctor texture line and Night(), BldgMgr.h one member.
- Shared one-liners (listed for the integrator): SceneFx.cpp (InitExposure, BeginFrame, Measure), VPlanetAtmo.cpp:167, D3D9Effect.cpp:328.
- Build tree only: Scenarios/Tests/Gfx Shot New York Dusk.scn.
- Generated: Textures/Bldg/facade_nite.dds (build step).

## Rev 2 (implementation)
Review: design-review-night-look.md (6 MUST, 4 SHOULD, 13 NIT). These bullets override the sections above.
- **M1 orbital term.** Under `_NIGHTLOOK` the legacy orbital term is weighted by wO = smoothstep(0.35, 0.8, CamSpace) (0 below ~11 km, 1 above ~58 km; CamSpace = sqrt(alt / 90 km)); the near term (the lamp means at height) carries the city below that. 2.2 corrected: at 700 m the flat floor is this term (0.97 x 0.088 x 4 x fNgt x lightfac 0.5 = 0.17 after the haze, ~50/255 through HDR) plus the near term (0.44 x albedo). Constants are calibrated with lightfac 0.5 inside fNgt. Added check ngtM1/ngtM0: NYC at night from ~30 km.
- **M2 near-saturated share, full resolution.** At night (n > 0) Measure replaces its first blit with a new IPI entry `PSHalfSat` (Exposure.glsl) that reads the full-res target and writes tHalf[0] (W/2): rgb the 2x2 mean, a the share of its 4 full-res pixels with max >= thr. When the target is MSAA or has no SAMPLED usage, it is first copied (StretchRect, nearest) into a single-sample full-res copy tFull (made on first use, NightLook only; SceneFx.h one line). The linear blits to W/4 and W/8 average a, so a at W/8 is the full-res share per 8x8 block; `PSBlocksSat` (PSBlocks compiled with PS_PSBlocksSat) sums c.a into acc.z. A 1-px clipped window counts as 1 px. By day (n = 0) the legacy blit chain and PSBlocks run unchanged. tHalf get COLOR_ATTACHMENT usage with NightLook; both entries compile only with NightLook, inside `#if defined(PS_PSHalfSat) || defined(PS_PSBlocksSat)`, so PSBlocks/PSTotal keep their SPIR-V. Threshold mix(254/255, 0.9, n). §0 and §8 "Exposure.glsl unchanged / no new pass / no new buffer" are dropped.
- **M3 band.** Only lo is lowered (lo' = lo x 2^(-3n)); hi, satLimit 0.15 and the downward rule stay legacy (satNight dropped). n is taken over the camera's own horizon: sin(elevation + dip), cos dip = R / (R + h), so a camera in orbit in the sun has n = 0 even over the night side. Added check ngtT1/ngtT0: an orbit view of the terminator from the night side, EV within 0.1.
- **M4 lamp filtering.** Per street family, footprints from fwidth of the warped coordinate: across fu = fwidth(qw.x) for the x-lines, along fv = fwidth(qw.y) (and swapped). Lamp points are anisotropic Gaussians exp(-dx^2/sx^2 - dy^2/sy^2), sx = max(s, fu), sy = max(s, fv), height x s^2/(sx sy). Line level: over fv 0.0625-0.125 block (lamps 4 -> 2 px apart) the points of a family blend to a band across the line, exp(-dp^2/sx^2) x 4 x 0.85 x pi s^2 / (sqrt(pi) sx) on its lit segment (same energy a unit length). Area level only over fu 0.2-0.35 (lines 5 -> 3 px apart). At 5 km across the view lines stay lines. LampMeans and the tests cover the three levels.
- **M5 NaN.** cHue = mix(1, cMsk.rgb / max(I, 1e-3), 0.5); no other division by a mask value; the ngtY pair checks daylight albedo.
- **M6 window step.** w = clamp(0.03 + 0.03 x max(lod, 0), 0.03, 0.15).
- **S1 nested mips.** Levels 1-3 box (luminance-weighted a); each texel of level k >= 4 is one of its 4 level k-1 children chosen by hash (rgb and a): expected value the box mean, and every coarse point exists one level finer. Test: each level >= 4 texel equals one of its children.
- **S2 cockpit.** Internal views (oapiCameraInternal, SceneFx::BeginFrame `in`) get `SetNight(n, true)`: the band drop and the lowered clamp are off (the VC rises as legacy, no 2.3 EV darker cockpit), the near-saturation threshold, measure and brake stay (the city through the canopy still stops a rise). Unit test "the cockpit keeps the day band, the brake stays".
- **S3 ownership.** For the integrator: the `_NIGHTLOOK` helper sits immediately before TerrainPS (outside the `_NIGHTLIGHTS` block), the PBR.fx emission line region, Utils/bldgtex (granted by the brief for the night map), Exposure.glsl new entries, SceneFx.cpp InitExposure/BeginFrame/Measure edits, Tests/CMakeLists.txt -O2 on AutoExpo.Test. All shared files by edit_block only.
- **S4 checks added.** Scenarios in the build tree: Gfx Night NYC 30km (M1), Gfx Shot Orbit Terminator Night (M3), Gfx Night KSC VC (S2), Gfx Night NYC Pan (TAA on/off in motion). Offline: TerrainPS SPIR-V with the Earth flags without `_NIGHTLOOK` identical to the file with my blocks cut; PBR.fx preprocessed text without the macro identical (blank lines aside); Exposure PSBlocks/PSTotal SPIR-V identical. The finish phase allows 3 runs: NYC Night NightLook 1/0 and NYC Dusk are run; the 30 km, terminator, VC and pan runs are open items (the maths for M1/M3/S2 is in the unit tests).
- **NITs.** 1: spill height x w/w'. 2: profile exp(-r^2/s^2), integral pi s^2, in shader, LampMeans and tests. 3: widths no thinner than 1.0 x footprint. 4: a = 0.05 + 0.80 u / lit (max 0.85). 5: rgb-0 texels of never-lit windows get a = 0; walls of lit windows keep their window's a. 6: BC3 shared endpoints at levels >= 4 accepted (noted). 7: GetLandView at the block's top level. 8: tClass fetched again in the uniform `Flow.bClass` branch (one fetch, cached). 9: any positive step at night is braked. 10: Monte Carlo counts fixed (1e6), -O2 on AutoExpo.Test. 11: Wyman citation dropped. 12: cost by a frame-time A/B at the night shot (and the estimate). 13: SSAO darkens pools and heads at building feet (accepted).
- **Code review (code-review-ue5-night.md) follow-ups.** By day nothing of the night block runs except the uniform fwidth and dFdx (warp, tClass fetch and lamps only under `fNgt > 0`, the footprint from the unwarped q; the lamp glare is added ×sct.atn after the haze, no second GetLandView). In the cockpit the brake window is 3x wider (1.5-9 %), because MFDs, HUD and panel lamps sit near saturation. The across widths are capped at half a block. NightWeight caps elevation + dip at 90 deg. D is pushed in 0.003 steps.
- **Risk (review NIT 4).** At full night the brake also holds the in-band drift back up after a bright view, when daylit content in the frame (2-7 % of pixels ≥ 0.9 in orbit, 12 % on a cloud deck) is past brake1. For example, an orbit camera on the night side looking at the day side stays at the lower e until the view changes. This is accepted (the review's NIT 9 asked for it). Read it off the terminator check.
