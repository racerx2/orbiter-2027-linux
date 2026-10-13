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
