# Design review: night look (NightLook)

Repo: /home/racerx/Projects/Orbiter-linux-custom, branch ue5-batch, HEAD 35e2949c (working tree already edited by the other owners: NewPlanet.glsl +46 lines, so `_NIGHTLIGHTS` is at 854 now, not 808; SceneFx.cpp +3). Paths are under OVP/VulkanClient/ unless shown otherwise. I read the design against the code, the shots cNn-85/cNx-85 and the configs. Read-only: nothing was built or run.

## Verified
- The windows' root cause (2.1) holds.
  - The facade has maps, so CheckMeshStatus drops FAST (Mesh.cpp:1387-1431).
  - MAP_EMISSION is bound (Mesh.cpp:1834-1846).
  - PBR_PS does max(cDiff, cEmis·emission2) (PBR.fx:368), then exposure (:375). There is no curve.
  - Emission2 is 1 below -6 deg (BldgMgr.cpp:405-412).
  - cNn-85 (AE=1) shows white walls; cNx-85 (AE=0) shows a grey window pattern.
- The measure is an 8x8 box (three linear halvings, SceneFx.cpp:516-518 HEAD) with `max >= 254/255` (Exposure.glsl:31) and satLimit 0.15 (AutoExpo.h:19, not :20).
- Every file:line in §3 and §9 checks out at HEAD:
  - D3D9Config.cpp:128/259/388, VideoTab.cpp:942/1045, VPlanetAtmo.cpp:167, D3D9Effect.cpp:328;
  - BldgMgr.cpp:55/385/392-414, BldgMgr.h:71;
  - NewPlanet.glsl:472-480/808-817/866/892, Surfmgr2.cpp:1135-1143;
  - bldgtex.cpp:51/54/175/188/242/379-392/397/467, Tests/CMakeLists.txt:201;
  - AutoExpo.cpp:33 (Target), :47 (Update).
  - The SceneFx.cpp numbers are from the working tree.
- Texture plumbing:
  - `NatLoadSpecialTexture(file, "nite")` builds `Bldg/facade_nite.dds` (D3D9Surface.cpp:74-96).
  - `AddMap` deletes the old map and sets OAPISURFACE_MAPS (D3D9Surface.cpp:580-586).
  - TextureMips is clamped to at most 1 (D3D9Config.cpp:194), so the file's own mips are kept.
  - BC3 is decoded (VkTexFile.cpp:50, 223).
  - Shaders are `#version 460` (VkShader.cpp:166), so textureQueryLod exists.
- The SpecialFX tag:
  - SetMaterialEx stores it (Mesh.cpp:1165-1167) and it is uploaded with the material (Mesh.cpp:1902/1919).
  - Only Metalness.fx:186-187 reads specialfx.x.
  - Add-ons can only reach it through gcCore MESHM_SPECIALFX (gcConst.cpp:199).
  - Make resets `cell.night` (BldgMgr.cpp:269), and Night() runs every frame (:206).
- The exposure inputs:
  - vPlanet::CameraPos() is camera minus planet in the global frame (VPlanet.cpp:689-692).
  - SunDirection() is the global unit vector (VObject.cpp:200), so their dot is sin(elevation) at the nadir.
- The macro plan:
  - With the key off, no macro is added and the SPIR-V is unchanged.
  - No UBO or sampler is added (INT-2/INT-3).
  - tClass (30), Flow.bClass and the 8 m lattice vMSc[0] with P 16384 are present.
  - The Earth flags have no `_MICROTEX`, so bClass is reachable.
- Scenario sun angles:
  - MJD 51982.08 is 20:55 EST (about -30 deg).
  - MJD 51981.967 is 18:12 EST, 16 min after sunset (about -3 deg).

## MUST

1. **The flat blanket at the test camera comes mostly from the orbital term, which the design keeps (NewPlanet.glsl:816, :892).**
   - The camera is at 700 m (GROUNDLOCATION … 700), and Earth's AtmoVisualAlt is 90 km (Config/GC/Earth.atm.cfg:6). So CamSpace = sqrt(0.7/90) = 0.088 (VPlanetAtmo.cpp:697 HEAD).
   - cNgt2 = 0.97 · 0.088 · 4 · fNgt · fBeta (lightfac 0.5, Src/Orbiter/Config.cpp:101) = 0.17. It is added flat to every lit land pixel after the haze, then goes through HDR (Srf_Expo 1.295).
     - AE=0: 1 − exp(−1.295·0.17) = 0.20, which is 50/255. The design measured a ground p50 of 44-49.
     - AE=1 (×4.9): 0.66, which is 168/255. The design measured 169-179.
   - So §2.2 puts the cause on the wrong term. With §5 as written, a flat floor of about 50/255 stays. "Between pools ~0.02" and the ngtN1 check p95/p50 ≥ 2.5 cannot be met.
   - Fix: under `_NIGHTLOOK`, weight the orbital term by wO = smoothstep(0.35, 0.8, CamSpace). That is 0 below about 11 km and 1 above about 58 km, so orbit stays legacy. The near term's means carry the city between those heights. Correct §2.2. Add a check at about 30 km at night for a continuous fade (no grey sheet, no dark gap).

