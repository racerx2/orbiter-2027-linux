# Code review: screen-space reflections (UE5 batch, owner "ssr")

Repo Orbiter-linux-custom, branch ue5-batch: `git diff 35e2949c` of the SSR regions, plus the new files SceneSsr.h/.cpp, SsrMath.h/.cpp, shaders/Ssr.glsl, SsrPrm.glsl, HiZ.glsl and Tests/SsrMath.Test.cpp. This is the final state after the owner's DONE at 17:19: the timing hooks are gone, and the SSR regions of VBase.cpp and Mesh.fx are unchanged by the 17:06/17:10 edits of other owners. Paths are under OVP/VulkanClient/ unless shown otherwise. Checked against design-ssr.md Rev 2, design-review-ssr.md and ue5-integration.md. Read-only review: nothing was built or run.

**Verified.**
- **Ray march (Ssr.glsl:68-150, mirrored by SsrMath::Walk).**
  - Setup:
    - row vector times the row_major `mPrev`, which is the D3D layout of SsrMath::Row;
    - the near clip of c1 only when d.w < 0, so there is no divide by zero;
    - X and s = 1/w are affine in t;
    - the screen clip gives [tA, tB].
  - Hi-Z (M2): L = min(9, ⌊log2 max⌋ + 1), and level 0 is padded to a multiple of 2^(L−1):
    - 2048×1280 at 1080p, and 2048×1024 at the user's 1912×1004;
    - PSHiZ0 writes 0 outside W×H, so every PSHiZDown is an exact 2×2 max;
    - the cell index is clamped to (size0 >> lvl) − 1 (Ssr.glsl:108).
  - Walk:
    - the cell skip `min(sA, sB) > Sc` is right for a max-of-1/z chain;
    - toward the camera (M3), t is advanced only when ds < 0 (Ssr.glsl:115), and otherwise the walk only steps a level down;
    - the level-0 thickness test (nearest z of the segment in the pixel ≤ z_s + τ0 + τrel·z_s) is the same in both directions;
    - zs = 1/Sc is reached only with Sc ≥ min(sA, sB) > 0;
    - every branch advances t or lowers the level, and the walk stops at 48 iterations.
  - Sky lookup (S3): only when `full` (tB ≥ 1 and not near-clipped) and t reached tB, through (R, 0)·mSky with w > 0, and only where level 0 is empty.
  - Jitter: depth lookups use X + vJit and colour lookups use X. This matches `_31 += 2j/W, _32 −= 2j/H` and the unjittered TAA output. Glass ("current" mode) gets this frame's jitter, because tDepth was drawn jittered.
  - SsrMath.Test passes 486420 assertions. These cover walk against dense march (including more than 100 toward-camera rays), odd-size Hi-Z, near clip, body matrices within 0.01 px, VNDF CDF and the curve pairs.
- **Fresnel and decode.**
  - The hosts keep their own Fresnel:
    - water fFS.y (`_WATERFX`) or fF.y·fSrf (legacy, 0 in space), × (1 − fFoam), and the 0.75 kept;
    - PbrMesh split sum unchanged;
    - PBR_PS/AdvancedPS/Metalness weight the replaced env radiance afterwards (SampleEnvMap returns raw radiance).
  - Base surfaces (M4): EnvBRDFApprox(0.04, √0.30, N·V), lit = mix(lit, L, F·conf) with no Lsky, F < 0.03 out.
  - The decode chains invert each host's own output:
    - PBR.fx:422-424 and Vessel.fx:280-282 (×e, ×T, +I);
    - Metalness.fx:402-406 (×e, curve, T/I);
    - PbrMesh.fx:595-600 (sRGB, ×e, curve, T/I), which is Decode(SsrMeshDecode(y, SSR_CURVE));
    - Mesh.fx:237 ((c·atten + insca)·e), which is (y/e_src − insca)/atten;
    - planet HDR/Filmic. The filmic root is the non-negative one: a < 0 and c ≥ 0 for y ≤ 0.985.
  - Every divide is guarded (max(T, 1e-3), clamp 0.99 before the quartic root, 0.985 clamp, atn ≥ 1e-3). No NaN path found.
- **Water (NewPlanet.glsl:757-771, 1003-1011).**
  - P = −camW = vPosW and V = vRay.
  - The trace sits in the `_RIPPLES` block and is lifted along vPlN.
  - The haze strip (S1) uses the same I_w terms as line 1031, applied after `sct`. Sky lookups are not stripped.
  - PlanetBind (SceneSsr.cpp:270-289) runs every Render call and every pass, with zero (valid 0) outside the proxy, the main pass and 30 km. It requires that last frame's proxy terrain was in the depth (NIT 5). The planetarium sets the sky weight to 0.
