# UE5 batch: SSGI (screen-space global illumination), as built

Branch ue5-batch (from 35e2949c), tree /home/racerx/Projects/Orbiter-linux-custom. Design: design-ssgi.md with its "Rev 2 (implementation)" section (that section overrides the body). Config keys SSGI (1) and SSGIStrength (1.0, 0..2) were already in (integrator).

## Files
### Own
- **OVP/VulkanClient/SceneFxGi.cpp** (stubs filled):
  - GiState: the MRT (tAlb, tNrm, W×H A2B10G10R10), tScn (W×H RGBA16F), tSrc[3] (W/2, W/4, W/8 RGBA16F), tGi[2] and tHist[2] (W/2 RGBA16F), the IPI, the previous view, the reset keys, e of the last frame. About 59 MB at 1920×1080.
  - InitGi: targets, SSGI.glsl with PSGiPrep plus four CompileShader; ppf `_PERFORMANCE`, `_FILMIC`, `_LIGHTGLOW` (PostProcess == PP_DEFAULT). A failure frees the IPI and every target and leaves gi NULL (S8; changed in Review fixes, was gi->ok = false only).
  - GiBind(phase, vessels), called three times from DepthPass: 0 = gate (camera < 20 km over the ground or a vessel within 2 km), MRT bound only when GI runs (Review fixes: no longer also for Config->SSR), mesh class 2; 1 = class 3 (vessels); 2 = class −1, `SetRenderTargetN(2, NULL)` then `(1, NULL)`, frame stamp. The MRT never leaves DepthPass (M2).
  - Normal(), Albedo(): public accessors (NULL unless written this frame).
  - Gi(target, vp): reset keys as PostTaa::BeginFrame, previous view as PostTaa::Capture, mRigP/mRigV with PostMath::Rigid, uniforms, then copy → PSGiPrep → two LINEAR halvings → PSGiMarch → PSGiBlur ×2 → PSGiTemporal → PSGiComposite (ONE/ONE into the target). Device state and the target are restored. A frame without GI drops the history.
  - Finish phase: the temporary GPU timestamp code (query pool, GiStamp/GiTimeFrame, the /tmp/earth/gi/time.on switch) is removed, as design §8 asks.