2. **The near-saturated share can never fire for windows or lamps (SceneFx.cpp:516-519 HEAD, Exposure.glsl:31).**
   - The share is counted on w/8 texels that are 8x8 box averages. Examples:
     - A 2x2 px window at 1.0 on a 0.05 wall gives (4 + 60·0.05)/64 = 0.11.
     - A 4x4 cluster gives 0.29.
     - Only solid bright areas of at least about 7x8 px reach 0.9.
   - With 5 % of the full-res pixels clipped (the design's own crop), sat stays at about 0. brake0/brake1 and satLimit' 0.05 do nothing, and the brief's "limit from the fraction of near-saturated pixels" is not met.
   - Fix (NightLook only):
     - Replace the first blit with a new entry `PSHalfSat` in Exposure.glsl. It writes rgb as the 2x2 mean and a as the share of the 4 pixels with max ≥ thr.
     - The two linear blits then average a. tHalf is RGBA16F, so a at w/8 is the full-res share per 8x8 block.
     - A new `PSBlocksSat` sums `c.a` into acc.z.
     - Keep both new entries under `#if defined(PS_PSHalfSat) || defined(PS_PSBlocksSat)`, so PSBlocks and PSTotal keep their SPIR-V. Compile them only when the key is on.
     - Cost is about 0.03 ms. Drop "Exposure.glsl unchanged" and "no new pass" from §8 and §0.

3. **The lowered band darkens daylit content whenever the camera's nadir is in night (§4, AutoExpo.cpp:33-45).**
   - hi' = 0.55·2^(−3n) = 0.069 at n = 1. Take an orbit sunrise or terminator view from the night side: nadir at −30 deg, mean of lit pixels 0.4.
     - Legacy: in band, so e drifts to 1.
     - Design: target log2(0.069/0.4) = −2.5, clamped to −1.5 EV. The dayside is drawn at 35 %.
   - At ground dusk (n 0.5, twilight sky at 0.3), e drops 0.6 EV below legacy.
   - Fix:
     - Lower only lo (lo' = lo·2^(−3n)) and keep hi and the downward rule as legacy. NYC still converges to about e = 1, because the city mean of about 0.07 is inside [0.015, 0.55] and drifts to 0.
     - Keep the nightMaxEV cap.
     - Add a check: an orbit scene looking at the terminator from the night side, NightLook 1 vs 0, EV within 0.1.

4. **Lamp filtering on max(fwidth(q)) leaves the test view as a uniform sheet (§5 AA).**
   - fwidth takes the long axis of the footprint. With FoV 50 deg at 1080 rows (8.1e-4 rad/px) and the camera at 700 m, the along-ground footprint is d²·α/h:
     - 0.06 block (7.7 m) at 2.6 km;
     - 0.125 block (16 m) at 3.7 km.
   - Manhattan is 2.5-12 km from the camera, so almost all of it gets "means only": one warm level times fC (saturated in NYC, Mask median 248) times fUrb. That is the blanket again, and the p95/p50 ≥ 2.5 check fails.
   - Fix: filter each street family by its perpendicular footprint, fwidth of qw.x for the x-lines and of qw.y for the y-lines. Add an intermediate level:
     - when lamps along a line are under 2 px apart (along-line footprint), use the line mean: a 1D Gaussian band of σ_p carrying the pool energy per unit length;
     - go to the area mean only when the perpendicular footprint passes 0.125 block.
   - At 5 km the across-view footprint is 4 m (0.03 block), so streets stay lines out to about 10 km. LampMeans and the tests get the line level.

5. **NaN on every pixel with no light, by day too (§5 "cHue = mix(1, cMsk.rgb / I, 0.5)").**
   - cMsk.rgb is 0 on water, on dark land and on tiles without a mask (NewPlanet.glsl:515 sets vec4(0,0,0,1) when !bMask). I = 0, so 0/0 is NaN, and NaN·fNL(0) is still NaN.
   - It reaches `cTex.rgb *= … + cNgt` (:866). max(0, NaN) (:868) gives 0 or NaN depending on the driver, so the albedo of water and countryside is lost in daylight with the key on.
   - Fix: `cMsk.rgb / max(I, 1e-3)`, and keep the whole night assembly free of divisions by mask values. The ngtY check will catch a slip only if it shows.

6. **The window step inverts under magnification (§6.3).**
   - `textureQueryLod(EmisS, uv).y` is the unclamped LOD, which is negative close up: −2 at street level.
   - w = 0.03 + 0.03·(−2) = −0.03, so smoothstep(a + 0.03, a − 0.03, D) has edge0 > edge1. That is undefined, and in practice the step is inverted: lit windows are off and late ones are on in the closest views.
   - Fix: `w = clamp(0.03 + 0.03 * max(lod.y, 0.0), 0.03, 0.15)`, or use `.x`.

## SHOULD

1. **Stochastic mips chosen independently per level pop with distance (§6.1 NiteMip).**
   - Level k and k+1 pick unrelated points, so a camera dolly crossfades between two different point sets. Points vanish and appear elsewhere, most visibly without TAA.
   - Fix: nested selection. Each level-k+1 texel takes one of its 4 level-k children uniformly (hash), copying its rgb and a. The expected value is still the box mean, and every point at a coarse level is one at the finer level.
   - Test: each level ≥ 5 texel equals one of its children.

2. **Cockpit at night.**
   - n comes from the nadir for internal views too, so a dark VC (mean about 0.03) goes to e ≈ 1 with the cap at +1. Legacy is +2.5, so the VC is 2.3 EV darker. That is a visible change for every night flight.
   - Fix: n = 0 when oapiCameraInternal() (SceneFx::BeginFrame already tracks `expoInternal`), or decide and document it with a DG VC night check (Gfx Night KSC, internal, NightLook 1/0).

3. **Ownership and edit regions.**
   - INT-8 gives night "NewPlanet.glsl _NIGHTLIGHTS block only". The NightLamps helper before TerrainPS (HEAD :479) is outside it, and shadows and fog also insert there.
   - PBR.fx is shared: shadows edit :209-211/484-487/509-511 and SSR edits PBR_PS env and FAST_PS around the same region as :367-368.
   - Utils/bldgtex is not in INT-8.
   - Fix: list all three for the integrator, keep the helper under `#if defined(_NIGHTLOOK)` immediately before TerrainPS, and use edit_block only.

4. **Missing checks:**
   - mid altitude (MUST 1);
   - orbit terminator (MUST 3);
   - VC night (SHOULD 2);
   - the NYC Night shot with TAA on and off while panning (stochastic points, lamp rows);
   - PBR_PS SPIR-V identity with the key off (D3D9Client.fx effect), next to the TerrainPS check.

## NIT

1. **Spill AA.** The widening uses (w/w')² for every feature, but the street spill is 1D, so its energy needs w/w'. At fw 0.1: 0.33 vs 0.57, and the spill is about 20 % of the city mean.
2. **Gaussian form.** The means use π σ², which is the integral of exp(−r²/σ²). For exp(−r²/2σ²) it is 2π σ². Name the profile in §5 so the shader, LampMeans and the test agree.
3. **Lamp minimum width.** σ' = 0.7 px sampled at pixel centres varies 22 % in peak between offset 0 and 0.5 px, so lamp rows twinkle while panning without TAA. Use max(width, 1.0·fw).
4. **WindowOn test.** "1 at D 1 for a ≤ 0.9" fails at w = 0.15: smoothstep(0.75, 1.05, 1) = 0.93. Cap a at 0.85, or let CellDusk reach 1.15.
5. **Dark texels.** Dark texels carry a = 1. Bilinear blends with a lit neighbour (a 0.3) give a 0.65 at half colour, which is off at D 0.5, so windows shrink at dusk. Give rgb-0 texels a = 0. Day stays dark through smoothstep(0, 0.05, D).
6. **Window colour at range.** In BC3, isolated warm and cool lit texels in one 4x4 block share two colour endpoints, so cool points turn dim-warm at levels ≥ 4. Accept it, or draw the lit points of one block from one palette.
7. **Head term.** GetLandView (Scatter.glsl:734-747) samples with implicit LOD (smaple3D, :257-258). Calling it again inside the per-pixel `fNgt > 0` branch is undefined. Compute it outside the branch, or multiply the head part by `sct.atn` after :827.
8. **Class weights.** `vec3 w` (tClass) is scoped inside `if (Flow.bClass) {…}` (HEAD :735-749), so it is not visible at :808. Fetch it again in the uniform branch and correct §11's "no new fetch".
9. **In-band drift.** The drift (AutoExpo.cpp:43) can also raise e (ev < 0 after a bright scene) and is not braked. Apply the brake factor to any positive step.
10. **Test cost.** AutoExpo.Test runs under ASan without -O2 (Tests/CMakeLists.txt:203). Give LampField/NiteMip fixed Monte Carlo counts (for example 1e6 points, which gives an SE of about 1.3 % on the head mean) and add -O2 as HorizonMap.Test does.
11. **Citation.** "Wyman 2013 style Gaussian point spread" is not an identifiable source. Drop it, since a Gaussian needs none.
12. **Cost measurement.** §11 should say how it is measured: a build-tree-only timestamp patch around the planet pass, as the earlier agent used, or a frame-time A/B.
13. **SSAO.** SSAO also darkens the emissive pools and lamp heads at building feet. Note it under interplay.

Counts: MUST 6, SHOULD 4, NIT 13.