- **Off path (SSR = 0).**
  - No macro (VPlanetAtmo.cpp:168, D3D9Effect.cpp BaseMacros), no SceneSsr, no buffers, no pass.
  - Scope and PlanetBind are no-ops. PbrRender shares nothing.
  - The owner's byte compare against HEAD gives SAME for the eight D3D9Client.fx entries, the PbrMeshPS variants and two TerrainPS flag sets.
  - The VkEffect parser numbers sampler_states regardless of `#if` (VkShader.cpp:551-603), and the SSR ones are textually last, so no existing binding moves.
- **UBO and samplers (INT-2/3).**
  - `gSsr` is the last member after `gCsm` in PlanetPSBlock (binding 2), FxParams and PbrShared, with no new binding. The owner's reflection shows mPrev right after gCsm, and the struct is 224 bytes (static_assert).
  - Samplers:
    - D3D9Client.fx: CsmS 28, tSsrCol 29, tSsrHiZ 30 (counted);
    - PbrMesh.fx: 29, 30, 31;
    - NewPlanet: 26/27 only without `_DEVTOOLS`, with the VPlanetAtmo gate plus `#error`. tOverlay/tMskOverlay alias those bindings but are only read under `_DEVTOOLS`.
- **Capture point and fog.**
  - Capture runs after PostTaa::Resolve and before jitter-off, the late labels, the haze, the plumes and the deferred glass (Scene.cpp:1967-1977, 2031).
  - Output() is null unless Resolve ran this frame, and a jittered frame without it is skipped.
  - The fog is in the TAA output and stripped once at capture (S5). Its weaknesses are in SHOULD 1.
- **Resources, state, lifetime.**
  - Every VkTex, VkSurf and the IPI are freed in the dtor. srcSurf holds at most the two fixed PostTaa histories.
  - tHiZ/tCol layouts: VkTex tracks the whole image, and IPI binds PrepareSample. Capture ends with PrepareSample of both, so no layout change lands in the main pass.
  - IPI viewport = output size (IProcess.cpp:194-204), so PSHiZ0 covers the padding.
  - Target, viewport and state are restored with GetState/Push/Pop/SetState, as PostTaa does.
  - Writing sCol through PSUnfog marks the mips dirty (VkCore.cpp:1123), and StretchRect into a single-sample target does too (1669).
- **Receivers and scopes.**
  - Vessel Scope: VVessel.cpp:723, main pass exterior only.
  - The deferred glass re-enters vVessel::Render (Scene.cpp:2745) and gets "current" mode.
  - Base kinds 1/2 use the planet as body.
  - PbrRender copies gSsr and both textures at each BeginVariant (once per mesh Render, Mesh.cpp:2794-2800).
  - Kind gates: env traces kind 0 only; SsrBase kind 1 only. Legacy kind-2 buildings are unchanged.
- **Rules.** `// not upstream` on every shared edit. The maths are cited and written from the papers. SsrVndf follows the listing printed in Heitz 2018 and SsrEnvBRDF uses the published Karis 2014 fit constants. Both are the cited publications, not code repositories.

## MUST
None.

## SHOULD

1. **HiZ.glsl:47-52, SceneSsr.cpp:203-214: PSUnfog does not invert what PSFogComposite blended (a real defect with HeightFog = 1).**
   - **Problem:**
     - PSFogComposite (HeightFog.glsl:404-423) blends a depth-weighted 2×2 upsample of the half-res tFH (DepthW on tDepthF). PSUnfog subtracts a plain bilinear tap of the same tFH, then divides by max(1 − a, 0.05).
     - At every silhouette in front of fog, the bilinear (f, a) mixes far-fog texels into near pixels and near texels into far ones. Example: a hangar edge against a fog bank, with c = 0.3, true a_c = 0.1 and bilinear a_b = 0.5, where f_b − f_c = 0.35:
       - the near side becomes max(0.9·0.3 − 0.35, 0)/0.5 = 0, which is black;
       - the far side becomes (0.1·c + 0.35)/0.5 ≈ 0.76, which is bright.
     - These 1-2 px dark and bright fringes are mirrored sharply by calm water and glass. Those traces are deterministic, so TAA does not average them out.
     - In dense fog (a → 0.95), any remaining mismatch is amplified up to 20×: the TAA-filtered c' against this frame's raw half-res fog.
     - With fogFirst (camera above cloud base + 500 m, SceneFxFog.cpp:509-512), the clouds lie over the fog. Cloud pixels then lose an f they never had.
   - **Fix:**
     - In PSUnfog, rebuild (f, a) with the composite's own weights:
       - bind tDepth (full res, already at hand in Capture);
       - take the same four tFH texels with weight max(f.x·f.y, 1e-3)·DepthW(d, depth at the half-res texel), then the v.rgb, v.a normalisation;
       - write this code in HiZ.glsl, not as an include of the fog owner's file.
     - Limit the gain: where a > about 0.75, lerp toward the fogged c' instead of dividing by as little as 0.05.
     - Skip the strip when fogFirst and the clouds drew, or document that case as accepted.

