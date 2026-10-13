# UE5 batch: volumetric plumes, smoke and steam, re-entry plasma (plumes owner)

Branch ue5-batch (HEAD 35e2949c, uncommitted). Switch: `VolPlumes` (default 1). The design is design-volumetric-plumes.md; its Rev 2 section overrides the body.

## Files
### Own
- **OVP/VulkanClient/PostPlume.h/.cpp** (stub filled, ~1290 lines). One instance (`PostPlume::inst`), made by Scene when VolPlumes = 1.
  - ctor: half-res targets tVolP/tVolP2 (RGBA16F), tVolPD (RG32F: plume distance, half-res scene distance), tPuff[2] (RGBA16F), tAtlas 512x64, tPuffData x3 (RGBA32F, 6 texels per puff), flow targets (2 vessels: raw front/back R32F 128², prep RGBA32F, blur RG16F 64², reduce 8² and 1²) and a D32 DS; IPI on Plume.glsl (7 entries) and ShaderClass PuffVS/PuffPS; macros `_LIGHTGLOW` (offscreen target) and `_FILMIC`. A failure logs one LogErr and leaves the legacy path. Log: `PostPlume: march 1, puffs 1, flow 1`.
  - NoiseReady: 32³ noise from VolCloud::ShapeNoise on a GfxPool NOISE job; no claims until it is uploaded.
  - CfgOf: `Config/GC/Plume/<class>.cfg` (EXHAUST sel class [pe=], SMOKE, GROUND, PUFFS; later lines win; groups matched by thruster group membership).
  - NozzleOf: exit radius from Orbiter's pressure loss (A_e = F0·φ/p_SL), exit pressure, class (Isp heuristic then cfg); cached by thruster, F0, Isp0, wsize (N8).
  - Collect (before AfterOpaque): frustum, GatherPlumes (LOD 1-3 px crossfade, Cluster M4, 32 budget, JetState, flicker from modulate, ground plane, light/haze from vObject::GetSunLight, fog via SceneFx::FogAlongRay, capsule bounds), UpdateGround (ground clouds in sim time: 6/s·sqrt(F0·lvl/1 MN), 384 per vessel, wind drag, buoyancy, 30 s life), GatherPlasma (re-entry stream owners, Φ = qV intensity, rigid stacks, flow frame, Mach angle, CEA colour table or the non-air colour, ellipsoid fallback, legacy billboard off M2), GatherPuffs (claimed diffuse streams S5, column light every 32 puffs or 1 km S7, deep opacity map, plume light, budget S6, back/front layer), uniforms, screen rect, claims.
  - Render (after PostLook::Haze): atlas bakes, flow maps (own FLOWMAP pass through vVessel::Render), puffs (2 layers, premultiplied), march + 3x3 depth-aware blur, bilateral composite onto the back buffer (ONE, ONE_MINUS_SRC_ALPHA). No tDepth → Revoke (legacy draws).
  - Static hooks used by shared files: LegacyWeight (exhaust crossfade), LegacyReentry, FlowVP, HazeShape.
