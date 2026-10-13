# Code review: visual batch, core (6189b7a9), look and water (uncommitted)

Verified: with TAA off, the label split is the old code (every one of the 92 moved lines is in the new functions, in the same order, called at the same places; the only other changed lines in that hunk are the two GBUF conditions). The jitter is set only before UpdateCameraFromOrbiter(MAINSCENE) and is always cleared after AfterOpaque. No Push/PopCamera straddles the clear. GBUF_DEPTH, pLocalCompute and pLocalResults are also made for the flare and shafts while RenderGlares stays on bGlares, and pTaa/pLook are null-initialised and deleted before pFx.
Look: every pass is blendop 0, with the add or screen done in the shader. The scene copies are StretchRect resolves, and writing back into the MSAA target works as it does for LightBlur. Target, viewport and state are restored as in SceneFx. All block uniform names and vec4 packing match (glslang reflection: vSeg offset 112, stride 16, 1536 bytes). The haze clip, its widths and its depth tests are correct. The cloud query runs inside Render under the loader mutex with a frame stamp, and SearchTile takes no lock.
Water: without the macros the shader code is unchanged. The (2u+v, 2v−u) lattice, the (24, 18) scroll and both foam octaves (period 64/256, shifts that are multiples of the period) are seamless across tiles of any level and across the 3600 s Time wrap. The mip and ring maths are right (log2(50/texel), with the texel size taken from texrange), and both Prm fields are written on every tile before SetPSConstants. vVolCld.z is set only in the main pass on the proxy, and cloud tiles overwrite it. dFdx runs at the top level, behind STAGE_PS.

## MUST

1. **PostLook.cpp:293-296: PSFinal binds samplers that are not in every variant, so the log is flooded every frame.**
   - glslang's reflection lists only samplers the entry actually uses. EShReflectionAllBlockVariables covers block members, not samplers. glslangValidator -q with the client's preamble confirms this:
     - with `_FLARE` off, PSFinal has no tDepthL, tVis or tVolL;
     - with `_SHAFTS` off (LightShafts=0, or the shaft IPI failed), it has no tShaft.
   - ImageProcessing::SetTextureNative then logs "Invalid variable name" (IProcess.cpp:617).
   - Final runs every frame while Sharpen or Vignette is on (both on by default). Unticking "Lens flare" therefore gives 3 LogErr per frame, and "Light shafts" gives 1. Each one fflushes and also goes to Orbiter.log.
   - Once iLine passes LOG_MAX_LINES (100000), all client logging stops, real errors included.
   - Fix: bind only what the variant compiled in:
     - `if (pShaft) SetTextureNative("tShaft", …)`;
     - `if (Config->LensFlare) { tDepthL, tVis, tVolL }`.
   - Alternatively, add an `ImageProcessing::Has(name)` check (`pPSConst->GetConstantByName`) and skip the names that are missing.

## SHOULD

1. **shaders/PostLook.glsl:85: the haze offset uses the aspect conversion inverted.**
   - The code multiplies by `vec2(vTex.y / vTex.x, 1.0)`, which is W/H. Turning screen heights into uv.x needs H/W.
   - At 16:9 the horizontal shimmer is (W/H)² = 3.2× too strong: ±10 px against the designed ±3 px at 1080p, which makes it anisotropic.
   - Fix: `vec2(vTex.x / vTex.y, 1.0)` (or `vec2(1.0 / vTex.z, 1.0)`).

2. **shaders/NewPlanet.glsl:646-647: the water's sky reflection pops at the bInSpace switch.**
   - In air the term is × fSrf, and fSrf = 1 − sqrt(CamAlt/visalt) reaches 0 at visalt.
   - bInSpace is !CameraInAtmosphere(), which turns true at the same altitude (Surfmgr2.cpp:1073, VPlanet.cpp:642).
   - The space branch adds 0.6·AmbientApprox·F with no fade. Crossing visalt on ascent or entry, the whole ocean's reflection jumps from 0 to that value, and it is strongest toward the limb where F reaches about 0.8.
   - Fix (continuous, and still the legacy term low down): in air use `cRfl = mix(cUp.rgb * cUp.a * 0.6, GetAmbient(reflect(-vRay, wnrmW)), fSrf) * fFS.y`, with cUp = AmbientApprox(vPlN). Keep the space branch as it is.

## NIT

1. **PostLook.cpp:27-40, 70, 85-88: the C++20 `requires` scaffolding for Depth()/VolumeTex() is no longer needed.**
   - SceneFx has both functions now (SceneFx.h:36-37).
   - If either is renamed, the haze and flare depth tests and the volume test turn off silently instead of failing to build.
   - Fix: call `scn->pFx ? scn->pFx->Depth() : nullptr` directly, and drop the log fields.

2. **VPlanet.h:110 and NewPlanet.glsl:121: the comments still say "x mask mip, yzw spare".** Surfmgr2 now puts the foam ring radius in y and z. Update both comments.

3. **Cloudmgr2.cpp:682 and 706: CloudAlphaToward and CloudSunAlpha have no `// not upstream` marker** in this upstream file. Only CloudDensityAt (664) is marked.

4. **shaders/PostLook.glsl:77-90 and 117-118: implicit-LOD `texture()` in non-uniform control flow.**
   - PSHaze samples tDepthL and tScene after the per-pixel early return, and PSShaftBlur samples inside the per-tap ternary.
   - The results are correct today, because these are single-level targets and IProcess gives them equal min and mag filters.
   - Fix: use `textureLod(…, 0.0)` so the shader is clean by the spec.

5. **PostLook::Final (PostLook.cpp:205-247): in the 2D panel cockpit, the ghosts and shafts come from a sun the panel hides.**
   - Final draws before Render2DOverlay (Scene.cpp:2177 vs 2228).
   - The glare disc is covered by the panel along with the sun, but ghosts and halo on the far side of the centre stay visible.
   - Fix: zero the flare and shafts when `oapiCameraInternal() && oapiCockpitMode() == COCKPIT_PANELS`, or fade them there.

6. **Docs: design §1's switch documentation is not in 6189b7a9 or the working tree.** VulkanClient.html "Effects" list, README, NEW.tex and LAUNCHPAD.tex are all missing it. Add it before the merge.