2. **NewPlanet.glsl:769 and Ssr.glsl:181-184: the trace cost is unmeasured, and two early-outs barely cut anything (efficiency and a verification gap).**
   - **Problem:**
     - Nothing has been looked at on the final build. The three queued runs (ssrSea1, ssrKsc1 with HeightFog, ssrSea0) were lost in the 17:17 reboot. Only the capture is timed (0.224 ms), not the traces against the §10 targets (0.6 ms full-screen sea, 0.35 ms glTF, 0.2 ms cockpit runway).
     - Water: `0.75·fSsrF ≥ 0.01` is always true under `_WATERFX`, since fFS.y ≥ 0.02·wSpec. Every water pixel runs up to 48 iterations, even looking straight down.
     - Base surfaces:
       - F = EnvBRDFApprox(0.04, √0.3, N·V) stays at or above 0.03 up to about N·V = 0.75, so nearly every runway and apron pixel traces;
       - the roughness fade at α = 0.30 caps conf at 0.32 (Ssr.glsl:71), so the largest blend is F·fade ≈ 0.12·0.32 ≈ 0.04;
       - the result is a full trace across the runway for a 4 % sheen.
   - **Fix:**
     - Run the three checks and one SSR on/off timestamp pair (cockpit at KSC, open sea) once the integrator restores /tmp/earth, and record them in the notes.
     - Put the roughness fade into both early-outs:
       - SsrBase: return when `F·(1 − smoothstep(vThick.z, vThick.w, a)) < 0.03`;
       - water: test `0.75·fSsrF·fade` the same way.
     - If the cockpit runway is still over budget, raise the base cut to the visible threshold (about 2/255 of the lit colour).

## NIT

1. **NewPlanet.glsl:1006: the haze strip leaves out fOrbShd.**
   - **Problem:** The strip subtracts I_w, but line 1031 adds I_w·fOrbShd (1027). Below 30 km Const.CamSpace ≈ 0, so fOrbShd ≈ 1 and the difference is negligible.
   - **Fix:** Compute fOrbShd before the mix and use the same product.

2. **Metalness.fx:265-270: no reflectance early-out.**
   - **Problem:** PBR_PS (PBR.fx:320) and AdvancedPS (Vessel.fx:210) skip the trace when the env weight is under 0.01, but Metalness traces every env-mapped pixel with α < 0.36, whatever cF·mix(frP, 1, fMetal) is.
   - **Fix:** Gate on that weight, or move the trace next to line 362 where it is known.

3. **NewPlanet.glsl:714 → 769: the normal is not renormalised at the shore.**
   - **Problem:** `wnrmW = mix(nvrW, wnrmW, fMask)` is not unit length where 0 < fMask < 1, so reflect() in SsrDir is skewed. Upstream's GetAmbient(reflect()) has the same flaw, and SsrKeepAbove catches the downward cases.
   - **Fix:** Pass normalize(wnrmW) to SsrDir.

4. **Comments: header blocks run past one line.**
   - **Problem:** Ssr.glsl:7-9 and SsrMath.cpp:7-9 are three-line header comments, and HiZ.glsl:7-8 and SceneSsr.cpp:6-7 are two-line ones. The project rule is one-line comments.
   - **Fix:** Fold each into one line, or accept them as the header style of the earlier batches.

5. **SceneSsr.cpp:287-288, 309-310: the textures stay bound when the parameters are zero.**
   - **Problem:** After a Scope ends, and in non-main passes, the FX and the Earth shader keep tSsrCol/tSsrHiZ bound. They are never read at valid 0. But on the first frame of a session, the bind of the never-written textures moves them from UNDEFINED through PrepareSample inside the main pass, which costs one EndRendering, once.
   - **Fix:** Harmless; optionally clear the image to 0 in the ctor (or call PrepareSample there) so the first frame's main pass is not split.

Counts: MUST 0, SHOULD 2, NIT 5.

