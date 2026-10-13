# UE5 batch: screen-space reflections (owner "ssr")

Repo /home/racerx/Projects/Orbiter-linux-custom, branch ue5-batch (on 35e2949c), uncommitted. Switch `SSR` (default 1). Design: design-ssr.md with "Rev 2 (implementation)", which overrides the body. All of Rev 2 is in; no placeholders, no temporary code (the GPU timing hooks used for the numbers below were removed before the final build).

## New files (owned)
- `OVP/VulkanClient/SceneSsr.h/.cpp`: the capture and the per-draw parameters.
  - Ctor: tSsrCol (RGBA16F W×H, full mip chain), tHiZ (R32F, L = min(9, ⌊log2 max⌋+1) levels, padded to multiples of 2^(L−1): 2048×1280 at 1080p, M2), tHiZTmp scratch (Wp/2×Hp/2, L−1 levels), one VkSurf per level, IPI HiZ.glsl (PSHiZ0, PSHiZDown, PSUnfog). A failure logs once and SSR stays off.
  - `Capture(target)` (Scene.cpp after PostTaa::Resolve, INT-5): colour from PostTaa::Output() (else the target; StretchRect resolves MSAA); with SceneFx::FogTex() this frame it goes through PSUnfog (c = (c' − f)/max(1 − a, 0.05)), so the fog is applied once per reflecting pixel (S5/INT-5); PSHiZ0 (tDepth distance → 1/z with CameraRays, jitter still on), then L−1 × (PSHiZDown into the scratch + NEAREST blit into tHiZ level k); PrepareSample of both textures (NIT 3); state and main target restored. Records the view (cam, camZ, VP unjittered, jitter, exposure, proxy, terrain recorded) and the rigid frames of the proxy and every visible vessel (Scene's visual list).
  - `Prepare()` (first main-pass request of the frame): reset rules (proxy/focus/target/internal change, no capture last frame, camera jump > 1 km + 2× last move in the proxy frame or turn > 45°, mesh-debugger camera or wireframe).
  - `Params()`: "previous" mode (before the capture: mPrev = SsrMath::BodyMatrix of the drawn body, a body missing last frame uses the proxy's) or "current" mode (after it: deferred glass, VP now, deterministic); vHiZ (W, H, top level, 48 iterations), vJit (last jitter px, start lift 1.5 px angle, near 0.1 m), vMode (valid, stochastic when TAA, phase f%16, sky weight), vThick (0.25 m, 0.015, α fade 0.20-0.36), vDecode (e_now/e_src, 1/e_src, colour mips−1, edge 0.06), vSurf (kind; base surfaces the dry model (1, 0.30, 0.04, 1), M4).
  - `PlanetBind()` (Surfmgr2, every Earth Render call, every pass, zero = valid 0 when nothing traces, M1): only the camera proxy in the main pass under 30 km, and only when last frame's proxy terrain was in the depth (NIT 5); sky weight 1 − smoothstep(3, 12 km), 0 with the planetarium.
  - `Scope` (RAII, VVessel::Render and VBase): gSsr + tSsrCol/tSsrHiZ on the legacy effect for one vessel/base, zero gSsr on exit; main pass and exterior only (VC, env, custom camera, shadow and depth passes get valid 0).
- `OVP/VulkanClient/SsrMath.h/.cpp`: pure double maths, CPU mirror of the shaders: Hdr/HdrInv, Filmic/FilmicInv, MeshCurve/Inv, sRGB pair, MeshEncode/Decode, Segment (McGuire & Mara setup with near clip), Vanish, HiZLevels, BuildHiZ, Walk (the Hi-Z walk of Ssr.glsl with the M3 toward-camera rule), March (dense brute force), SkyAt, Lift, KeepAbove, Reflect, Vndf (Heitz 2018), Ign, ConeLod, EdgeFade, Smoothstep, EnvBRDFApprox (Karis 2014), WetModel (Lagarde 2013), JumpTest, BodyMatrix.
- `shaders/SsrPrm.glsl`: the 224-byte struct (static_assert in SceneSsr.h).
- `shaders/Ssr.glsl`: SsrDir (mirror, or one VNDF sample per frame with IGN while TAA runs), SsrKeepAbove, SsrEdge, SsrTrace (screen segment, Hi-Z walk from level 1, thickness τ0 + τrel·z, sky lookup only when the walk reached t = 1 unclipped and level 0 is empty there, S3), SsrFetch (cone LOD), SsrEnvBRDF, SsrMeshDecode, SsrBase (kind 1 weight). No blocks or samplers (hosts declare them).
- `shaders/HiZ.glsl`: PSHiZ0, PSHiZDown, PSUnfog.
- `Tests/SsrMath.Test.cpp`: 9 cases (curve pairs incl. y = 0.985/1/2, mirror identity, Hi-Z build on odd sizes, walk vs dense march away from and toward the camera, thin pole and sky rays, near clip, body matrices, VNDF CDF, cone/edge/base weight/wet/jump units).

## Shared files (SSR regions only, `// not upstream`)
- NewPlanet.glsl: `gSsr` appended to PlanetPSBlock after `gCsm` (M1, INT-2); tSsrCol 26 / tSsrHiZ 27 with `#error` under `_DEVTOOLS`; SsrPlanetDecode (HDRInv with the 0.985 clamp, filmic root, S2); the trace in the ripple block (`_WATERFX`: α_ssr = 0.35·fRgh, Fresnel fFS.y; legacy: fF.y·fSrf, off in space), early out 0.75·F < 0.01 (NIT 7); the mix after `sct` (finite hits lose the camera-to-water haze, sky lookups as they are, × (1 − fFoam), S1).
- D3D9Client.fx: SsrPrm.glsl include, `SsrPrm gSsr` at the end of FxParams after gCsm, samplers 29/30 + Ssr.glsl after CsmS (INT-3).
- PBR.fx: PBR_PS env term (vessels, kind 0; `_GLASS` gFresnel term at LOD 0) and the kind-1 base surface term (PBR_PS and FAST_PS); Vessel.fx AdvancedPS env (vessels); Metalness.fx cEnv (vessels); Mesh.fx BaseTilePS kind-1 term (wrapped in STAGE_PS).
- PbrMesh.fx: samplers 30/31, `gSsr` at the end of PbrShared after gCsm; EvalIBL specular (bent normal under PBR_ANISOTROPY) or the clearcoat ray when the coat is the smoother lobe (one trace per pixel).
- VPlanetAtmo.cpp: `_SSR` for `bWater && bRiples && SSR && !EnableMeshDbg`. D3D9Effect.cpp: `_SSR` in BaseMacros. PbrRender.cpp: gSsr, tSsrCol, tSsrHiZ shared with the variants only when SSR = 1. VVessel.cpp: Scope next to PbrEnvScope (opaque and deferred glass both pass there). VBase.cpp: Scope kind 1 (RenderSurface), kind 2 (RenderStructures). Surfmgr2.cpp: one line `SceneSsr::PlanetBind(scene, vp, pShader, depthRec)` (integrator file, listed).
- Scene.cpp/.h (integrator, already in): create/delete, `pSsr->Capture(pBackBuffer)` after the TAA resolve, friend.
- Not used: SceneFx::Normal() (ssgi). Every host traces from its own exact normal; back-face hits stay hits (R2, accepted, Rev 2).

## Tests
- Build: /tmp/earth/vfx/build.sh, b4 and b5 (final, timing hooks removed), BUILD EXIT=0, no warnings in SSR files.
- SsrMath unit test (standalone compile of Tests/SsrMath.Test.cpp + SsrMath.cpp + PostMath.cpp, /tmp/earth/ssr/t.sh; ctest left to the integrator): 9 cases, 486420 assertions, all pass. These cover the walk vs a dense march (away from and toward the camera), Hi-Z build on 37x23 and 1x5, curve round trips within 1e-4 up to y = 0.985 (clamped at 1 and 2), mirror identity within 1e-3, VNDF CDF within 0.01, body matrices within 0.01 px, and the jump test.
- Offline glslang (/tmp/earth/ssr/chk.sh, chk2.sh), all RC 0:
  - D3D9Client.fx PBR_PS, AdvancedPS, FAST_PS, MetalnessPS, BaseTilePS/VS, PBR_VS, XRHUD_PS and PbrMesh.fx (base, clearcoat, aniso+transmission, all five extensions) with `_SSR`, with `_SSR _CSM`, and with `_SSR _LIGHTGLOW _GLASS _ENVMAP`;
  - NewPlanet TerrainPS with `_SSR` + `_WATER _RIPPLES` (legacy), + `_WATERFX _VOLSHD`, and + `_WATERFX _FILMIC _CSM`.
- SSR off vs HEAD 35e2949c, SPIR-V byte compare: SAME for all eight D3D9Client.fx entries, PbrMeshPS (base and all extensions), and TerrainPS (`_WATER _RIPPLES _WATERFX _VOLSHD _SHDMAP _CLOUDSHD` and `_WATER _RIPPLES _SHDMAP`). With SSR = 0 there is no SceneSsr, no pass, and no macro.
- Block layout (INT-2) from the glslang reflection with `_SSR _CSM`: gSsr directly follows gCsm in PlanetPSBlock (gSsr.vHiZ at 876, so mPrev at 748, which is the end of gCsm.vF at 732 + 16) and in FxParams (vHiZ at 2184, mPrev at 2056, gCsm.vF ends at 2056). The C++ writes gSsr by name (224 bytes, static_assert).
- Live: the user's own Custom-tree session (16:57 to after 17:10, DISPLAY :0, 1912x1004, TAA 1, MSAA 4, with SceneShadows, SSGI, VolPlumes and NightLook all 1) loaded the b4 build, which still had the timing hooks.
  - SceneSsr was created 6 times: "1912x1004, 9 depth levels from 2048x1024, 11 colour levels".
  - Capture GPU time is 0.224 ms median (min 0.217, max 0.348) over 202 means of 300 frames, against the design's 0.25 ms target.
  - Its client log has no SSR, "shared value missing" or shader [ERROR] lines. The only [ERROR] lines are the existing DrawOrbits "UnDeleted Surfaces" and "clbkReleaseSurface ... off-line" lines at session close, which are not SSR.
- Earlier (previous owner, 16:22, older build): ssrW1/ssrW0 ("Gfx" sea view from a ground camera 4 km away), no shader [ERROR] lines. On open sea seen from far away, SSR on and off look almost the same, as expected: the sky lookup gives about what GetAmbient gave.
- Shot runs: ssrSea1 ("Tests/SSR DG Sea", SSR 1), ssrKsc1 ("Tests/SSR KSC DGs", SSR 1 + HeightFog 1, to exercise PSUnfog) and ssrSea0 (SSR 0), each with shots at 10/20/28 s and the other features at 0.
  - Queued at 16:58, but blocked from 16:57 to 17:14 by the user's own Custom-tree Orbiter (pid 935629 on DISPLAY :0, then a second one, pid 973988). cfgrun.sh waits for any Custom-tree Orbiter. I did not touch his process.
  - The device then rebooted at about 17:17. /tmp/earth (build.sh, run.sh, cfgrun, Xvfb, the queue, shots, logs and my helper scripts under /tmp/earth/ssr) is gone. The repo and the build tree are intact.
  - The shot checks have therefore NOT run. I will run them as soon as the integrator restores the scripts, and add the results under "Review fixes".

## Open items
- Shot checks pending (see Tests): the three runs above, with the on/off difference over the water and no shader [ERROR] in the client logs. They need the run infrastructure that the 17:17 reboot erased.
- Cost: only the capture is measured (0.22 ms at 1912x1004). The per-shader trace cost (planet loop, vessels, glass, against the 0.6 / 0.35 / 0.2 ms targets) needs an SSR on/off timestamp pair, which the queue did not allow. The timing hooks are removed.
- Accepted per Rev 2: one frame of lag for surfaces drawn before the capture (R1); a single depth layer, with back-face hits kept (R2); plumes and exhausts are not reflected (O2); the reflector-to-hit fog is not added (S5).
- Requests: none. The Surfmgr2.cpp PlanetBind line and the Scene hooks are in place.


## Review fixes (code-review-ue5-ssr.md: MUST 0, SHOULD 2, NIT 5)
- SHOULD 1 (PSUnfog did not invert the fog composite), fixed:
  - HiZ.glsl PSUnfog now rebuilds (f, a) with PSFogComposite's own weights: the same four half-res tFH texels, max(fx·fy, 1e-3)·DepthW(d, depth at FullUV(q)) on the full-res tDepth, then the v/ws normalisation. DepthW is written again here (UnfogW); nothing is included from the fog owner's file.
  - Where the composite drew nothing (a < 1e-3), the colour passes through unchanged.
  - The gain is limited: from a = 0.75 to 0.95 the output blends toward the fogged c', so there is no 20× gain on the TAA residual.
  - SceneSsr::Capture binds tDepth to PSUnfog (point), sets tFog to point sampling, and passes vSrc.zw = 1/fog size.
  - Accepted and documented: with fogFirst (camera above cloud base + 500 m), cloud pixels lose fog they never had. The fix needs an accessor in the fog owner's SceneFxFog.cpp, which I do not own; requested below.
- SHOULD 2 (early-outs, cost), fixed in the shaders; the measurement is still open:
  - Ssr.glsl adds SsrFade(a) (validity × roughness fade), now shared by SsrTrace.
  - SsrBase returns when EnvBRDF·SsrFade(α) < 0.03. Dry ground (α 0.30, fade 0.316) now traces at grazing views only: F·fade is 0.038 at N·V 0.05 and 0.015 at N·V 0.5. Two new SsrMath.Test checks cover both.
  - Water tests 0.75·fSsrF·SsrFade(α_ssr) ≥ 0.01.
  - Not done: the SSR on/off timestamp pair and the three shot runs. They need /tmp/earth/vfx/build.sh, run.sh and cs/cfgrun.sh, which the 17:17 reboot erased and which were not restored when this section was written.
- NIT 1, fixed: the haze strip now uses the same fOrbShd factor as the haze line, (1 − (1 − fShd)·CamSpace·0.5), computed in the SSR block, so the upstream line is unchanged.
- NIT 2, fixed: Metalness.fx traces at the cE line, under `_SSR && _ENVMAP`, only when gEnvMapEnable, the draw is a vessel (kind 0), and max(cF·mix(frP, 1, fMetal)) ≥ 0.01. The old block after SampleEnvMap is removed, and cEnv is used only on that line.
- NIT 3, fixed: SsrDir gets normalize(wnrmW).
- NIT 4, fixed: the header comments of Ssr.glsl, SsrMath.cpp, HiZ.glsl, SceneSsr.cpp and SsrPrm.glsl are one line each.
- NIT 5, left as is (harmless, per the review): on the first frame only, one EndRendering when the textures are first sampled. A PrepareSample in the ctor would need a recording command buffer at Scene construction.
- Checks after the fixes (device, offline; build.sh was missing):
  - glslang RC 0 for HiZ.glsl PSHiZ0, PSHiZDown and PSUnfog; for a stand-in host that compiles every Ssr.glsl function (SsrBase, SsrTrace, SsrDir, SsrMeshDecode with and without `_LIGHTGLOW`); and for TerrainPS with `_SSR` on three flag sets (legacy ripples; `_WATERFX _VOLSHD`; `_WATERFX _FILMIC _CSM`).
  - Metalness.fx (an .fx effect) could not be compiled offline, because my fxc.py was lost in the reboot. The edit uses only names already in scope there: cF, frP, fMetal, fRgh3, nrmW, camW, frg, gEnvMapEnable.
  - SceneSsr.cpp and SsrMath.cpp: syntax-only compile with the flags in build.ninja, RC 0, no warnings in SSR files.
  - SsrMath.Test: 9 cases, 486422 assertions, all pass.
  - SSR off is unchanged by construction: every edit is inside `_SSR` (Metalness: `_SSR && _ENVMAP`), and the IPI and C++ paths need SceneSsr.
- Still open: a full build through build.sh (BUILD EXIT=0), the three shot runs, and the on/off timing. All of them wait on the integrator restoring /tmp/earth/vfx and /tmp/earth/cs.
- Request (fog owner or integrator): `bool SceneFx::FogBeforeClouds() const` (fogFirst this frame), so Capture can skip the strip over cloud pixels in that case.
- At 17:39, /tmp/earth/vfx had only build.lock: build.sh, run.sh and cs/cfgrun.sh were not restored. I did not run cmake or ninja myself. Once the scripts are back, the next steps are: build.sh until BUILD EXIT=0, then the three runs ssrSea1, ssrKsc1 (with HeightFog 1) and ssrSea0, then the client logs checked for shader [ERROR] lines.

