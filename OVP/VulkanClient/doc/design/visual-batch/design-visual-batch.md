# Design: visual batch (lens flare, light shafts, TAA, water, 3D-cloud shadows, filmic, camera effects)

Repo: Orbiter-linux-custom (/home/racerx/Projects/Orbiter-linux-custom), branch visual-fx from cloud-seams. Work happens in Custom (owner's direction, 2026-10-13); the PBR/glTF parts in section 12 are part of this work, not a later merge. Facts come from map-postprocess.md and map-water.md (agent maps of the Earth tree, which Custom contains after merge 96c8af66), and must be re-checked in Custom.

## Rev 2: review fixes (design-review-visual-batch.md); these override the sections below
- **M1 vessel motion.** DepthPass writes a second R8 target, an object flag: 1 for the camera target vessel (oapiCameraTarget), 0 for everything else (SceneDepth.glsl and NewMesh.glsl SceneDepth_PS under _TAA).
  - Flagged pixels reproject with that vessel's rigid motion: inverse(mWorld_now)·mWorld_prev·Vprev·Pprev.
  - Other vessels use the planet frame and rely on the clamp.
- **M2 no distance reset.** The 2 km rule is gone: the planet-frame reprojection is exact for static ground, and an off-screen prevUV already sets α = 1.
  - A full reset is decided before UpdateCameraFromOrbiter(MAINSCENE), with jitter off for that frame.
  - Reset triggers: a proxy change, internal/external switch, focus change, the mesh-debugger camera, or a previous frame where pass A did not run.
- **M3 order.** The volume shadow pass runs between Ssao() and Clouds().
- **M4 own entries.** The volume shadow has its own entries in VolShadow.glsl, which #includes VolCloud.glsl: PSVolShadow, then VolBlur-style depth-weighted blur, then a VolComposite-style 2×2 upsample.
  - It lives in its own IPI, compiled only when VolCloudShadows = 1, so a failure never turns the volume clouds off.
- **M5 filmic everywhere HDR() runs.** _FILMIC goes into all three planet flag blocks (VPlanetAtmo.cpp) and into HazeMgr's HorizonPS/HorizonRingPS flags (HazeMgr.cpp:270-271).
- **M6/M7 filmic scope.**
  - Filmic replaces 1 − exp(−Ex) only: Scatter.glsl HDR() and Tree.glsl's curve. The volume clouds and the horizon go through HDR().
  - Metalness.fx, LightBlur.glsl and PbrMesh.fx keep x(1+x⁴)^−¼. Their shape is not the same family, so vessels and glass stay as they are.
  - Calibration: a scale of 0.568, tested within 0.04 over x 0.3–2.
- **M8 waves and foam.** The second wave layer uses the integer lattice uv2 = (2u + v, 2v − u), a 26.6° turn √5 finer, scrolled by whole repeats per 3600 s. Foam noise uses ClsNoise with an integer period and a whole number of cycles per 3600 s.
- **M9 normal strength.** After decoding, cNrm.xy is scaled by k = mix(1, 0.15, smoothstep(2e3, 30e3, dst)); z *= wNrmStr as today.
- **S1 glass.** Glass draws after AfterOpaque (RenderPbrDeferred) and stays out of TAA. Legacy see-through groups ghost slightly; a DG canopy check is added.
- **S2 cloud depth.** VolMarch writes tw/ts to a second half-res R32F target (MRT). PSTaa uses it where the volume alpha > 0.3. For depth-0 pixels below the layer it uses the cloud-shell distance. VolMarch steps stay at full count.
- **S3 labels.** With TAA on, the planet label and marker block (planets loop, planetarium, exterior art, surface labels) is drawn after pass A, unjittered, through a Scene::RenderSceneLabels() split.
- **S4 depth age.** SceneFx::DepthValid() returns a frame stamp; tDepth is read only when it was written this frame.
- **S5 flare cloud query.** Tile density is kept when LensFlare = 1 too. The lookup runs inside TileManager2<CloudTile>::Render (mutex held), using VolCloud::CloudLng(lng, cloudrot).
- **S6 flare visibility.** Multiplied by the share of 5 tDepth taps around the sun point that are 0 (when valid), and by 1 − volume alpha at the sun point.
- **S7 glare buffers.** GBUF_DEPTH, pLocalCompute and pLocalResults are also created with LensFlare or LightShafts on. RenderGlares stays gated on bGlares. tVis is sampled point/clamp at u = 0.5/32 with smoothstep(0.5, 1).
- **S8 shadow strength.** The volume shadow alpha is shadow · 0.75 · luminance(atn) · saturate(n·sun), with the normal taken from depth.
- **S9 2D shadow fade.** SurfTile writes vVolCld on every tile: main pass and proxy planet only; z = volume-shadow weight, nonzero only when the shadow pass ran last frame. The fade is applied before the fX line.
- **S10 glint.**
  - Glint: Schlick on V·H. Sky: Schlick on V·N.
  - Far roughness capped at 0.2.
  - Orbit glint check: peak luminance with WaterFx on vs off.
- **S11 water details.**
  - Line references are Custom's (water 587-628, specular 813-814).
  - fShadow already includes fH, so the glint uses fShadow·fShd.
  - The new space-sky term does not use fSrf.
- **S12 foam coast.** 8 taps on a ring at a ~50 m mip, averaged, only where fMask > 0 and dist < 8 km. Texel size comes from vTexOff.zw and the 512 mask width.
- **S13 jitter rebuild.** The ray rebuild subtracts _31/_32. VolPS carries the jitter in vCamZ.w and vMapN.w. SSAOPS gets a vJit member under _TAA, and pAO gets a ppf.
- **S14 Halton.** H(frame%8 + 1).
- **S15 tree dither.** Uses frame%8 from vInsc.w (the frame counter, not time).
- **S16 haze.**
  - Off in vacuum (CameraInSpace ≥ 0.9).
  - Offset samples nearer than the nozzle are rejected through tDepth.
  - Drawn only in the segments' bounding rect (SetTemplate).
  - Uses vVessel::GetInterface and MWorld.
- **S17 checks added:**
  - PostProcess = 1 runs;
  - fullscreen without MSAA;
  - chase behind a DG at 200 m/s;
  - warp ×100 in orbit;
  - VC with MFD;
  - labels on;
  - custom camera MFD and env map (no jitter);
  - ocean over L14 edges;
  - orbit glint;
  - filmic horizon;
  - SunGlare = 0 with the flare on;
  - forced DepthPass cost from LEO.
- **NITs.**
  - Custom line numbers are used.
  - Jitter is cleared at the end of AfterOpaque on every path.
  - The TAA copy-back is a StretchRect.
  - Segments go in vec4 arrays.
  - The history uses a Catmull-Rom 5-tap.
  - The dialog grows by 40.
  - The vignette radius is fixed.
  - Pass B is gated on LightBlur being OK.
  - TerrainSunVisibility is computed once per frame and reused.
  - The new .cpp files go next to SceneFx.cpp in CMake.
- **Work split** (one owner per file):
  - Me: config, dialog, docs, ShaderParams fields, Scene.cpp hooks and the label split, SceneFx.h members, filmic.
  - Agent TAA: PostFx core, PostMath, PSTaa, SceneFx depth and flag, VolMarch MRT, Tree dither.
  - Agent Look: PostLook (haze, shafts, flare, sharpen, vignette) and the flare cloud query.
  - Agent Water: TerrainPS water and the 2D shadow fade, Surfmgr2 per-tile fields.
  - Agent Shadow: VolShadow.glsl and SceneFxShadow.cpp.

## 0. Rules
- Each feature has its own switch in VulkanClient.cfg. Off means the legacy result:
  - shader code sits under a macro that is added only when the switch is on (no SPIR-V change when off);
  - C++ passes are skipped.
- The 2D panel, HUD, GDI overlay and debug text are never touched (they draw after Scene.cpp:2157).
- Auto-exposure keeps measuring the scene: Measure stays before the new final chain, so flare and shafts do not drive exposure.
- Own code only, written from published maths (Karis 2014 TAA, Narkowicz 2015 ACES fit, Mitchell 2007 radial light shafts, Schlick Fresnel). No GPL or other external code is copied. The unused OVP LensFlare.glsl is left as it is.
- One-line comments.

## 1. Switches
| Key | Default | Feature |
|---|---|---|
| LensFlare | 1 | ghosts and halo along the sun line |
| LightShafts | 1 | screen-space sun shafts in air |
| TAA | 1 | temporal anti-aliasing of the 3D world |
| WaterFx | 1 | waves, glitter, Fresnel, sky, foam |
| VolCloudShadows | 1 | ground shadows from the 3D clouds |
| Filmic | 0 | filmic tone curve |
| Vignette | 1 | soft corner darkening |
| Sharpen | 1 | contrast-adaptive sharpening |
| HeatHaze | 1 | refraction behind engine exhausts |

- Read once at start, as the other ground graphics keys are.
- Dialog: a new group "Effects" under Ground graphics, 9 checkboxes in two rows. IDC_GG_* ids 3045-3053. The dialog grows by 30 units; OK and Credits move down.
- Docs: VulkanClient.html (new "Effects" list), README, NEW.tex, LAUNCHPAD.tex.

## 2. Where the passes run (Scene::RenderMainScene)
- **A. End of SceneFx::AfterOpaque (Scene.cpp:1906):** after SSAO, volume clouds and volume cloud shadows, run TAA. Then turn the projection jitter off, rebuilding the projection without it.
  - Markers, exhausts, particles, beacons and the VC cockpit draw later, so they are not jittered and get no history.
- **A2. Right after A:** heat haze. The exhaust flames themselves draw later, on top.
- **B. After LightBlur (2114), before RenderGlares (2120):** PostFx final pass: light shafts (half-res mask and radial blur first), then lens flare, sharpen, vignette.
  - Input: the backbuffer resolved by StretchRect into a single-sample copy.
  - Output: the backbuffer.
  - Measure (2023/2108) has already run.
- New module **PostFx.h/.cpp**, owned by Scene like SceneFx: buffers, passes, exhaust list, history.
  - Shaders: **PostFx.glsl** (IPI entries) and a **PostMath.h/.cpp** pure-maths unit with tests.

## 3. TAA
- **Jitter.** Halton(2,3), 8 phases, in pixels: j = H(frame%8) − 0.5.
  - Scene::SetCameraAperture adds `_31 += 2jx/W, _32 −= 2jy/H` only while `jitterOn`.
  - jitterOn is set just before UpdateCameraFromOrbiter(MAINSCENE) (1422), cleared at A, and never set in the custom camera, env, irradiance or shadow turns.
  - SceneFx ray rebuilds (SSAO vProj and VolMarch vCamX/Y) add the same offset from P._31/_32.
- **Depth.** SceneFx::InitDepth runs when TAA is on, and DepthPass runs every main frame while TAA is on (today only when SSAO or clouds want it).
  - tDepth 0 means sky, or nothing within 1e6 m.
- **Reprojection.** Static world, in the proxy planet's rotating frame. PostMath builds, in double precision on the CPU:
  - `Mcur→prev = T(camNow − plNow) · RnowT · Rprev · T(plPrev − camPrev) · Vprev · Pprev(no jitter)`, for points;
  - `Minf = Vprev · Pprev` (rotation only, global frame), for tDepth 0.
  - The shader rebuilds P = dir·dist from the pixel ray (vCamX/Y/Z as VolMarch does). prevUV comes from P·M (dist > 0) or dir·Minf (dist 0).
  - The previous matrices are kept in PostFx and reset on camera-mode or focus change, and when the camera moves more than 2 km in one frame (history dropped).
- **Resolve (PSTaa).**
  - Current: the resolved copy.
  - History: RGBA16F, bilinear at prevUV.
  - 3×3 neighbourhood mean and σ in YCoCg; the history is clipped to mean ± 1.25σ.
  - α = 0.1, raised to 0.35 when |prevUV − uv| > 2 px. α = 1 when prevUV is off screen or the history was reset.
  - Output to history-next, then copied to the backbuffer (an IP pass into the MSAA target, as LightBlur does). History swaps.
- **Noise sources.**
  - Tree.glsl's screen-door IGN gets `+ frame` (macro _TAA, frame in Prm.vCam), so the dither averages out.
  - SSAO and VolMarch already change per frame.
  - With TAA on, VolMarch steps ×0.75.
- **Cost.** One full-res pass plus a copy (about 0.5 ms at 1080p), plus the depth pass where it did not run before (high altitude).

## 4. Heat haze
- **PostFx::CollectExhausts** runs before pass A2, on the main pass only. For each visible vVessel with exhausts, GetExhaustCount/Spec through its VESSEL interface. Per exhaust with level > 0.02:
  - ref = lpos − ldir·lofs and end = ref − ldir·lsize·level, in vessel-local space;
  - transformed by the vessel's mWorld (camera-relative), then by the unjittered VP;
  - kept: screen p0, p1, half-width w (wsize projected at ref), level, camera distance.
  - At most 32, nearest first. None means no pass.
- **PSHaze** (uniform array of 32 segments): for each pixel, the closest segment gives
  - s = level · (1 − d/w)² · (1 − t)^0.5 · near-fade, with t along the segment;
  - offset = s · 0.006 · (n − 0.5), where n is 2D value noise at uv·(60, 20) scrolled along the segment by time;
  - a sample of the scene copy at uv + offset;
  - with tDepth present, only pixels farther than the nozzle are bent.
- Glass, cockpit and HUD are not touched (A2 runs before them).

## 5. Light shafts (pass B)
- **When:** camera in air (CameraInSpace() < 0.9), the sun above the local horizon, and its screen point within 1.5 screen widths of the centre.
- **Mask, half res:** luminance of the scene copy above a knee (smoothstep 0.6–0.95) × sky test × radial falloff from the sun point.
  - Sky test: tDepth == 0 when tDepth exists, and GBUF_DEPTH.a == 0 when it exists (vessels and the VC cockpit). Otherwise luminance only.
- **Blur:** 48 samples toward the sun point, decay 0.965, weights summed. Two passes of 24 instead if the half-res cost is over 0.3 ms.
- **Composite in PSFinal:** shafts × sun colour (the same CPU colour the glare uses) × 0.35 × air factor (1 − CameraInSpace()), added.

## 6. Lens flare (pass B, inside PSFinal)
- **Sun point:** WorldToScreenSpace2 of the sun direction (unjittered).
- **Visibility:**
  - CPU: GFXGlare-independent 1 × TerrainSunVisibility × (1 − cloud alpha along the sun ray) × the edge fade;
  - GPU: vessel and cockpit occlusion read from pLocalResults texel 0 (the sun glare's own test; 1 when bGlares is off).
  - Cloud alpha comes from a new CloudMgr2 query, `CloudAlphaToward(cameraPos, sunDir)`. Below the layer it reads the tile density where the ray meets the layer. Above the layer it is 0. Inside the layer it is 0.5 × density.
- **Look:**
  - 6 ghosts at t = −0.6, −0.3, 0.2, 0.45, 0.8, 1.1 along sun→centre (t = 1 is the centre mirror). Each is a soft disc of radius 0.02–0.09 screen heights with chromatic tints, and RGB offset by ±1.5 % for colour fringes.
  - A halo ring of radius 0.42 at the centre, strongest when the sun is 0.3–0.6 from the centre.
  - Strength = 0.25 × visibility × sun brightness (the glare's maxRGB(SunLightColor)), × e (exposure). Added after shafts.
- It applies in the cockpit view too (the VC is in pass B's input).

## 7. Sharpen and vignette (end of PSFinal)
- **Sharpen** (contrast-adaptive; own form after the published idea):
  - 5-tap cross, local min and max;
  - w = −a·sqrt(saturate(min(mn, 1 − mx)/mx)), with a = 0.12, or 0.18 with TAA on;
  - c' = (c + w·Σn)/(1 + 4w).
- **Vignette:** c *= 1 − 0.18·smoothstep(0.45, 1.05, r), with r = length((uv − 0.5)·(aspect, 1))/0.5·aspect-corrected.

## 8. Ground shadows from the 3D clouds (SceneFx)
- **When:** VolClouds active this frame (wV > 0), the switch on, and tDepth made.
- **PSVolShadow** in VolCloud.glsl, half res into an R8 buffer:
  - P from tDepth and the ray;
  - a ray to the sun through the slab (Slab() with the sun direction): 12 steps of Density(p, Coverage(p)·Clump, 0) over at most 6 km;
  - T = exp(−od·ext·0.6); shadow = 1 − T;
  - faded with distance 35–60 km (vFade) and with wV.
- **Blur:** the SSAO depth-aware blur reused with its own buffers.
- **Composite:** like SSAO, alpha-blended black with a = shadow·0.75. It lands on terrain, trees, buildings and vessels alike.
- **2D layer shadow fade** (_VOLSHD on the Earth shader):
  - Surfmgr2 sets `vVolCld.z` for surface tiles (the volume weight, as the cloud tiles get);
  - TerrainPS does `fShd = mix(fShd, 1, vVolCld.z · (1 − smoothstep(35e3, 60e3, dist)))`;
  - so the near ground takes only the volume shadow, with no double shadow.

## 9. Water (WaterFx, macro _WATERFX on the Earth shader)
- **Waves:** two tOcean taps, scale 1 and 0.37, the second scrolling 63° off east at 0.7× speed. Normals are whiteout-blended.
- **Normal strength:** `wNrmStr · (1 − smoothstep(2 km, 30 km, dist))·0.85 + 0.15`. Far-off shimmer turns into roughness.
- **Roughness:** `mix(0.08, 0.35, smoothstep(1 km, 80 km, dist))`, so the sun glitter path widens with distance.
- **Fresnel:** Schlick, F0 0.02, × wSpec (replaces 0.15 + 0.85 f³ under the macro).
- **Sky:** in air, GetAmbient(reflect) as today. In space, AmbientApprox(up)·0.6 (today there is none from orbit).
- **Glitter:** the GGX path × fShd × fShadow (the gap at 774-775 is closed under the macro).
- **Colour:** in place of the fixed tint vec3(0, .55, 1)·wBrightness: `mix(cTex.rgb, deep, 0.25·fMask)` plus a sun-facing subsurface `vec3(0.0, 0.08, 0.07)·fDPS·fMask·0.3`, with deep = (0.012, 0.045, 0.075).
- **Foam:**
  - Land fraction near the pixel: `fL = textureLod(tMask, vUVSrf, vWaterFx.x).a`, a mip chosen on the CPU so that one texel is about 200 m.
  - Foam = fMask · smoothstep(0.03, 0.2, fL) · (1 − smoothstep(0.35, 0.7, fL)) · noise(world uv·k + time) · (1 − smoothstep(3 km, 8 km, dist)).
  - Coloured white × lighting.
  - Tile-edge clamp of the mask is accepted (the foam is faint at tile edges).
- New Prm field `vec4 vWaterFx` (x: mask mip, yzw spare). ShaderParams grows to 464 and the static_assert is updated.
- Surfmgr2 computes the mip per tile: `log2(texelSize/200 m)`, clamped to 0–9.

## 10. Filmic (macro _FILMIC)
- Curve: ACES fit (Narkowicz 2015), `f(x) = x(2.51x + 0.03)/(x(2.43x + 0.59) + 0.14)`, with an input scale of 0.57.
  - Calibrated so that f(0.57x) ≈ 1 − exp(−x) at x = 0.3–2 (within 0.03).
  - Shadows get deeper; mid and highlights stay put, so the auto-exposure band still holds.
- Where:
  - Scatter.glsl HDR() (planet, sky, clouds, volume, horizon);
  - Tree.glsl's inline curve;
  - Metalness.fx and LightBlur's HDRtoLDR when they apply;
  - Glare.glsl unchanged.
- The macro is added to the planet flag strings, the SceneFx IPI ppf, the TreeMgr shader flags, the D3D9Effect macros and LightBlur's ppf, only when Filmic = 1.
- Mesh.fx and PBR.fx have no curve today and are unchanged.

## 11. Exposure and order summary
1. Main scene.
2. AfterOpaque: depth, SSAO, volume clouds, volume shadows, **TAA**, jitter off.
3. **Heat haze.**
4. Markers, exhausts, particles, VC.
5. Measure.
6. LightBlur (if on).
7. **PostFx B**: shafts, flare, sharpen, vignette.
8. Glares.
9. GDI, then 2D HUD.

## 12. PBR/glTF parts (Custom)
- **PbrMesh.fx:** its display curve (after LinearToSrgb·gExposure) becomes the filmic f() under _FILMIC. Its compile macros come from PbrRender, which gets _FILMIC and _TAA.
- **TAA jitter:**
  - Check that PbrRender takes the Scene projection each pass (gVP or mProjView); if it caches its own, take the jittered one.
  - glTF opaque groups are already in tDepth (Mesh.cpp opt 2). BLEND groups (glass) are not, so they reproject at the depth behind; the clamp limits the ghosting. This is noted.
- **GBUF_DEPTH (vessel normal-depth):** check that PBR meshes draw in RENDERPASS_NORMAL_DEPTH. If not, add them, so that glTF vessels block the flare, shafts and sun glare.
- **Transmission and glass** (scene copy, Transmitted): they run in the main scene, before TAA. No change.
- **Heat haze** uses EXHAUSTSPEC, which works the same for glTF and XR vessels.
- **Custom's Scene.cpp phases (SetPhase(PHASE_ALL) before AfterOpaque):** pass A sits inside AfterOpaque, so it follows them. Check that no PBR phase draws opaque geometry after AfterOpaque.
- **Checks in Custom:** DG and DGTest glTF at KSC (TAA ghosting on the glTF DG, haze on glTF engines), the PBR material bench with Filmic on and off, and the nested-glass scene with TAA.

## 13. Pure maths and tests (PostMath)
- Halton jitter: in ±0.5 px, mean over 8 phases under 0.07.
- Reprojection: a static planet point maps to its previous pixel under camera translation, rotation and planet rotation within 0.01 px. The sky uses rotation only.
- Flare ghost placement: points on the sun–centre line.
- Filmic calibration within 0.03.
- Haze segment projection: endpoints behind the camera are clipped, and distance from a pixel to a segment.
- Sharpen weight bounds.

## 14. Checks
- **Each switch off:** pixel-identical to the cloud-seams build in Orbit Alps, Alps High, Carpathians Under and a KSC DG view. With all off, identical too.
- **On, one at a time:**
  - sunset KSC looking at the sun (flare, shafts);
  - Alps High (cloud shadows, TAA cloud grain);
  - forest close-up (TAA tree shimmer);
  - a coast at 300 m and 3 km (water);
  - DG with main engines at KSC (haze);
  - Filmic on/off.
- Frame time per switch (NVIDIA, 1080p). No validation-layer errors. ctest.

## Rev 3: as built (implementation and code review, 2026-10-13)
- Light shafts add as a screen blend, c += s(1 - c), with the mask falloff (1 - smoothstep(0, 0.3, r))^2. A plain add of 0.35 turned the low sunset sky white.
- Flare and shafts are off in the 2D panel cockpit (the panel may hide the sun; the final pass runs before it). PSFinal binds only the samplers its variant uses.
- TAA object flag: SceneFx draws the camera target and everything docked or attached to it (recursively) again into an R8 target, depth-tested against the depth pass, instead of an MRT in every depth shader.
- TAA: VolMarch keeps its full step count (S2). Depth-0 pixels under the cloud shell try both the shell and infinity and keep the history nearer the current pixel, so stars do not smear. Late labels follow "TAA runs this frame" (reset frames too). The tree dither moves only while the frame is jittered. The Catmull-Rom history and the box clip are written from the spline's basis and the ray-box definition.
- Known: the celestial sphere's planetarium grids and their labels are still drawn jittered before the resolve, so they are anti-aliased like the stars. VolCloud.glsl's PixelRay extraction changes VolMarch's SPIR-V with every switch off; the arithmetic and pixels are the same.
- Volume shadows: built at scene start when VolClouds and VolCloudShadows are on. They share VolPS's cloud fields with the march through SceneFx::VolParams.
- Water:
  - The glint gets x2 pi, because the lit ground here has no 1/pi and a x2.
  - Specular anti-aliasing widens the lobe by the normal's spread over a pixel (Kaplanyan 2016).
  - Foam is lit as white ground.
  - The mip is log2(20 m / texel), with a 40 m tap ring.
  - Foam is a thin broken line at the waterline, opacity 0.7. Each noise octave fades to its mean below about 2 px per cell.
  - The sky reflection in air blends into the orbit term with fSrf, so nothing pops at the top of the air.
  - There is no wave-exposure data, so lagoon and lake shores get the same thin line as ocean beaches.