- **OVP/VulkanClient/SsgiMath.h/.cpp**: curves Tw/Tm/Ta and inverses, sRGB, LightFX/LightFXSq, SenderV and CompositeDy per class and family (the same steps as the shader), octahedral 10-bit, slice CDF and mass, BitMask with per-slice dither, SampleBits (front/back with thickness), SliceGamma, radius and pixel radius, ProbeWeights, RayLength/Gauss7/MeshAmbient (GetObjectAtmoParams' sky ambient).
- **Tests/SsgiMath.Test.cpp**: 10 cases, 4764 assertions (see Tests).
- **OVP/VulkanClient/shaders/SSGI.glsl** (new IPI, #includes Scatter.glsl; GiPS at binding 2, samplers 13-22): PSGiPrep, PSGiMarch, PSGiBlur, PSGiTemporal, PSGiComposite.
- **OVP/VulkanClient/shaders/SceneDepth.glsl**: under `_SSGI` DepthPrm gains vTexOff and vAlb (appended, INT-2); the VS passes the world normal and the tile uv; the PS writes albedo = pow(tex, TrGamma)·TrExpo (tex without air, grey 0.5 without a texture) and the octahedral normal, class 0.
- **OVP/VulkanClient/shaders/NewMesh.glsl** (SceneDepth_PS and its block only): under `_SSGI` `PSGi ps_gi` appended to PSConstBlock (binding 1, no new UBO), tMR at binding 5; ρ = (texture, sRGB-decoded when flagged)·factor·(1 − metal), inverted normals flipped, class and family in the alpha channels.
- **OVP/VulkanClient/Mesh.h/.cpp** (RenderShadowMap opt 2 parts): MeshShader::PSGi ps_gi, hPSG, hPST[1] (tMR); `D3D9Mesh::SetGiClass()` with a static class (−1 outside DepthPass, so CSM casters and FlagPass skip the uploads); `bGiTile` set by the base-tile constructor; GlobalInit compiles SceneDepth_PS with `_SSGI` when SSGI. In opt 2 with SSGI: PBR groups bind the base texture always and the MR map, vMat = baseColor, metal = factor·mr.b, decode flag from PbrRender::NeedsDecode, family 2 (IsPbrGroup) else 0; legacy groups vMat = material diffuse, Metalness groups metal = map.g or D3D9MatExt::Metalness, family 1, base tiles family 3; fully lit groups (UsrFlag 0x4) ρ = 0.

### Shared (edit_block, small regions)
- **SceneFx.h**: public `VkTex *Normal() const`, `VkTex *Albedo() const` (for ssr); private `void GiBind(int phase, const std::list<vVessel*> &vessels)`.
- **SceneFx.cpp**: InitDepth compiles pTerrain with `_SSGI` when SSGI; DepthPass gets the three `if (gi) GiBind(n, vessels);` lines (after the push, before the vessels, before the pop). AfterOpaque and FlagPass unchanged.
- **Surfmgr2.cpp RenderDepth**: when the shader has tDiff (compiled with `_SSGI`): the tile texture (IPF_LINEAR|CLAMP), vTexOff from GetTexRangeDX, vAlb (TrGamma, TrExpo) with air, (1, 1) without; the larger Prm is uploaded only then.
- **shaders/Tree.glsl**: under `_SSGI`, two outputs and the distance-pass branch writes the procedural albedo and the world normal (class 1).
- **TreeMgr.cpp:110**: `_SSGI ` in the flags when SSGI.

## What each part does (short)
- Senders (PSGiPrep, half res): the 2×2 texels within 2 % of the reference distance, each converted from display to exposed linear radiance by its class and family (terrain Tw⁻¹ minus the land inscatter, trees Tw⁻¹, Metalness/PbrMesh Tm⁻¹, legacy and base tiles identity; airless terrain Ta⁻¹), clamped (M5) and capped by 1.5·e·ρ·(2·sun + ambient + 0.5·night) (S5).
- March: 2 slices × 2 sides × 8 quadratic taps; GTAO slice through the camera; 32 sectors uniform in the slice's cosine CDF, edges dithered per slice (unbiased; found in the tests: plain rounding missed thin rims); thickness by sender class; sender mip by tap spacing; L = probe weight · facing · falloff · max(L_s − v_amb, 0) with v_amb from NewPlanet's terrain ambient or GetObjectAtmoParams' sky ambient at the receiver (M4).
- Blur: 9 taps per axis, distance and normal weights. Temporal: rigid reprojection (planet or camera target), distance test, α 0.08 / 0.25 / 1, history × e_now/e_prev.
- Composite: depth- and normal-aware 2×2 up-sample, Δv = strength·ρ·E·fade(15-25 km), each receiver's own forward curve: terrain Tw with A, airless LightFX, trees Tw·A + I, Metalness in quadrature (LightFXSq), PbrMesh in linear through sRGB and Tm, legacy/base tile with the shaders' light clamp at 1. Output max(Δ, 0), NaN/Inf-guarded, alpha 0.
- Off: no InitGi, no MRT, the three depth-pass shaders compile without `_SSGI` (SPIR-V byte-identical to 35e2949c, re-checked in the finish phase), Surfmgr2 uploads the old 128-byte Prm, RenderShadowMap takes the old path.

## Tests
- Build: /tmp/earth/gi/b10.log BUILD EXIT=0 (after the timing code was removed).
- Unit: Tests/SsgiMath.Test run directly (no ctest): "All tests passed (4764 assertions in 10 test cases)".
- Offline glslangValidator (/tmp/earth/gi/chk.py, includes inlined as VkCompileGLSL does): the five SSGI.glsl entries compile with no macro, `_FILMIC`, and `_LIGHTGLOW _PERFORMANCE`; SceneDepth VS/PS, NewMesh SceneDepth_PS/NormalDepth_VS, Tree VS/PS compile with and without `_SSGI`.
- Off check (/tmp/earth/gi/off.sh, SPIR-V without `_SSGI` against 35e2949c): SceneDepth TerrainDepthVS/PS, NewMesh SceneDepth_PS/NormalDepth_VS/ShdMapOIT_PS SAME; Tree.glsl TreePS (_TAA) acb0056ac8 and TreeVS 3c24f628b3, the same hashes as 35e2949c (off.sh's own Tree compile of the working tree stops only at shadows' new `#include "SceneShadow.glsl"`, which chk.py inlines).
- GPU cost (temporary timestamps, now removed; KSC Mid, 1920×1080, mean of 300 frames, four windows): Gi 0.48 / 0.56 / 0.54 / 0.53 ms, depth pass with the MRT 0.047-0.070 ms. Within the §8 targets (≤ 1.0 ms, ≤ 0.1 ms).
- Earlier run (previous owner, KSC Mid, giMid1 on vs giMid0 off, t = 24 s): mean luminance 108.46 vs 108.33; 1.53 % of pixels brighter by > 3/255, 0.46 % by > 8; darker by > 3 only 0.24 % (SSAO/TAA run-to-run noise, no systematic darkening); the gain sits on the VAB wall base and the shaded side of the DG tail facing the sunlit apron (cMid.png). No "[ERROR]" shader line; the shutdown "UnDeleted Surfaces" list is the usual one.
- Finish runs NOT done: giF1 (SSGI=1), giF0 (SSGI=0), giF2 (SSGI=1 SSGIStrength=2) on Tests/Gfx SSGI KSC Low (other UE5 effects off, shots 16/22/28 s) were queued at 16:57, held until 17:14 by an Orbiter from the Custom tree on DISPLAY=:0 (PID 935629, the user's own session, not touched), then lost when the device rebooted at about 17:18 (uptime 0, /tmp/earth with build.sh, run.sh, cfgrun.sh, the Xvfb script and every queued run gone). The repo working tree survived (VulkanClient.so of 17:13:59 holds this code). The previous owner's scripts (/tmp/earth/gi/chk.py, off.sh, cmp.py) and the giMid shots are gone too; the numbers above were taken before the reboot.

## Open items
- Left: the three KSC Low runs above (on, off, strength 2), as soon as the integrator restores /tmp/earth/vfx/build.sh and run.sh; then the shots are compared (on − off ≥ 0 on the DG's shaded side and the VAB wall base, sky 0, no "[ERROR]" line).
- No code item open. Design §13's extra scenarios (Canyon, Moon, Hangar, Night) and the stability/motion p99 checks were not run in this phase (3-run limit); the integrator's combined runs cover coexistence with SSR, CSM, fog and plumes.

## Requests for other owners
- ssr: none (ssr does not read Normal()/Albedo(); they stay valid only in frames where GI ran, NULL otherwise).
- integrator: optional, review NIT 1 (SceneFx.cpp:281, your line): `more = depthOK && (gi || …)` keeps the depth pass in frames where GI's gate is off when SSGI is the only depth reader (after a failed InitGi gi is now NULL, so that case is gone).


## Review fixes
Review code-review-ue5-ssgi.md: no MUST, one SHOULD, three NITs.
- SHOULD 1 (fixed, SceneFxGi.cpp GiBind phase 0): `gi->bound = gi->run;`. The MRT, the tile tDiff binds and the ps_gi uploads now happen only in frames where GI runs, not for SSR (which reads nothing from SSGI). The "for ssr" text is gone from the comment and from the notes; Normal()/Albedo() still return NULL unless written this frame.
- NIT 2 (fixed, SceneFxGi.cpp InitGi): on a failure `FreeGi(gi); gi = nullptr;`. The IPI and all ~55 MB of targets are freed; with gi NULL, DepthPass, GiBind and Gi are skipped, and `more` no longer keeps the depth pass for a failed GI.
- NIT 3 (fixed, Mesh.cpp RenderShadowMap opt 2, legacy groups): vMat.w = hTex && !(UsrFlag & 0x10), so "no texture colour" groups take ρ = Diffuse, as the legacy shaders' bNoC.
- NIT 1 (not mine, SceneFx.cpp:281 is the integrator's line): left as a request under "Requests for other owners". After NIT 2's fix the failed-init case is gone. Only the "SSGI is the only depth reader and GI's gate is off" case is left; with the default SSR/VolPlumes on it never shows.
- Re-check: the syntax-only compile of SceneFxGi.cpp and Mesh.cpp with build.ninja's flags is rc 0. Build: /tmp/earth/plm/build.sh is the plumes owner's stand-in for the lost vfx/build.sh and takes the same /tmp/earth/vfx/build.lock. b11 failed only in SceneFxFog.cpp (fog's file, mid-edit). b12 after a minute: BUILD EXIT=0, VulkanClient.so relinked. SsgiMath.Test run directly: all tests passed (4764 assertions in 10 test cases). No caller of Normal()/Albedo() outside SceneFxGi.cpp.
- Still not done: the three KSC Low runs (on, off, strength 2) and a look at the shots. After the reboot there is no run.sh, cfgrun.sh or Xvfb script, so no run is possible through the shared queue. They are the first thing to run when the integrator restores the run scripts: `run.sh "Tests/Gfx SSGI KSC Low" 30 giF1 '16 22 28' SSGI=1 SSR=0 SceneShadows=0 VolPlumes=0 NightLook=0 HeightFog=0`, then the same with SSGI=0 (giF0) and with SSGI=1 SSGIStrength=2 (giF2). Check: no "[ERROR]" shader line; on − off ≥ 0 outside noise on the DG's shaded side and the VAB wall base; sky unchanged.