- **OVP/VulkanClient/PlumeMath.h/.cpp**: isentropic relations, Prandtl-Meyer, ExitArea/Pressure, JetState (S3), PlumeRadius, Simons, Classify, Blackbody (Wyman fit), PlasmaColour/Intensity, ChordIntegral, HG, BandIntegral, DeepOpacity, FlowViewProj, Cluster (M4), CapsuleVisible, FrustumPlanes.
- **OVP/VulkanClient/shaders/Plume.glsl** (new, compiled only when VolPlumes = 1): block PlumePS binding 1, samplers 4-22; PSPlumeMarch (8 nearest capsule intervals, ≤ 48 steps, IGN start with no frame term, Hillaire step, plume field: jet/vacuum blend, diamonds, Mach disk, nozzle glow, turbulence, soot/scatter, wall jet, smoke tail; plasma: shock-layer band from the flow maps, halo, base wake; fog and volume-cloud occlusion), PSPlumeBlur, PSPlumeComposite (Pf + (1−αf)(V + (1−αv)Pb)), PSAtlasBake, PSFlowPrep/Reduce/Blur, PuffVS/PuffPS (spherical billboards, exact parabolic chord, HG sun, ambient, plume light, haze, fog, clouds).
- **Tests/PlumeMath.Test.cpp**: 16 cases, 7008 assertions (design 17 list, N2, N3, S3).
- **Config/GC/Plume/**: Atlantis.cfg (main hydrolox, OMS/RCS hypergolic, steam, smoke), Atlantis_SRB.cfg (solid), DeltaGlider.cfg (texture, dust).
- **Particle.h/.cpp**: accessors (Diffuse, First, Alpha, Texture, DefaultTexture, Owner, IsReentry), Claim/Unclaim; Render returns early only for a stream claimed for this frame's main pass. ReentryStream::IsReentry() true (M3).
- **PostLook.cpp**: Exhausts() takes the volume plume's radii and length from PostPlume::HazeShape; without a plume the values are the old ones.

### Shared (small edits)
- **VVessel.h/.cpp**: `friend class PostPlume`, HullBox (D9UpdateAABB, N7), FLOWMAP pass (no cockpit, RenderShadowMap with FlowVP), RenderExhaust × LegacyWeight, RenderReentry × LegacyReentry.
- **VObject.h**: GetSunLight().

## Switch off (VolPlumes = 0)
- PostPlume is not created, so `inst` is null: LegacyWeight and LegacyReentry return 1 (exhaust call unchanged, `ints *= 1.0f`), HazeShape leaves PostLook's values, no stream is claimed, no FLOWMAP pass runs, Plume.glsl is never compiled. HullBox is computed but unused.

## Finish-phase changes (this owner, 16:56-17:15)
- The previous owner's last Xvfb run (plmlook1, Look DG Engines) showed no flame at all and no Render log: GatherPlumes culled the exhausts against the frustum planes of Camera.mProjView, whose near/far at Collect time are the last pass's limits, not the scene's. Collect now keeps the four side planes and replaces near/far by "in front of the eye" (w > 0) and an always-true plane.
- The TEMP per-frame "PostPlume DBG" log is gone. Replaced by bounded logs: the first 3 frames with lit exhausts (`PostPlume: frame N: lit, plumes, plasma, puffs`, per exhaust up to 8 lines with class, r_e, px, distance), and the first 3 draws (`PostPlume: drawn: ... rect ... march ...`), or `no tDepth this frame, legacy drawing`.
- PLUME_TIMING is 0 (it was 1: per-frame EndRendering + timestamp queries and a log every 300 frames). Set it to 1 only to measure.
- Build b7 (17:14): BUILD EXIT=0, 0 warnings in my files.

## Tests
- PlumeMath (standalone compile with the build's Catch2, /tmp/earth/plm/mtest.sh): **All tests passed (7008 assertions in 16 test cases)**.
- Shader: Plume.glsl compiles all 9 entries at run time with no [ERROR] (plmlook1 log, and the user's own session).
- Live numbers from the user's own Custom-tree session (16:57-17:14, real desktop GPU, the 16:57 build with timing on), scenario Look DG Engines Side: `2 plumes 0 plasma 0 puffs, march 1, I0 0.598, L0 9.12 m, r_e 0.80 m, class texture`; GPU of Render 0.062-0.096 ms (300-frame means); Collect 0.006 ms (600-frame mean). The only [ERROR] lines in that log are the shutdown "UnDeleted Surfaces" (Orbits.dds, DrawOrbits sample), present in every owner's runs (baseline).
- Queued runs (run.sh, 3 in all): plmA "Tests/Plume DG Hover" VolPlumes on (shots 6, 12, 22 s), plmB "Tests/Plume Atlantis Pad" VolPlumes=1 (8, 16, 26 s), plmC "Tests/Plume DG Hover" VolPlumes=0 (6, 12, 22 s). The run queue was blocked from 16:57 to 17:14 by the user's own Custom-tree Orbiter on :0 (run.sh waits for any Custom-tree Orbiter), and he started it again at 17:14, so the queue is blocked again. Results go into "## Review fixes
Review: code-review-ue5-plumes.md (MUST 1, SHOULD 4, NIT 11). All MUST and SHOULD fixed, plus NIT 1-5 and 8.
- **MUST 1 (puffs invisible without a plume):** PSPlumeComposite takes each tap's half-res scene distance from tDepth itself (`SceneDist(q / vTex.zw)`, the 2×2 nearest, 0 = far) instead of tVolPD.g, which stays 0 when the march is skipped. Contrails after burnout, the pad cloud after cut-off and PUFFS-1 smoke without a lit engine now composite. tVolPD.g is still used by the blur.
- **SHOULD 1 (tail self-shadow):** the sun path is the chord inside the tail cylinder, `(max(−b+√D,0) − max(−b−√D,0))/spl`, 0 when the sun ray misses it; no more tsun > 1 rim.
- **SHOULD 2 (pop at w_atm 0.5):** `wj = smoothstep(0.3, 0.7, w_atm)`; norm = mix(1/(2r_e), 1/(1.8R), wj); the texture class fetches both atlas mappings (jet uv and vacuum uv) and mixes them by wj (one fetch when wj is 0 or 1); its extinction uses the same norm.
- **SHOULD 3:** PuffPS skips LitCurve under _LIGHTGLOW (LightBlur tone-maps), as the march and the meshes.
- **SHOULD 4:** GatherPuffs tests each puff sphere against the frustum (side planes + w > 0) before the inside cap, the fill budget and the 8192 cap; off-screen puffs take no budget.
- **NIT 1:** the band stays normalised to unit layer thickness [V]; the unused λ (PzState.lam) is removed.
- **NIT 2:** the end-on-hull tolerance adds one full-res pixel at that distance, `dsc·2·vCamY.w·vTex.y`.
- **NIT 3:** Hash credited: lowbias32 (Wellons, hash-prospector, public domain).
- **NIT 4:** the PLUME_TIMING block is removed (statics, query pool, EndRendering splits).
- **NIT 5:** Revoke clears reentryW; Collect drops a bake queue left from a frame without Render (slot handle reset).
- **NIT 8:** every 300 frames Collect drops nozzle-cache and groundAcc entries whose handle is no longer a vessel.
- Not taken: NIT 6 (mip-20 mean, accepted), 7 (extreme-input cost), 9 (second flow map every frame: about 4 hull draws), 10 (curve after haze, thick haze only), 11 (integrator's SceneFx).

### Checks after the fixes
- Plume.glsl offline: all 9 entries × {none, _LIGHTGLOW, _FILMIC} compile with glslangValidator (Vulkan 1.3, the VkCompileGLSL preamble): 27/27 clean (/tmp/earth/plm/glsl.sh).
- C++: PostPlume.cpp.o compiled 17:23:29 with no warning (b8). /tmp/earth/vfx/build.sh was lost in the reboot, so builds go through /tmp/earth/plm/build.sh (same lock file /tmp/earth/vfx/build.lock, `cmake --build -- -k 0`). The link was blocked by SceneFxFog.cpp:486 (fog owner mid-edit).
- Link: VulkanClient.so relinked 17:26:43 with my PostPlume.cpp.o (17:23:29) after the fog owner's fix; my build b11 (17:27) is up to date, BUILD EXIT=0. Build-tree Modules/VulkanClient/Plume.glsl is identical to the source.
- PlumeMath tests rebuilt and run again (17:27): All tests passed (7008 assertions in 16 test cases).
- Live check of the culling fix (b7 build, in the user's own Custom-tree session after the reboot, DG GL-NT main engines at 100 % in orbit): `exhaust 0/1 of GL-NT: level 1.00, texture, r_e 0.80 m, 17.4/17.6 px, 49 m away`, `2 lit exhausts, 2 plumes`, `drawn: 2 plumes ... march 1, I0 0.95, L0 30.0 m` (vacuum length 2.5 × lsize), Collect 0.008 ms mean over 600 frames, no [ERROR] line from Plume.glsl or PostPlume. Before the fix the same path never reached Render in plmlook1.
- Not done: the three run.sh checks (DG hover on, Atlantis pad on, DG hover VolPlumes=0) and looking at their shots. /tmp/earth/vfx/run.sh, cfgrun.sh and the Xvfb :58 script were lost in the 17:17 reboot and were not restored by 17:28, and the user's own Custom-tree Orbiter (pid 13450, from 17:20) is running, which the old run script waited for. The review fixes are therefore verified by compile (C++ and all 27 shader variants), unit tests and review only; the switch-off path is unchanged by them (all fixes are inside PostPlume and Plume.glsl, which do not exist with VolPlumes = 0).
- Final build check b12 (17:35): BUILD EXIT=0 (tree up to date; VulkanClient.so relinked 17:30:53 with my objects). At 17:37 /tmp/earth/vfx/ still had no run.sh and the user's Orbiter (pid 13450) was still running, so no run.sh check was possible in this phase.
- Left for the integrator: restore /tmp/earth/vfx/run.sh (+ cfgrun.sh, Xvfb :58), then run `"Tests/Plume DG Hover" 26 plmA "6 12 22"`, `"Tests/Plume Atlantis Pad" 28 plmB "8 16 26" VolPlumes=1`, `"Tests/Plume DG Hover" 26 plmC "6 12 22" VolPlumes=0`: look for a visible DG flame spreading on the ground, the SSMEs, SRBs, white smoke and the pad cloud, no [ERROR] from Plume.glsl, and legacy flames with VolPlumes=0.
