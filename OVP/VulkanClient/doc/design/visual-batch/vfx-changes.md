# TAA (agent taa): changes

Repo Orbiter-linux-custom, branch visual-fx, on top of 6189b7a9. Switch: `TAA` in VulkanClient.cfg (default 0). Nothing committed.

## Files
- **PostMath.h/.cpp** (pure maths, double): Halton, `Jitter(n)` = H(n%8+1) − 0.5 px (S14); small Vec3/Mat3/Mat4 helpers; `Rigid()` = the camera-relative point now → camera-relative point a frame back for a body fixed in (R, o): A = Rprev·Rnowᵀ, b = A(camNow − oNow) + (oPrev − camPrev), so the 1e11 m offsets cancel in double; `View`, `Proj` (as SetCameraAperture), `ToPixel`, `PixelRay`, `JitterAxis` (CPU mirror of the shader ray).
- **Tests/PostMath.Test.cpp**: Halton values; jitter in ±0.5 px and the 8-phase mean under 0.07 (−0.055, 0.000); a static ground point (heliocentric planet at 30 km/s, tilted and spinning, camera at 2 km flying 250 m/s and turning) maps back to its previous pixel within 0.01 px for all 8 jitter phases, with the ray rebuilt from the jittered pixel and the matrix rounded to float as the GPU gets it; the sky maps by the camera turn only (camera translating fast); a point on a chased, rolling vessel at 7.7 km/s maps back by the vessel's rigid motion; a static camera reads the history at the pixel itself once PSTaa adds the jitter; behind-the-camera is rejected. 1144 assertions pass.
- **PostTaa.h/.cpp**
  - BeginFrame (before UpdateCameraFromOrbiter): reset when the proxy body, focus, camera target or internal/external mode changed, when the last frame's Resolve did not run, with the mesh-debugger camera or wireframe (`skip`: no jitter and no pass), or when SceneFx has no depth targets. A reset frame has no jitter. No distance rule (M2).
  - Resolve (pass A): needs SceneFx::Depth() this frame. Captures the camera (Camera.pos, mView, mProj with _31/_32 zeroed), the proxy (oapiGetRotationMatrix/GetGlobalPos) and the camera-target vessel; builds mPt = Rigid(planet)·Vprev·Pprev, mVs = Rigid(target vessel)·Vprev·Pprev, mSky = Vprev·Pprev in double, uploads as float. Sphere for depth-0 pixels: the cloud shell (Rp + cloudalt) from below the layer, else the planet sphere; c = |o|² − r² computed in double.
  - Passes: StretchRect target → tCur (RGBA16F, resolves MSAA); PSTaa → tHist[next]; StretchRect tHist[next] → target (draw into MSAA targets); target and device state restored as SceneFx::AfterOpaque does. Histories swap.
  - `VkTex *Output()`: this frame's resolved image (RGBA16F) after Resolve ran.
- **shaders/PostTaa.glsl** (new, IPI entry PSTaa): 3×3 mean/σ in YCoCg of 1/(1+Y)-weighted colours, nearest-depth tap (edges move with the front); position from the jittered pixel ray × distance: flagged pixels use mVs, volume-cloud pixels with alpha > 0.3 use the march distance, depth-0 pixels the sphere, else the sky matrix; prevUV + this frame's jitter (the history is unjittered); Catmull-Rom 5-tap history; clip toward the mean at ±1.25σ; α = 0.1, 0.35 above ~2 px of motion (smoothstep 1.5–2.5), 1 off screen / reset; NaN/Inf guards on current and history.
- **SceneFx.h/.cpp**
  - With TAA: InitDepth also when TAA; DepthPass every main frame; `FlagPass`: the camera-target vessel is drawn a second time into an R8 target `tFlag` with the depth pass's depth buffer (LESS_OR_EQUAL), SceneDepth_PS's distance saturating to 1 there; other pixels 0.
  - Public accessors: `DepthOK()`, `DepthValid()` (written this frame), `Depth()`, `VolumeTex()` (tVol[1], premultiplied, half res, when the clouds drew this frame), `Flag()`, `VolumeDist()`, and `CameraRays()` (now public).
  - CameraRays folds the projection jitter into camZ (camZ − X·_31/_11 − Y·_32/_22) when Scene::IsJittered(): VolMarch's and VolShadow's PixelRay need no change.
  - SSAO: pAO gets ppf `_TAA`; PSAO sets `vJit` = (_31/_11, _32/_22).
  - Volume clouds: with TAA, `tVolD` (half-res R32F) as VolMarch's second target, `_TAA` in its ppf, steps ×0.75; output 1 unbound after the march.
- **shaders/VolCloud.glsl**: under `_TAA` a global `volDist` (tw/ts) set in VolMarch and a `location = 1` output only in the PS_VolMarch main. PixelRay and the VolPS fields unchanged.
- **shaders/SSAO.glsl**: `vec2 vJit` at the end of SSAOPS and `r.xy -= vJit` in ViewPos, both under `_TAA`.
- **shaders/Tree.glsl**: under `_TAA` the screen-door IGN is offset by vInsc.w·5.588238.
- **TreeMgr.cpp**: `_TAA` in the shader flags with TAA on; vInsc.w = frame % 8 in the main and distance passes (custom camera keeps 0).

## Not done the way the prompt put it
- Object flag: not an MRT output of SceneDepth.glsl / NewMesh SceneDepth_PS. Every shader that draws into tDepth (terrain, meshes, bases, buildings, the tree distance pass) would have to write location 1, and SceneDepth_PS has no free per-draw value to say "camera target" (ps_const and ps_bools are rewritten per vessel and per group in VVessel/Mesh). The second draw of one vessel needs no shader or Mesh change and leaves the TAA-off SPIR-V untouched. SceneDepth.glsl and NewMesh.glsl are unchanged.
- PBR (section 12): no change needed. PbrRender copies gVP from the legacy effect each BeginVariant (SharedNames, PbrRender.cpp:49, 358), which SetCameraAperture sets with the jitter; RenderShadowMap opt 1 (NORMAL_DEPTH) already draws PBR groups and skips transmission ones; glass draws in RenderPbrDeferred after pass A (S1). Mesh.cpp and PbrRender are unchanged.

## Tests
Builds b1-b3 (/tmp/earth/taa/b*.log): BUILD EXIT=0; Tests/PostMath.Test: all 1144 assertions pass. Shots /tmp/earth/shots/taa*-{20,21,28}.png (1 = TAA on, 0 = off), client logs /tmp/earth/taa*.client.html: no shader errors, no [ERROR] besides the usual undeleted-surfaces list at exit; no NaN/black pixels. Compare scripts /tmp/earth/taa/cmp.py, diff.py.
- **Gfx Forest** (taa1f/taa0f): the tree band screen-door checkerboard and the jagged skyline are gone with TAA (crop gradient energy at the skyline 11.2 vs 15.2).
- **Clouds Land Alps High** (taaAH1/0): ridge and cloud-over-ground edges smooth, no ghosting at the volume cloud edges.
- **Clouds Land Germany Under** (taaGU1/0): the haze/ground grain is gone (gradient energy 6.9 vs 8.9).
- **KSC DG** (taaK2/taaK0 with MSAA 4, taaKn1/taaKn0 with SceneAntialias=0): without MSAA the wing and fin stair-steps are smoothed; with MSAA the image matches off apart from noise. Static scene frame-to-frame change (shots 1 s apart): on 0.21 mean / p99 4 levels, off 0.03 / 2 (residual jitter ≤ 4/255); the Blackman-Harris current halved it (point-sampled current: 0.34 and 0.60).
- **Chase, DG-S flying** (new build-tree scenario Scenarios/Tests/Gfx TAA Chase.scn, from "Cruising above Florida"; taaC1/0): the vessel stays sharp, no smear; sky grain removed; frame-to-frame 2.2 vs 3.1.
- **Orbit Alps** (taaO1/0): dither grain removed, no ghosting.
- **Night KSC** (taaN2/taaN0): stars keep 94 % of their energy (25 brightest), peak 73 % (spread by the jitter).
- **PostProcess=1** (taaP1): runs, no errors.
- **Virtual cockpit in LEO** (Demo/Virtual cockpit, taaV1): HUD and cockpit sharp (drawn after pass A), Earth not smeared at 7.5 km/s.
- **TAA alone** (KSC, SSAO=0 VolClouds=0, taaS1): SceneFx makes tDepth for TAA, works.
- Final build b4: BUILD EXIT=0, PostMath.Test passes, build-tree shaders equal the sources.

## Not tested
- Time warp ×100 in orbit, labels/planetarium on, custom camera MFD, env maps (no scenario hooks for these here). Labels are the integrator's late block (bLabelsLate); jitter is only set by BeginFrame after the custom/env turns.

## Notes for other owners
- PostLook: `PostTaa::Output()` is the resolved scene (RGBA16F, single sample), valid in the frame Resolve ran. The late labels (bLabelsLate) are drawn into the target after it, so it is not a full scene copy for the haze pass where labels can sit.
- VolShadow (SceneFxShadow.cpp): CameraRays now carries the jitter in camZ, so its PixelRay matches the jittered tDepth with no change on its side.
- Residual jitter: a static scene changes by up to 4/255 (p99) frame to frame with TAA on; stars come out ~25 % lower in peak, energy kept.
# Look passes: heat haze, light shafts, lens flare, sharpening, vignette (agent "look")

Repo /home/racerx/Projects/Orbiter-linux-custom, branch visual-fx, on top of 6189b7a9 (uncommitted). Final build BUILD EXIT=0; LookMath.Test: all 1451 assertions in 7 test cases pass.

## Files
- **OVP/VulkanClient/PostLook.h/.cpp** (stubs filled)
  - Ctor, per switch:
    - HeatHaze: IPI `PSHaze`.
    - LightShafts: two half-res R16F targets and IPI `PSShaftMask` + `PSShaftBlur` (ppf `_SHAFTS`).
    - IPI `PSFinal`, compiled with `_SHAFTS` / `_FLARE` / `_SHARPEN` / `_VIGNETTE` only for the switches that are on.
    - Each IPI fails on its own (LogErr, that part off). The scene copies are made lazily.
    - Logs `PostLook: haze 1, final [...], SceneFx depth 1, volume 1`.
  - `Haze(target)`:
    - Off in vacuum (CameraInSpace() >= 0.9).
    - `Exhausts()` walks Scene's vobj list with the exhaust loop's filter. Per exhaust with GetExhaustLevel > 0.02:
      - `ref = lpos - ldir*lofs`, `end = ref - ldir*lsize*level`;
      - through vVessel::MWorld, then Camera.mProjView (unjittered: TAA cleared the jitter before Haze);
      - LookMath::ProjectSegment clips the ends at w = 0.05 m.
    - Half-widths: 1.2 x wsize at the nozzle, 2 x at the end, projected per end and capped at H/4.
    - Strength = level x near fade (camera within 1-4 nozzle widths of the plume) x size fade (half-width / 30 px).
    - Keeps the 32 nearest, as 3 vec4 each (`vSeg[96]`), and their pixel bounding rect.
    - StretchRect copy of the target (resolves MSAA), then PSHaze drawn only in that rect (SetTemplate). The main target, viewport and state are restored as SceneFx does.
  - `Final(bb)`:
    - Skipped when pOffscreenTarget exists and LightBlur is not OK.
    - Sun point from WorldToScreenSpace2 (unjittered).
    - Shafts when the camera is in air (CameraInSpace < 0.9), the sun is over the local horizon (dip included, smooth over -0.6..1.7 deg) and the sun point is within 1.5 widths of the centre (fades 1.0-1.5). Strength 0.35 x (1 - CameraInSpace).
    - Flare visibility, CPU part = TerrainSunVisibility (own TerrainSunCache, from the camera, vNear as the glare) x (1 - CloudSunAlpha) x EdgeFade. Strength = 0.25 x that x maxRGB(SunLightColor(…, 2.0)) x e (e clamped 0.25-2).
    - Returns with no pass when shafts and flare are both 0 and Sharpen and Vignette are off.
    - Otherwise: a copy of the backbuffer, then the shaft mask, the shaft blur, then PSFinal into the backbuffer. Sharpen amount 0.18 when the frame was jittered (Scene::bLabelsLate), else 0.12.
  - SceneFx `Depth()` and `VolumeTex()` are used through a C++20 requires-check, so the code compiled before agent TAA added them. They are present now (log: depth 1, volume 1).
- **OVP/VulkanClient/shaders/PostLook.glsl** (new). One LookPS block at binding 1. Samplers 4-9: tScene, tDepthL, tGbuf, tShaft, tVis, tVolL.
  - `PSHaze`:
    - The segment with the largest s = level*(1-d/w)^2*(1-t)^0.5 (t mapped to the unclipped plume).
    - Offset = s*0.006*(n-0.5) screen heights. n is value noise (own integer hash: Teschner primes, Murmur3 finaliser) in plume-aligned coordinates, scrolled along the plume.
    - With Depth(): a pixel nearer than the nozzle is not bent, and an offset sample nearer than the nozzle falls back to the pixel itself (margin 2 % + 1 m).
  - `PSShaftMask` (half res): smoothstep(0.6, 0.95, luma) x sky (Depth() == 0 when valid, GBUF_DEPTH.a == 0) x (1 - smoothstep(0, 0.3, r))^2 around the sun.
  - `PSShaftBlur`: 48 taps toward the sun, decay 0.965, normalised by the weight sum. The march covers 90 % of the way, at most one screen height, with an IGN start offset. Taps off screen count as 0.
  - `PSFinal`, in order:
    - sharpening: 5-tap cross, w = -a*sqrt(saturate(min(mn,1-mx)/mx)), c' = (c+w*sum)/(1+4w). It is applied to the scene before the smooth additions, which is equivalent and keeps their slopes out of the weight;
    - shafts x sun colour x strength, added as a screen (x (1-c)), so a bright sky does not clip to white (see Notes);
    - flare, additive:
      - 6 ghosts at t = -0.6, -0.3, 0.2, 0.45, 0.8, 1.1 (t=0 centre, 1 the sun's mirror), radii 0.02-0.09 screen heights, tinted, RGB at +-1.5 % for fringes;
      - halo ring at radius 0.42 with weight smoothstep(0.1,0.3,d)*(1-smoothstep(0.6,0.9,d)), brighter on the sun's side;
      - multiplied by the GPU visibility: tVis texel 0 at u = 0.5/32, point, smoothstep(0.5,1), as GlareVS; x the share of 5 Depth() taps around the sun point that are 0; x (1 - VolumeTex alpha at the sun point);
    - vignette: c *= 1 - 0.18*smoothstep(0.45, 1.05, r), r = |(uv-0.5)(W/H,1)| / |(0.5 W/H, 0.5)|.
- **OVP/VulkanClient/LookMath.h/.cpp** (pure, no Vulkan):
  - Filmic (kept), Smoothstep;
  - GhostT / GhostPos, HaloWeight, EdgeFade;
  - ProjectSegment (row-vector view-projection, clip at wmin, kept t0/t1), SegDist, HazeStrength;
  - SharpenWeight, Sharpen, Vignette.
- **Tests/LookMath.Test.cpp**: the filmic test kept, plus:
  - ghosts collinear with the sun and the centre (t=-1 the sun, t=1 the mirror);
  - halo and edge fade bounds;
  - segment projection to pixels, clipping either end behind the camera, wholly behind gives false;
  - pixel-segment distance;
  - haze strength monotonic and 0 at the edge and at the plume end;
  - sharpen weight in [-a, 0], 1+4w > 0.25, flat input unchanged, peaks up and pits down, output >= 0;
  - vignette 1 in the middle, 0.82 at the corners, symmetric.
- **OVP/VulkanClient/Cloudmgr2.h/.cpp** (cloud alpha query only; atlas code untouched):
  - CloudTile::PreLoad keeps the 256x256 density when `VolClouds || LensFlare`.
  - New `float CloudAlphaToward(TileManager2<CloudTile>*, cameraPos, sunDir)`, loader mutex held. In planet-local space:
    - below the layer: the finest loaded density where the ray meets the cloudalt shell, through VolCloud::CloudLng(lng, cloudrot) and VolCloud::Src::Covers/At;
    - inside (cloudalt ±1 km): 0.5 x the density over the camera;
    - above: 0;
    - smooth blends between the three;
    - x the CloudPS volume fade mix(1, smoothstep(35, 60 km, dist), GetVolCloudFade()), so the volume clouds (tested on the GPU) are not counted twice.
  - `TileManager2<CloudTile>::Render`, under its mutex, for the main pass and the camera's proxy with LensFlare on, stores it per frame. `CloudSunAlpha(mgr, frame)` returns it, or 0 for another frame.
  - The new functions sit after the SearchTile specialisation, to avoid "specialisation after instantiation".
- **Build tree only** (Scenarios/Tests, not in git):
  - `Look KSC Sunset.scn`: a ground camera 13 m up, SE of a DG on the SLF, sun 7 deg up in the upper left;
  - `Look KSC Sunset Cockpit.scn`: DG cockpit facing the sun;
  - `Look DG Engines.scn`: main at 25 %, close behind;
  - `Look DG Engines Side.scn`: main at 60 %, rear quarter, KSC buildings behind.
  - The window is 1920x1080 and run.sh crops the top-left 1280x720, so the scenes put the sun up and to the left.

## Tests (shots /tmp/earth/shots/<tag>-NN.png; diffs /tmp/earth/look/d*.png made with /tmp/earth/look/diff.py)
- **KSC sunset**, all on vs all off, TAA=0: lookS4 / lookS6 / lookS7 (final shader) vs lookS4off.
  - Ghosts on the sun-centre line at (605,355), (1077,600) and (1225,680), and the halo arc. Matches the debug values: sun uv (0.196, 0.217), shafts 0.333, flare 0.143 (maxRGB 0.57, e 1.0, terrain 1, cloud 0, Depth and VolumeTex valid).
  - Shaft glow and rays toward the horizon, vignette at the corners, sharpening on edges. HUD text unchanged.
  - Final lookS7 vs off: mean abs diff 5.9.
- **SunGlare=0** (lookS4g): the flare is still drawn. Its diff vs off matches lookS4's block by block (within 1.3).
- **Cockpit** (lookC1 vs lookC1off, TAA on): ghosts, halo and shafts under the glass HUD. The HUD pixels are unchanged (they draw after Final).
- **PostProcess=1** (lookS5pp): LightBlur runs, then Final (shafts, flare). No errors.
- **TAA on** (lookS5taa): works. Sharpen amount 0.18.
- **Orbit** (lookO1 vs lookO1off, Gfx Clouds Orbit Alps): CameraInSpace 1.0, so no shafts. The sun is behind the camera, so no flare. Only sharpening (detail edges) and vignette (corner -25/255 on bright cloud).
- **DG main engines** (lookD1 vs lookD1off, lookE2 vs lookE1off; only HeatHaze differs):
  - 2 segments, rect (199,439)-(878,757) at 1080p; segment 0 is (742,576)->(392,621), half-width 62->129 px, s 0.6, nozzle 28.8 m.
  - lookD1 vs off: only the plume region changed (mean abs 0.066, 2794 px > 2, all at the nozzles).
  - Shimmer is visible on the wing edge behind the plume. Parts nearer than the nozzle are not pulled in (no ghosting).
- **Each switch off**:
  - HeatHaze=0 leaves the image identical outside the plume (lookD1off);
  - all five off: PostLook is not created (Scene.cpp), so no pass, buffer or shader (lookS2off, lookS4off, lookO1off, lookC1off);
  - LensFlare=0 also leaves Cloudmgr2 as before (no density kept unless VolClouds, no lookup).
- **Cloud query** (VolClouds=0, temporary debug log, since removed): CloudSunAlpha read 0.154 under the thin layer in Gfx Clouds Land Germany Under (lookK1, 1.8 km), 0.62 in Gfx Clouds South Under (lookK2, 1.5 km), and 0.003 in Gfx Clouds Inside (lookK3, 7.5 km, near the top). With VolClouds on, it is ~0 near the camera by design.
- **Client logs**: no shader or PostLook errors. The only [ERROR] lines are the "UnDeleted Surfaces" list at exit, the same 84 lines in the all-off runs.

## Notes / left
- Shafts composite: the design's plain add of 0.35 x the normalised rays turned the low sunset sky white over ~0.4 screen heights (lookS3/S4/S6, +20..27/255 around the sun). I kept 0.35 and the knee, tightened the mask's radial falloff to (1-smoothstep(0,0.3,r))^2, and add as a screen, c += s*(1-c). Dark occluders get the full rays and the bright sky stays unclipped. Revert to a plain add in PSFinal if the old look is wanted.
- The haze is subtle by design (0.006): at most 0.003 screen heights (about 3 px at 1080p) at full throttle.
- With the volume clouds on, the CPU cloud alpha is ~0 near the camera by design (the GPU VolumeTex term covers it).
- The debug logging used during testing is removed. Final build b7: BUILD EXIT=0. The build-tree PostLook.glsl is identical to the source.
- Requests for other owners:
  - (Scene.cpp, integrator, optional) NIT 13 "TerrainSunVisibility once per frame": RenderGlares keeps its own static cache and PostLook has its own (both cached, cheap). Sharing one needs a Scene member.
  - Frame time per switch was not measured; no GPU timer is reachable from here.
# vfx: better water and the 2D cloud shadow fade (agent "water")

Repo Orbiter-linux-custom, branch visual-fx (HEAD 6189b7a9). Uncommitted. Only my two files were edited.

## Files
- **OVP/VulkanClient/shaders/NewPlanet.glsl** (TerrainPS, all new code under `_WATERFX` or `_VOLSHD`):
  - `vUVWt0`: the unscrolled ocean uv (whole repeats per L14 tile). `fFoam` declared at the top.
  - Second wave tap on the lattice uv2 = (2u + v, 2v − u) (26.6°, √5 finer), scrolled by (24, 18) repeats per 3600 s, so the Time wrap is seamless.
  - Whiteout blend on unit normals, then scaled back to the stored z (0.5 in D3D9Ocean.dds), so `z *= wNrmStr` acts as today. Then `xy *= mix(1, 0.15, smoothstep(2e3, 30e3, dst))`.
  - Fresnel: Schlick F0 0.02 × wSpec × fMask; the glint on V·H, the sky on V·N.
  - Roughness: `min(mix(0.08, 0.35, smoothstep(1e3, 80e3, dst)), 0.2)`, plus a specular AA term from the pixel derivatives of the water normal (Kaplanyan 2016, α² += min(2σ², 0.18)). This is not in the design: without it the 0.08 lobe sparkle-aliased at 1–4 km.
  - Glint gain × 2π (not in the design, see Results). The lit ground has no 1/π and a ×2, so a physical F0 alone made the glint about 7× too weak against the water.
  - Sky: in air GetAmbient(reflect) × F × fSrf as today. In space AmbientApprox(vPlN).rgb × a × 0.6 × F, without fSrf.
  - Colour: `mix(cTex, (0.012, 0.045, 0.075), 0.25 fMask) + (0, 0.08, 0.07) · max(fDPS, 0) · fMask · 0.3`, then × (1 − F_sky). This replaces the fixed tint.
  - Foam (only where fMask > 0 and dst < 8 km):
    - fL is the mean of 8 `textureLod(tMask)` taps on a 100 m ring at the Prm.vWaterFx mip.
    - The noise is two ClsNoise octaves on vUVWt0: 64 and 256 cells per repeat, with periods 64 and 256 and shifts (64, 128) and (512, 256) per 3600 s, so it is seamless over tiles and over the Time wrap.
    - foam = fMask · smoothstep(0.03, 0.2, fL) · (1 − smoothstep(0.35, 0.7, fL)) · smoothstep(0.4, 0.7, n) · (1 − smoothstep(3e3, 8e3, dst)).
    - Foam scales the glint and the sky reflection by (1 − foam).
    - It is lit as white ground after the lighting line: `pow(0.8, TrGamma) · TrExpo · (cSF · fX · fShadow · fShd · 2 + cA + ambient)`. Lit as water (fDPS²), it came out darker than the sky reflection.
  - Specular: `cSun · fSpe · smoothstep(fDPS) · fShadow · fShd` (fShadow already has fH).
  - `_VOLSHD`: `fShd = mix(fShd, 1, Prm.vVolCld.z · (1 − smoothstep(35e3, 60e3, dst)))` just before the fX line.
- **OVP/VulkanClient/Surfmgr2.cpp** (SurfTile::Render, after fTgtScale; written on every tile):
  - `vWaterFx = (mip, ru, rv, 0)`, where:
    - texel size is tile extent (bnd, lng × cos lat) / (512 × texrange size);
    - mip = clamp(log2(50 m / max(tu, tv)), 0, 9). Note: the design's log2(texel/50) has the wrong sign;
    - ru, rv = 100 m ring radius in mask uv, capped at 0.05.
  - `vVolCld = (35e3, 60e3, z, 0)`, where z = scene->GetVolShadowFade() in the main pass for the camera's proxy planet, else 0.

## Build
- /tmp/earth/water/b2.log: BUILD EXIT=0. The build-tree NewPlanet.glsl matches the source.

## Off = old
- glslangValidator compile of the old and new NewPlanet.glsl (TerrainPS and TerrainVS) with the Earth flags without _WATERFX/_VOLSHD gives byte-identical SPIR-V (also checked after the last edit). The C++ only fills the two Prm fields, which the off shaders do not read (vVolCld is read only by CloudPS, and cloud tiles write their own).
- The shaders compile with both macros, also without _RIPPLES. No shader errors in the client logs.

## Tests (TAA, LensFlare, LightShafts, Vignette, Sharpen, HeatHaze = 0 in all runs; shots /tmp/earth/shots/<tag>-24.png)
- New scenarios (build tree only): Tests/Gfx Water Coast 300, Coast 3km (KSC), Sea 1500, Orbit, Orbit Atlantic, Orbit Glint.
- **Coast 300 m** (wat9on vs wat1off):
  - glint path brighter and wider; navy water instead of the light tint;
  - sparkle density like legacy after the AA term (v1 without it and with raw-z whiteout: a speckle field);
  - a thin line of white foam flecks on the beach line.
- **Coast 3 km** (wat8on / watCon vs wat2off): a mottled white surf band, about 100 m wide, along the Cape beach and lagoon shores. Ponds smaller than the ring get none. A debug run (watd1/watd2: R fL, G foam) confirmed fL ≈ 0.5 at the waterline and a foam band out to about 100 m.
- **Sea 1500 m, many L14 edges** (watAon vs watAoff):
  - crossed waves and a strong glint path;
  - no seams at ×3 contrast;
  - glint p99.9 luminance 253 on vs 205 off.
- **Orbit glint** (Gfx Water Orbit Glint, camera on the computed specular point; wat7on vs wat7off): centre ocean patch p99.9 luminance 104 vs 108, max 113 vs 115.
  - The water is about 7 % darker overall, from the removed blue tint.
  - Without the 2π gain (wat3 Mediterranean, v1), the glint area was 25 % darker (86.6 vs 116.1).
- **Alps High, VolCloudShadows 1 vs 0** (watB1 vs watB0, with the shadow agent's SceneFxShadow.cpp of 10:52):
  - within ~35 km the 2D layer's shadows are gone and the volume shadows show;
  - smooth fade to 60 km; no change beyond.
- A diagonal colour seam in the Atlantic orbit view is in both on and off (Surf imagery), not mine.

## Left / notes for others
- Foam also appears on lagoon and lake shores, since there is no wave-exposure data. The mask tile-edge clamp is accepted as designed.
- The water is darker than the legacy look by design (no tint). wBrightness is no longer used under _WATERFX.
- Nothing is needed from other owners.
# vshd: ground shadows from the volumetric clouds (VolCloudShadows)

Agent vshd. Design: design-visual-batch section 8, rev 2 M3, M4, S8 (S9 is the water agent's, it reads volShdFade).

## Files
- `OVP/VulkanClient/shaders/VolShadow.glsl` (new). It #includes VolCloud.glsl and reuses its VolPS block, Slab, Coverage, CloudQ, Clump, Density, PixelRay and DepthW. Own sampler `tShd` at binding 18. No own uniform block: wV goes in vMarch.z, as VolMarch does.
  - `PSVolShadow` (half res, into R8):
    - Builds P from tDepth at the full-res texel the half-res texel stands for (`FullUV`), plus PixelRay.
    - tDepth 0 or beyond vFade.y (60 km) gives 0.
    - Takes Slab(P, toSun) and caps the path at 6 km.
    - Marches 12 midpoint steps of Density(p, Coverage(p)·Clump, 0), with VolMarch's clump fade over 25-35 km from the camera. Detail noise is skipped and there is no jitter, so there is no per-frame noise.
    - Result: (1 − exp(−od·0.04·0.6)) · (1 − smoothstep(35, 60 km, dist)) · wV.
  - `PSVolShdBlur`: 3×3 depth-weighted, as VolBlur (DepthW, centre weight 2).
  - `PSVolShdComposite`: VolComposite's depth-aware 2×2 up-sample at full res.
    - Output is black with alpha = shadow · 0.75 · luminance(GetLandView atn) · saturate(n·toSun) (S8).
    - The normal comes from tDepth, taking the nearer neighbour on each axis (as SSAO's ViewNormal, in geocentric axes).
    - tDepth 0 (sky) gives alpha 0.
- `OVP/VulkanClient/SceneFxShadow.cpp`:
  - `struct VolShadowState`: the IPI and two half-res R8 targets.
  - `FreeVolShadow`.
  - `VolShadow(target, vp, wV)`:
    - Builds its own ImageProcessing lazily, on the first call. That only happens with VolCloudShadows = 1, so with the key off nothing is compiled or created.
    - ppf is `_PERFORMANCE` when !bAtmoQuality, so the scatter table sizes match.
    - On a failure it logs once and stays off: volShdFade stays 0 and the volume clouds are untouched.
    - It returns unless DepthValid() (S4).
    - It copies Clouds()' VolPS setup (CameraRays, so the TAA jitter comes for free; map frame, cloud frame, slab, vFade, vNoise, vTex) and binds tDepthV, tCov, tShape, tLndAtn.
    - It runs shadow → blur → composite (Execute(1, true)) into the target.
    - volShdFade = wV only when the composite really drew, 0 otherwise.

## Tests (Xvfb :58, 1920×1080 render, NVIDIA)
- Builds b1–b3: BUILD EXIT=0, no warnings.
- Offline glslangValidator compile of all three entries, with and without _PERFORMANCE: OK.
- Client logs have no shader [ERROR] and no "Invalid variable name". The only [ERROR] lines are the pre-existing "UnDeleted Surfaces" at exit.
- Debug shader runs (raw shadow painted red, from a build-tree-only copy):
  - vshDbgA (Alps High): shadows lie on the anti-sun side of the clouds. The sun is at azimuth about 157°, elevation 38°, to the left of the view, and the mountains' lit faces agree.
  - Same run: no shadow in the sky, and the shadow fades out by about 60 km.
  - vshDbgC (Carpathians Under): overcast, the near ground is shadowed except the gaps.
  - vshDbgS (South Under, ocean): the shadow gap matches the layer gap.
  - vshDbgG (Germany Under): almost no clouds there, so almost no shadow.
- On/off pairs (TAA=0, other new effects off):
  - vshA1/vshA0 (Alps High).
  - vshC1/vshC0 (Carpathians Under): mean luminance ratio 0.93. The gaps are brighter because the 2D shadow fades by volShdFade; the rest is darker from the volume shadow.
  - vshH1/vshH0 (Germany High): almost cloudless there. Only one small cloud shadow, plus faint changes where the 2D shadow faded.
  - Switch off: VolShadow is never called, so there is no IPI and no shader, and the output is the legacy one.
- Cost, measured with GPU timestamps around the three passes (temporary code, now removed):
  - 0.094 ms (Alps High), 0.083 ms (Carpathians Under), 0.079 ms (Germany Under), 0.083 ms (South Under);
  - mean frame interval 21.1–21.6 ms in all runs.
- Stability: vshA2, the final build, shot at 20 s and 21 s with a static camera.
  - The frames differ only at the cloud edges (the clouds' own per-frame noise).
  - The shadowed terrain does not change, so there is no noise crawl.
- TAA on: vshT1 (Alps High, TAA=1, final build). No errors, and the shadows look the same as with TAA off.
  - CameraRays already folds the jitter into camZ, so this pass follows it.
- The build-tree debug shader (VolShadowDbg.glsl) has been deleted. Scratch files are in /tmp/earth/vshd.

## Notes / for other owners
- The VolPS constants (vNoise 1/12000, 1/1500, 0.35, 1; vFade 35e3, 60e3, 80e3, 0.04; the slab at cloudalt − 1000 + 3000) are copied from SceneFx::Clouds. If Clouds changes them, change SceneFxShadow.cpp too, or factor out a shared helper in SceneFx (owner of SceneFx.cpp).
- The 2D cloud shadow is a straight-down projection; the volume shadow follows the sun. In the 35–60 km cross-fade, the two shadows of one cloud sit in different places (by cloud height / tan(sun elevation)). The design accepts this.
- The S8 strength is applied as written: n·sun scales linearly, so flat ground at a 38° sun gets about 0.62 of the 0.75.
