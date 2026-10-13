# Design: screen-space reflections (SSR) for water, base surfaces and vessel materials

Repo: Orbiter-linux-custom (/home/racerx/Projects/Orbiter-linux-custom), branch visual-fx, HEAD dd4e087c (clean tree). Paths are under OVP/VulkanClient/ unless shown otherwise. Line numbers are from this tree. Read-only survey: nothing was built or run.

## 0. Rules
- One switch, `SSR` in VulkanClient.cfg, read once at start. Off is the legacy image:
  - no `_SSR` macro anywhere (Earth flags, D3D9Effect::BaseMacros), so the SPIR-V is unchanged;
  - no SceneSsr object, no buffers, no passes;
  - the SceneFx, Surfmgr2 and label gates are as today.
- Own code from published work, cited in one line where used:
  - McGuire & Mara 2014, "Efficient GPU Screen-Space Ray Tracing" (JCGT 3(4)): screen-space ray setup and 1/w interpolation.
  - Uludag 2014, "Hi-Z Screen-Space Cone-Traced Reflections" (GPU Pro 5): Hi-Z traversal and the cone level.
  - Stachowiak 2015, "Stochastic Screen-Space Reflections" (SIGGRAPH Advances): one ray per pixel, averaged over time.
  - Heitz 2018, "Sampling the GGX Distribution of Visible Normals" (JCGT 7(4)).
  - Karis 2013, "Real Shading in UE4": split sum.
  - Schlick 1994: Fresnel.
  - Jimenez 2014, "Next Generation Post Processing in CoD:AW": interleaved gradient noise (IGN).
  - Valient 2014, "Reflections and Volumetrics of Killzone Shadow Fall": last frame's colour.
  - Wronski 2014, "The future of screenspace reflections": edge and thickness fades.
  - Lagarde 2013, "Water drop 2b, dynamic rain and its effects": wet surfaces.
- No GPL, Khronos or vendor sample code. One-line comments. `// not upstream` in upstream files.
- 2D panel, HUD, MFD, GDI and debug text are never touched, and no text is drawn over the sim.

## 1. What it does and why
- **Reflections of the visible scene** (terrain, sea, bases, buildings, trees, vessels, volume clouds, sky, night lights) on:
  - water: the Earth TerrainPS `_WATER`/`_RIPPLES` path, with or without `_WATERFX`;
  - base surfaces: the base tile mesh and the "under shadows" structures (runways, pads, aprons);
  - vessel materials:
    - glTF PBR: EvalIBL specular and clearcoat, including transmission glass;
    - legacy PBR_PS and AdvancedPS, which reflect only with EnvMapMode ≥ 1, as today;
    - MetalnessPS.
- **Fallback.** Where the ray leaves the screen or finds nothing, the existing term is kept, with a soft blend:
  - water: GetAmbient in air, AmbientApprox in orbit;
  - PbrMesh: `Prefiltered()` (GGX cubes, the legacy cube or EarthGlow);
  - legacy: the EnvMapAS cube;
  - bases: a haze-colour sky.
- **What exists after the opaque scene** (the survey):
  - tDepth: R32F camera distance, 0 = none. It holds terrain under 150 km (Surfmgr2.cpp:1300), bases, buildings, trees and vessels (SceneFx.cpp:287-301). It is written only in AfterOpaque, after the main pass has drawn water and vessels.
  - GBUF_DEPTH: vessels and cockpit only, geometric normals, made only when glare, flare or shafts are on (Scene.cpp:287, 1622-1641). Not usable for water or bases.
  - TAA history: PostTaa::Output(). It is the resolved opaque scene, RGBA16F, unjittered, with no labels, VC, exhausts or glass (PostTaa.cpp:64, 131-198).
  - PBR env cubes per vessel (pbrEnv, pEnv[ENVMAP_MAIN], VVessel.cpp:1188-1205). The water's cRfl is sky radiance before atn, insc and HDR (NewPlanet.glsl:649-666, 874, 894).
- **Every host writes display values with its own curve and haze:**
  - planet: HDR() = 1 − exp(−Expo·x), or the filmic curve (Scatter.glsl:206-213);
  - Metalness and PbrMesh: x(1+x⁴)^−¼ unless `_LIGHTGLOW` (Metalness.fx:386-401, PbrMesh.fx:538-542);
  - PBR_PS, AdvancedPS, FAST_PS and BaseTilePS: no curve (PBR.fx:375, 533; Vessel.fx:258; Mesh.fx:225);
  - each also has its own Transmission/Inscatter or atn/insc.

**Architecture options**

| | (a) SceneFx post pass | (b) trace inside the shaders | hybrid (water in shader, vessels in post) |
|---|---|---|---|
| Normal, roughness, F0 | needs MRT on every pipeline drawn into the main target (attachment count changes everywhere), or a second material pass that duplicates the water, PBR and legacy code | exact, in place (wave normal, normal maps, clearcoat, anisotropy) | water exact; vessels still need MRT |
| Composite | must subtract the env term each shader already added, through each shader's curve and haze: 2 more RGBA16F MRTs | the env radiance is replaced before the curve: exact | two systems |
| Glass (drawn after AfterOpaque, not in tDepth) | excluded | included | excluded |
| Lag | none | 1 frame for what draws before the capture | 1 frame for water |

- **Chosen: (b) everywhere.** One trace (Ssr.glsl) is used by every host.
  - It runs against a Hi-Z pyramid of 1/z and a colour pyramid, both captured once per frame right after the TAA resolve.
  - Surfaces drawn before the capture (water, bases, opaque vessels) trace last frame's data through a per-body rigid reprojection.
  - Surfaces drawn after the capture (exterior glass in RenderPbrDeferred(false)) trace this frame's data with no lag.
  - SSR only supplies incoming radiance. Each host keeps its own Fresnel, roughness and curve, so the result matches `_WATERFX` Schlick and the PBR split sum by construction.

## 2. Where it hooks (Scene::RenderMainScene and friends)
- Scene.cpp:347: `if (Config->SSR) pSsr = new SceneSsr(gc, this, viewW, viewH);`. Delete at 381-382.
- Scene.cpp:1595-1596:
  - after `pTaa->BeginFrame`: `if (pSsr) pSsr->BeginFrame();`;
  - `bLabelsLate = TaaActive() || SsrActive();`, so with TAA off the labels and the planetarium art also draw after the capture.
- Scene.cpp:1955-1956, after `pTaa->Resolve(pBackBuffer)` and before the jitter-off line: `if (pSsr) pSsr->Capture(pBackBuffer, RenderMarkers);`.
  - The projection is still jittered here, so CameraRays() matches tDepth.
  - Late labels (1957-1961), markers, exhausts, beacons, particles and the VC all draw later, so none of them are captured.
- Scene.cpp:2016: RenderPbrDeferred(false). Exterior glass uses this frame's capture ("current mode", §5).
- Scene.cpp:2051-2079: VC, internal pass. SSR is off there (§13).
- Scene.cpp:3419: `bool Scene::SsrActive() const { return pSsr && pSsr->Active(); }`, next to TaaActive().
- SceneFx.cpp:37: InitDepth also when `Config->SSR`.
- SceneFx.cpp:273: DepthPass every main frame when `scn->TaaActive() || scn->SsrActive()`.
- Surfmgr2.cpp:1973: depthRec adds `Config->SSR`. Integrator note: TAA is missing from this gate today, so with SSAO = 0 and VolClouds = 0 the terrain is not in tDepth and TAA reprojects it as sky. Add `Config->TAA` too.
- Surfmgr2.cpp:1956: before `vp->InitEclipse(pShader)`, the Earth shader's gSsr and the two textures, every call and every pass (§8).
- VVessel.cpp:716: a scope in vVessel::Render, next to PbrEnvScope, that sets gSsr (FX) for this vessel, restored to "off" on exit.
- VBase.cpp:381 (RenderSurface) and 412 (RenderStructures): scopes with kind 1 (surface gloss) and kind 2.

## 3. Passes and buffers (SceneSsr, at Capture, about 0.25 ms)
- **tSsrCol:** RGBA16F, W×H, full mip chain, `autoGenMips`, RTUsage.
  - Filled by StretchRect from PostTaa::Output() when TAA resolved this frame, else from the target (this resolves MSAA).
  - Then `dev->PrepareSample(tSsrCol)` builds the mips right away (VkCore.cpp:1568-1574), so no EndRendering lands in the next main pass.
- **tHiZ:** R32F, W×H, L = ⌊log2 max(W,H)⌋ + 1 levels, RTUsage, no autoGenMips. Level 0 holds s = 1/z_view (0 = nothing); level k holds the max over its 2×2 children, i.e. the closest.
- **tHiZTmp:** R32F, W/2×H/2 with L−1 levels, scratch.
  - The IPI cannot sample tHiZ while writing it: VkConstBuffer binds the default texture for a texture that is also an attachment (VkShader.cpp:321-326).
  - So level k is drawn into tHiZTmp(k−1) from tHiZ(k−1), then StretchRect NEAREST copies it into tHiZ(k). StretchRect blits between levels (VkCore.cpp:1616-1667).
- **Passes per frame:**
  1. colour copy and mips;
  2. PSHiZ0 (full res, tDepth → tHiZ level 0);
  3. L−1 × (PSHiZDown + copy).
  - The target, viewport and state are restored as SceneFx::AfterOpaque does.
- **VkSurf per level:** made once.
- **Memory at 1080p:** about 22 + 11 + 3 MB.
- **No post pass and no G-buffer:** the traces run in the forward shaders.
- **Validity:**
  - Capture needs `SceneFx::Depth()` this frame. Without it the capture is marked invalid, and the next frame's main pass has SSR off.
  - With TAA on and no Output() this frame, it is also marked invalid (no jittered copy).

## 4. Shaders, macros, bindings
- **`_SSR` macro:**
  - VPlanetAtmo.cpp:165 Earth flags: `if (bWater && bRiples && Config->SSR) flags += "_SSR ";`;
  - D3D9Effect.cpp:326 BaseMacros: `if (Config->SSR) macro.push_back({ "_SSR", "" });`. This reaches D3D9Client.fx and every PbrMesh variant.
- **New shaders:**
  - `shaders/SsrPrm.glsl`: the struct only, needed before the hosts' uniform blocks;
  - `shaders/Ssr.glsl`: the functions, after the blocks and samplers;
  - `shaders/HiZ.glsl`: IPI entries PSHiZ0 and PSHiZDown, cbuffer binding 1 {vCamX, vCamY, vCamZ (CameraRays), vFwd (GetCameraZ), vSrc (source level w, h, level)}, tDepth 4, tHiZ 5.
- **SsrPrm** (scalar, row_major, 224 bytes, mirrored in SceneSsr.h with a static_assert):
  - `mat4 mPrev`: camera-relative point now → source clip, rigid with this body;
  - `mat4 mSky`: direction → source clip (w = 0);
  - `vec4 vHiZ`: W, H, levels used (≤ 8), iterations (40);
  - `vec4 vJit`: source depth jitter in px (xy), start offset in px (1.5), near w (0.1 m);
  - `vec4 vMode`: valid, stochastic, noise phase (frame % 16), sky weight;
  - `vec4 vThick`: τ0 = 0.25 m, τrel = 0.015, α fade from 0.20 to 0.36;
  - `vec4 vDecode`: x = e_now/e_src (planet), y = 1/e_src (meshes), z = colour mips − 1, w = edge fade width 0.06;
  - `vec4 vSurf`: x = kind, y = α, z = F0, w = albedo factor. Kind 0 is a vessel: the legacy env paths and PbrMesh. Kind 1 is a base surface (tile mesh and structure_bs): the gloss term and PbrMesh. Kind 2 is a base building: PbrMesh only. Bases have no env cube, so a legacy Metalness building would otherwise fade to black at the screen edge.
- **NewPlanet.glsl (Earth TerrainPS):**
  - 141-143: under `_SSR`, tMicroA and tMicroB become `tSsrCol` (21) and `tSsrHiZ` (22); tMicroC stays. Earth never has `_MICROTEX` (VPlanetAtmo.cpp:157-175), and `#if defined(_SSR) && defined(_MICROTEX)` → `#error`.
  - After 181: SsrPrm.glsl, `layout(binding = 3, row_major, scalar) uniform SsrBlock { SsrPrm gSsr; };`, Ssr.glsl, HDRInv(). Binding 3 is the free UBO slot (NUBOS 4, VkCore.h:302).
- **D3D9Client.fx:**
  - sampler_states `tSsrCol` (LINEAR, MipFilter LINEAR, CLAMP) and `tSsrHiZ` (POINT, CLAMP) under `#if defined(_SSR)`, after Planet3S (535). VkEffect numbers samplers in order (VkShader.cpp:547-596), so these get 28 and 29 and the existing 4-27 do not move.
  - `#if defined(_SSR) SsrPrm gSsr; #endif` appended at the end of FxParams, after gExposure (153). Existing offsets do not move. The block's reflection reference is its first member, kernel (VkShader.cpp:598-617), so the off build still compiles.
  - SsrPrm.glsl comes before line 99. Ssr.glsl comes before the includes at 675.
  - Not a separate UBO here: the VkEffect parser ignores `#if`, so a block at binding 3 would be referenced by the reflection stub even with SSR off.
- **PbrMesh.fx:**
  - samplers after IridThickS (141), giving 29 and 30;
  - SsrPrm.glsl before PbrShared (82);
  - `SsrPrm gSsr;` at the end of PbrShared, after gSceneCopy (109);
  - Ssr.glsl after Common.glsl (144).
- **Ssr.glsl API:**
  - `SsrHit { vec2 uv; float conf; float lod; }`;
  - `vec3 SsrDir(vec3 N, vec3 V, float a, vec2 frag)`;
  - `SsrHit SsrTrace(vec3 P, vec3 R, float a)`;
  - `vec3 SsrFetch(SsrHit h)` = `textureLod(tSsrCol, h.uv, h.lod).rgb`, display values;
  - `float SsrSchlick(float f0, float c)`;
  - `vec3 SsrMeshDecode(vec3 y, bool curve)` for the .fx hosts: gSun.Transmission/Inscatter, the x(1+x⁴)^−¼ inverse unless `_LIGHTGLOW`, then × vDecode.y. NewPlanet defines `SSR_PLANET` and gets HDRInv instead.
  - Callers sit inside `#ifdef STAGE_PS`. BaseTilePS (Mesh.fx:207) is outside one today, so its call is wrapped.

## 5. The trace (Ssr.glsl)
- **Direction.**
  - `SsrDir`: reflect(−V, N) when vMode.y = 0 or α < 0.05.
  - Otherwise H = VNDF(V, α, u) (Heitz 2018), R = reflect(−V, H), with u = (IGN(frag + 5.588238·phase), IGN(frag + (37, 17) + 7.919·phase)) (Jimenez 2014).
  - R·N ≤ 0 falls back to the mirror direction.
  - Water also bends R to at least 0.01 above the mean plane (vPlN) so grazing waves do not trace into the sea.
- **Setup** (McGuire & Mara 2014).
  - c0 = (P, 1)·mPrev and c1 = (P + R·1e6, 1)·mPrev.
  - c0.w < near: miss. c1.w < near: clip c1 to w = near.
  - Pixels: X = (c.x/c.w·0.5 + 0.5, 0.5 − c.y/c.w·0.5)·(W, H). s = 1/c.w.
  - X and s are both affine in the screen parameter t ∈ [0, 1].
  - The segment is clipped to the screen rectangle, giving [tIn, tOut]. Start at t = max(tIn, 1.5 px/|X1 − X0|).
  - |X1 − X0| < 1 px: only the sky test at X1.
- **Hi-Z walk** (Uludag 2014, with a max-of-1/z chain; s grows toward the camera).
  - Start at level 1. Per iteration:
    - cell = ⌊(X(t) + jit)/2^lvl⌋, Sc = texelFetch(tSsrHiZ, cell, lvl);
    - tx is where the ray leaves the cell (+0.01 px).
  - If min(s(t), s(tx)) > Sc (in front of everything in the cell): t = tx, lvl = min(lvl + 1, levels − 1).
  - Else if lvl > 0: t = clamp((Sc − s0)/(s1 − s0), t, tx) (t is kept when |s1 − s0| is about 0), lvl −= 1.
  - Else (level 0): z_r = 1/s(t), z_s = 1/Sc.
    - z_r − z_s ≤ τ0 + τrel·z_s: hit at X(t).
    - Otherwise the ray passed behind a thin object: t = tx and the walk goes on.
  - Sky has Sc = 0, and a ray always has s > 0, so it never hits sky.
  - At most vHiZ.w = 40 iterations.
- **Sky at infinity.**
  - No hit, the walk reached tOut = 1 with X1 on screen, and tSsrHiZ level 0 at X1 is 0: the pixel at X1 is what lies in direction R.
  - That gives clouds, volume clouds, the horizon, stars, and the Earth from orbit.
  - conf = EdgeFade × vMode.w.
- **Confidence.** conf = EdgeFade(uv) · (1 − smoothstep(0.75N, N, it)) · (1 − smoothstep(0.20, 0.36, α)) · vMode.x.
  - EdgeFade = smoothstep(0, vDecode.w, min(uv, 1 − uv)) on both axes, aspect-corrected (Wronski 2014).
- **Colour level** (Uludag 2014 cone).
  - lod = log2(max(1, d_px·α·k)), d_px the screen length to the hit.
  - k = 1 when deterministic, 0.3 when stochastic (the sampled direction already spreads the lobe; the small blur cuts the noise).
  - Clamped to vDecode.z.
- **Jitter.**
  - Hi-Z lookups add vJit.xy: the source depth was drawn jittered, so a point at unjittered pixel X shows at X + j (Scene.cpp:3518 adds +2j/W to _31 and −2j/H to _32, both +j in pixels).
  - Colour lookups are not offset: the TAA output and the non-TAA target are unjittered.
- **Modes, set by SceneSsr per draw.**
  - "Previous" (main pass before Capture): mPrev = Rigid(body now → last frame) · V_prev · P_prev (unjittered); mSky = V_prev · P_prev; jit = last frame's; e_src = last frame's e; stochastic when TAA runs this frame.
  - "Current" (after Capture: exterior glass): mPrev = mSky = V_now · P_now (unjittered); jit = this frame's; stochastic off (glass is not in TAA).
  - Off: VC, the env, custom-camera, shadow and depth passes, reset frames, the mesh debugger camera or wireframe.

## 6. Hosts: where the radiance goes, Fresnel, decoding
- **Water, TerrainPS.**
  - Where:
    - after 652 (`_WATERFX`): `cRfl = mix(cRfl, HDRInv(SsrFetch(h)) * fFS.y, h.conf);`;
    - after 660 (legacy, in air): `cRfl = mix(cRfl, HDRInv(SsrFetch(h)) * fF.y * fSrf, h.conf);`.
  - Normal and roughness: wnrmW with α = fRgh (`_WATERFX`, already widened by specular AA), or the legacy 0.1 + 0.1·fDWS. P = −frg.camW.xyz, V = vRay.
  - Fresnel: unchanged (Schlick F0 0.02 on V·N in `_WATERFX`; legacy 0.15 + 0.85f³). Foam (685), atn, insc and HDR (874-894) follow as today.
  - HDRInv(y) = −ln(1 − min(y, 0.985))/Const.Expo · vDecode.x.
    - Under `_FILMIC`: the positive root t = (b + √(b² − 4ac))/(−2a) with a = 2.43y − 2.51, b = 0.59y − 0.03, c = 0.14y, giving x = t/(0.568·Const.Expo)·vDecode.x.
    - vDecode.x = e_now/e_src, because Expo = expoBase·e (VPlanetAtmo.cpp ApplyExposure).
  - Early out: no trace when fMask = 0 or 0.75·fFS.y·fMask < 0.01 (steep views of near water).
  - Planet gate: valid only for the camera proxy, in the main pass, with the camera under 30 km.
  - Sky weight vMode.w = 1 − smoothstep(3 km, 12 km, camera altitude). From high up the sky the camera sees is not the sky over the water. It is 0 while the planetarium is on (plnmode & PLN_ENABLE), so grid lines do not mirror.
- **PbrMesh EvalIBL (349).**
  - `vec3 Lenv = Prefiltered(R, s.perceptual);`
  - Under `_SSR`, when Max3(mix(Fd, Fm, metallic))·ao ≥ 0.01: Lenv = mix(Lenv, Decode(MeshDecode(SsrFetch(h))), h.conf), with the trace on SsrDir(s.N or s.Nbent, s.V, s.a).
  - Clearcoat (361): when s.cc > 0.5 and the coat is smoother than the base, the single trace follows the coat ray and the base keeps Prefiltered. One trace per pixel.
  - MeshDecode(y) = ((y − Inscatter)/max(Transmission, 1e-3)), then (no `_LIGHTGLOW`) clamp 0.99 and c/(1 − c⁴)^¼, then × vDecode.y. This is Transmitted()'s conversion (PbrMesh.fx:261-283), with e_src.
  - Fresnel and the split-sum LUT are unchanged. A perfect mirror shows exactly the displayed hit colour (§16 test).
- **Legacy PBR_PS (309-333) and AdvancedPS (Vessel.fx:199-235).**
  - Inside `#if defined(_ENVMAP)` and `if (gEnvMapEnable)`, kind 0 only. One trace on RflW, α = (1 − fRghn)² (fRghn is smoothness here: fLOD = (1 − fRghn)·8).
  - Base term: mix(env(fLOD), L, conf).
  - `_GLASS` and `gFresnel` term: mix(env, textureLod(tSsrCol, h.uv, 0), conf) after decoding.
  - L = ((y − I)/T)·vDecode.y. There is no curve, and the env cube is display values at e = 1 (Scene.cpp:1483).
- **MetalnessPS (263).** After SampleEnvMap, kind 0 only: cEnv = mix(cEnv, L, conf).
  - α = mix(0.01, 1, fRgh³), its own GGX lobe.
  - L = curve⁻¹((y − I)/T)·vDecode.y, or no curve under `_LIGHTGLOW`.
- **Base surfaces (kind 1): BaseTilePS (Mesh.fx:207-226) and FAST_PS (PBR.fx:446-540, lit branch, before the exposure line 533).**
  - albedo *= vSurf.w;
  - F = SsrSchlick(vSurf.z, N·V);
  - lit colour = mix(lit, mix(Lsky, L, conf), F).
  - L:
    - BaseTilePS: (y·vDecode.y − insca)/max(atten, 1e-3);
    - FAST_PS: ((y − I)/T)·vDecode.y.
  - Lsky = insca/max(1 − atten, 0.05) (respectively Inscatter/max(1 − Transmission, 0.05)): the asymptotic haze colour, the sky near the horizon that a grazing reflection sees.
  - Early out: F < 0.03. A dry surface then traces only at grazing angles.
  - Wet model (Lagarde 2013), w = SSRWetness (§9):
    - α = mix(0.30, 0.02, w);
    - F0 = mix(0.04, 0.02, w);
    - albedo × mix(1, 0.55, w).
  - Dry (w = 0) is a soft grazing sheen.
  - PBR_PS gets the same term for base surface meshes with maps. AdvancedPS and Metalness base meshes are unchanged (kind ≠ 0, and bases have no env cube).
  - glTF base meshes (kinds 1 and 2) reflect through their EvalIBL, with the EarthGlow fallback.

## 7. Data needed and where it comes from
| Data | Source |
|---|---|
| Depth of last frame and this frame | SceneFx::Depth() (tDepth R32F distance), turned into tHiZ at Capture |
| 1/z at level 0 | z = d·dot(normalize(PixelRay(uv)), camZ), with rays from SceneFx::CameraRays() (jitter folded in) and camZ from Scene::GetCameraZ() |
| Colour of last frame and this frame | PostTaa::Output() (unjittered, labels-free) or the target, copied into tSsrCol |
| View and projection | Camera.mView, and Camera.mProj with _31/_32 = 0 |
| Camera position | Camera.pos |
| Jitter | Scene jitX/jitY |
| Exposure | Scene::GetExposure() (fExposure) |
| Proxy frame | oapiGetRotationMatrix/GetGlobalPos of Camera.hObj_proxy |
| Each visible vessel's (R, o) | from RenderMarkers at Capture |
| Rigid map | PostMath::Rigid (PostMath.h:33), as PostTaa::Capture (PostTaa.cpp:98-129) |
| Water normal, roughness, Fresnel | TerrainPS locals (wnrmW, fRgh, fFS, fF) |
| PBR inputs | Surf (N, Nbent, Nc, a, perceptual, specW, f0d, metallic), the LUT |
| Legacy inputs | RflW, fRghn or fRgh, cRefl, gSun.Transmission/Inscatter |
| Base inputs | vertex normal, atten/insca or gSun |
| Not used | GBUF_DEPTH (vessels only, sometimes, no material), PostTaa's private matrices (SSR keeps its own) |

## 8. C++ changes per file
- **SceneSsr.h/.cpp (new).**
  - `SceneSsr(gc, scn, W, H)`: tSsrCol, tHiZ, tHiZTmp, the per-level VkSurf, the IPI HiZ.glsl (PSHiZ0, PSHiZDown). A failure logs once, and SSR stays off.
  - `BeginFrame()`: the reset rules.
    - From PostTaa::BeginFrame (PostTaa.cpp:70-96): proxy, focus, camera target or internal/external change; mesh-debugger camera or wireframe; no capture last frame.
    - Plus a jump test: camera displacement in the proxy frame > 1 km + 2× the previous displacement, or a turn > 45°.
    - A reset frame traces nothing before the capture.
    - Active() is still true then, because the capture runs and the labels must stay late. It is false only with the mesh-debugger camera or wireframe, or after an init failure.
  - `Capture(target, vessels)`: §3. It records View {cam, proxy (R, o), VP unjittered, jitter, e} and a map OBJHANDLE → (R, o) of the vessels. It switches the mode to "current".
  - `PlanetParams(vPlanet*, SsrPrm&)`, `VesselParams(vVessel*, bool internal, SsrPrm&)`, `BaseParams(vBase*, int kind, SsrPrm&)`:
    - mPrev = Rigid(body) · VP_prev;
    - a vessel missing from last frame's map uses the proxy's;
    - "current" mode uses VP_now;
    - all as float, built in double.
  - `Active()`, `Color()`, `HiZ()` (also for SSGI).
  - `SceneSsr::Scope` (RAII): `FX->SetValue(eSsr)` plus `SetTexture(eSsrCol/eSsrHiZ)`, restored to a zero SsrPrm (valid 0) on exit.
- **SsrMath.h/.cpp (new, pure, double):**
  - curve pairs: HDRInv, FilmicInv, MeshCurveInv, the sRGB pair as in PbrBRDF.glsl:17-27;
  - MeshDecode/Encode;
  - NearClip, ToScreen;
  - the CPU Hi-Z build (odd sizes) and the CPU Hi-Z walk, a mirror of Ssr.glsl, plus a brute-force linear march;
  - Vndf, ConeLod, EdgeFade, WetModel, JumpTest, BodyMatrix (PostMath::Rigid · VP).
- **Scene.h/.cpp:**
  - `friend class SceneSsr;` (77), `class SceneSsr *pSsr = nullptr;` (529), `SsrActive()` (388), `GetSsr()`;
  - hooks of §2.
- **SceneFx.cpp:** 37, 273 (§2).
- **Surfmgr2.cpp:** 1973 (depthRec). At 1956: `if (pShader->bSsr) { SsrPrm p; if (auto s = scene->GetSsr()) s->PlanetParams(vp, p); pShader->SetPSConstants("gSsr", &p, sizeof p); pShader->SetTexture("tSsrCol", …, IPF_LINEAR | IPF_CLAMP); pShader->SetTexture("tSsrHiZ", …, IPF_POINT | IPF_CLAMP); }`.
  - This runs in every pass. Non-main passes get valid 0, because push descriptors need binding 3 set.
- **VPlanetAtmo.cpp:** 47-48 (`bSsr` from "_SSR"), 165 (flag). VPlanet.h:60-61: `bool bSsr;` in PlanetShader.
- **VVessel.cpp:** 716, in vVessel::Render, next to PbrEnvScope: `SceneSsr::Scope ssr(scn, this, internalpass);`. Active only in RENDERPASS_MAINSCENE and with !internalpass. The opaque draw and the deferred glass both pass through here.
- **VBase.cpp:** 381-405 `Scope` with kind 1; 412-433 `Scope` with kind 2.
- **D3D9Effect.h/.cpp:** `_SSR` in BaseMacros (326). Handles `eSsr` ("gSsr"), `eSsrCol`, `eSsrHiZ` next to eExposure (472). A zero gSsr at 481 when SSR is on.
- **PbrRender.h/.cpp:**
  - "gSsr" joins SharedNames (49-50) only when `Config->SSR`, or the missing-name LogErr would fire;
  - hSsrCol/hSsrHiZ in Variant (346-357);
  - BeginVariant (374-406) copies `D3D9Effect::FX->GetTexture(eSsrCol/eSsrHiZ)`.
- **PostLook.cpp:248** (Look owner): sharpen amount from `scn->TaaActive()`, not bLabelsLate, which SSR now also sets.

## 9. Config, dialog, docs
- **D3D9Config.h:122:** `int SSR; // not upstream: screen-space reflections (0,1)`. Default 1 (D3D9Config.cpp:123), read and clamped (247), written (369).
- **Optional, cfg only:** `float SSRWetness` (0-1, default 0, dry), a base-surface wetness. See open question O1. If the owner wants strictly one key, the dry model is fixed.
- **Dialog:** `AUTOCHECKBOX "Screen-space reflections", IDC_FX_SSR, 290, 428, 95, 8` in Effects row 2 (D3D9Client.rc, after 286). The id is the next free in 3054-3999; the integrator assigns the ids of the five new features.
  - VideoTab.cpp: set at 936, saved at 1033.
  - The Effects group grows only if the five features need a third row.
- **Docs:**
  - VulkanClient.html Effects list after 682: "Screen-space reflections (`SSR` 1): water, runways and glossy vessel parts reflect what is on screen; elsewhere the sky or environment map as before";
  - README.md:225;
  - NEW.tex:48;
  - LAUNCHPAD.tex:555.

## 10. Cost (1080p, RTX 5070 Ti)
- **Targets:**
  - Capture ≤ 0.25 ms (copy, colour mips, Hi-Z level 0 and 10 levels);
  - TerrainPS trace ≤ 0.6 ms on a full-screen sea (Gfx Water Sea 1500);
  - TerrainPS on land only ≤ 0.05 ms extra (register pressure, Alps High);
  - glTF DG filling half the screen ≤ 0.35 ms;
  - KSC runway from the cockpit ≤ 0.2 ms;
  - total typical ≤ 1.0 ms, worst ≤ 1.6 ms.
- **What bounds it:** the 40-iteration cap, the weight and roughness early-outs, and 1 ray per pixel.
- **Measure:**
  - temporary VkQueryPool timestamps (vkCmdWriteTimestamp2, as the vshd agent did) around Capture, the planet loop (Scene.cpp:1807-1823), the vessel block (1862-1952) and RenderPbrDeferred(false);
  - mean of 300 frames, SSR on minus off, TAA on and off, MSAA 4 and 0;
  - static camera.

## 11. Interplay with the existing effects
- **TAA:**
  - The traces run before the resolve, so the stochastic sample is averaged (Stachowiak 2015). One sample per pixel, phase frame % 16.
  - Mirror-like water (α < 0.05) is deterministic.
  - The neighbourhood clip limits ghosting, because reflections move with the virtual image, not the surface (O3).
  - The capture is the TAA output, so it is anti-aliased and holds last frame's reflections (multi-bounce, converging since F < 1).
- **TAA off:**
  - no jitter;
  - the deterministic mirror plus the cone level;
  - labels are forced late (§2).
- **SSAO, volume clouds, volume cloud shadows:** composited before the capture, so they are reflected.
- **Shafts, flare, sharpen, vignette, heat haze:** after the capture, so they are not reflected (correct for lens effects).
- **Water:**
  - Glint (fSpe) and foam unchanged. Foam still scales cRfl (685).
  - The sun disc is not in the capture (RenderGlares is later), so there is no double sun.
- **Filmic:** HDRInv has the `_FILMIC` root. Mesh curves are unchanged (design-visual-batch M7).
- **Exposure:**
  - The source's e is undone (vDecode). The shader applies this frame's e.
  - Measure (Scene.cpp:2084) sees the reflections.
- **LightBlur (PostProcess = 1):** mesh shaders have no curve under `_LIGHTGLOW`, so MeshDecode skips the inverse, as Transmitted() does. RGBA16F keeps values above 1.
- **Night:** night lights, runway lights (drawn with the planet, VPlanet.cpp:1309) and stars are captured, so a wet runway mirrors the lights.
- **Other UE5-batch features:**
  - Shadows: nothing shared.
  - Plumes and plasma: drawn after the capture, so not reflected (O2).
  - SSGI: share tHiZ and tSsrCol (`SceneSsr::HiZ()`, `Color()`; with SSGI on and SSR off, Scene makes SceneSsr in data-only mode). Same sampler names. UBO binding 3 is taken by SSR in NewPlanet.
  - Height fog: applied per shader, so it rides along through the decode.

## 12. PBR, glTF, XR
- **PbrMesh:**
  - IBL specular through SsrDir with the bent normal under PBR_ANISOTROPY;
  - the clearcoat lobe (one trace);
  - iridescence only changes F;
  - sheen untouched;
  - PBRM_FULLYLIT has no IBL, so no SSR.
- **Transmission and BLEND glass** (RenderPbrDeferred(false), Scene.cpp:2016):
  - "current" mode, no lag, deterministic;
  - glass is not in tDepth, so it cannot hit itself;
  - Transmitted() is unchanged (the refraction).
- **Opaque PBR groups (PHASE_OPAQUE):** "previous" mode.
- **gSsr reaches the variants** through SharedNames and the two textures in BeginVariant. Env and custom passes get valid 0 from the FX default.
- **Legacy and XR1/2/5:**
  - PBR_PS, AdvancedPS and Metalness with EnvMapMode ≥ 1;
  - FAST groups have no reflection except base surfaces;
  - the XR2 HUD pass (P3) is untouched.
- **Per-vessel rigid matrices** keep self-reflections consistent: a wing reflecting the fuselage of a moving or rotating vessel.

## 13. Cockpit/VC, glass, labels, exposure and filmic, switch-off
- **VC:**
  - internalpass, so SSR is off. VC meshes have no env either (VVessel.cpp:801).
  - Cockpit glass (RenderPbrDeferred(true)) is off too: its reflection would show the outside, which the capture holds, not the cockpit.
  - Exterior parts seen from the VC (MESHVIS_EXTPASS) reflect as usual.
  - The VC and the 2D panel are never in the capture.
- **Glass:** §12. Legacy `_GLASS` groups draw in the main pass, so "previous" mode.
- **Labels and markers:**
  - Planet labels, planetarium art, surface labels and vessel markers draw after the capture (§2).
  - The celestial sphere's planetarium grids draw early (design-visual-batch Rev 3). Sky lookups are off while the planetarium is on.
- **Exposure and filmic:** §6, §11.
- **Switch-off:** §0. With SSR = 0:
  - D3D9Client.fx and PbrMesh.fx keep their sampler bindings (the new sampler_states are appended last and compiled out);
  - FxParams and PbrShared keep their layout;
  - TerrainPS is unchanged.
  - Checked by byte-comparing the SPIR-V (§15).

## 14. Risks and open questions
- R1 **One frame of lag** for anything moving relative to the reflector. For example, a DG belly over the runway at 100 m/s and 60 fps: 1.7 m. The per-vessel rigid matrix keeps self-hits exact. It is the same lag as engines that read last frame's colour.
- R2 **Single depth layer:**
  - nothing behind the front surface;
  - thin objects can give false hits or stop rays. τ is tuned in the checks.
  - Back faces cannot be told apart (no normals in Hi-Z).
- R3 **Register pressure** in TerrainPS and the vessel shaders lowers occupancy on non-reflective pixels too. It has a land-only check. If over budget, the trace moves behind a uniform branch with `[[dont_flatten]]` (GL_EXT_control_flow_attributes) or into a smaller helper.
- R4 **Display-referred source:**
  - mips are averaged before decoding (bright highlights soften);
  - y is clamped at 0.985 in HDRInv, so very bright vessel pixels reflect slightly dimmer in water;
  - with TAA off the source is 8-bit (BGRA8 backbuffer).
- R5 **Bindings:**
  - NewPlanet 21/22 rely on Earth having no `_MICROTEX` (#error guard);
  - D3D9Client.fx 28/29 leaves 2 free;
  - PbrMesh.fx 29/30 leaves 1 free.
  - SSGI must share, not add.
- R6 **Hi-Z texelFetch through IPI samplers set to noMip:** OpImageFetch takes its level from the operand, and the sampler LOD clamp does not apply. Check under validation.
- R7 **Sky lookups** assume the sky is at infinity. Clouds at a few km parallax by the camera-to-surface distance, which is small near the surface. Sky lookups are faded out from 3-12 km up.
- O1 **Wetness source:** cfg key `SSRWetness` (proposed), a dialog slider, or later a weather hook (cloud cover or rain)?
- O2 **Late capture:** capture after particles and RenderPbrDeferred(false), with labels and markers moved after it, so engine flames and plumes reflect in water and wet runways. Cost: one more copy plus a Scene.cpp reorder.
- O3 **Reflection-aware TAA reprojection** for glossy pixels (virtual-image depth) if smearing shows in fast camera moves.
- O4 **Default `SSR = 1`** (about 1 ms) like the rest of the batch? The owner decides.

## 15. Checks (scenarios and pass/fail)
- **Off:**
  - pixel-identical to dd4e087c in:
    - Gfx Water Coast 300, Coast 3km, Sea 1500;
    - Look KSC Sunset;
    - Gfx Clouds Orbit Alps;
    - glTF DG at KSC;
  - glslangValidator SPIR-V byte-identical for Earth TerrainPS/VS, the D3D9Client.fx VesselTech P0-P4, BaseTileTech, and the 32 PbrMesh variants (ORBITER_PBR_PRECOMPILE).
- **Water, Coast 300 at KSC:**
  - the towers and buildings mirror at the shore;
  - a vertical edge and its reflection meet at the waterline within 2 px;
  - clouds show in the water from sky lookups;
  - no seam where conf fades (mean luminance step ≤ 10 % across the fade band against the GetAmbient side).
- **Lake under mountains** (new build-tree scenario, an Alps lake, camera at 300 m): the ridges mirror; no streaks behind thin pylons or trees beyond τ.
- **"SSR DG Over Water"** (new, DG at 30 m over the Atlantic, chase camera):
  - reflection under the DG, lag ≤ 1 frame;
  - TAA on: no ghost trail longer than 2 px after a stop.
- **KSC runway from the DG cockpit, sunset:**
  - dry: a faint grazing sheen only;
  - `SSRWetness = 1`: hangars, sky and runway lights mirrored;
  - the VC shows no reflections on its own meshes or on the inside of the canopy.
- **glTF DG at KSC:**
  - chrome and glossy parts reflect the runway and other vessels;
  - the canopy (deferred glass) reflects with no lag;
  - a mirror sphere test (metallic 1, roughness 0) shows the hit colour within 2/255 (the curve pair).
- **Metalness or legacy vessel, EnvMapMode 1 and 2:** SSR where hits, env elsewhere, no hard edge at the screen border.
- **LEO:**
  - DG or ISS panels reflect the Earth when it is on screen (sky lookup);
  - the env cube takes over off screen with no pop;
  - water SSR is off (above 30 km).
- **Labels and planetarium on, TAA off:** no text or grid lines in reflections.
- **Static camera:**
  - TAA off: frame-to-frame difference in reflection regions p99 ≤ 1/255 (deterministic);
  - TAA on: p99 ≤ 4/255, as the TAA residual.
- **Camera cuts:** F3 vessel switch, internal/external toggle, ground-camera preset jump. The first frame shows env reflections only, no garbage.
- **Other modes:**
  - time warp ×100 in orbit, custom camera MFD, EnvMapMode 2: the secondary renders have no SSR;
  - PostProcess = 1 and Filmic = 1 run;
  - the mirror sphere test holds.
- **Robustness:**
  - mesh debugger camera: SSR off;
  - validation layers: no errors (layouts of tHiZ and tSsrCol, binding 3 always pushed);
  - client log: no shader errors and no "shared value missing".
- **Cost** per §10. ctest passes.

## 16. Pure maths and tests (SsrMath, Tests/SsrMath.Test.cpp)
- **Curve pairs:**
  - HDR/HDRInv (Expo 0.5-4), Filmic/FilmicInv, the mesh curve, sRGB;
  - round trip within 1e-4 on y ∈ [0, 0.985];
  - monotonic.
- **Mirror identity:**
  - MeshEncode(MeshDecode(y)) = y within 1e-3 for T ∈ [0.3, 1], I ∈ [0, 0.2], e ∈ [0.25, 2] (PbrMesh with sRGB; legacy; Metalness, with and without `_LIGHTGLOW`);
  - planet HDR(HDRInv(y)) = y at ratio 1.
- **Hi-Z build:** equals the brute-force max over each cell's footprint on random fields of odd size (37×23, 1×5).
- **Hi-Z walk:**
  - analytic scenes (ground plane plus a box, pinhole camera, 256×144);
  - over 1000 random rays, the hit pixel equals a 0.25 px linear march within 1 px for ≥ 99.5 %;
  - misses agree;
  - iterations ≤ N;
  - a ray behind the box by more than τ misses;
  - a sky ray ends at its vanishing pixel.
- **Near clip:** clipped w = near within 1e-6; a segment wholly behind is rejected.
- **Body matrix:** a static ground point and a point on a moving, rolling vessel (PostMath fixtures) map to their previous pixel within 0.01 px.
- **VNDF:**
  - H·N > 0 always;
  - for V = N, the CDF of θh matches tan²θ = α²u/(1 − u) within 0.01 (10⁵ samples, α 0.1/0.3/0.6);
  - α → 0 gives the mirror direction within 1e-3.
- **Other units:**
  - ConeLod: 0 at α = 0, monotonic, clamped;
  - EdgeFade: 0 at the border, 1 inside 6 %;
  - WetModel: endpoints and monotonic;
  - JumpTest: 250 m/s and 7.7 km/s at 60 fps keep the history; a 5 km teleport resets.
- **CMake:**
  - OVP/VulkanClient/CMakeLists.txt after 80: SceneSsr.cpp, SsrMath.cpp;
  - Tests/CMakeLists.txt after 225: `add_unit_test(SsrMath.Test …/SsrMath.cpp …/PostMath.cpp)` plus the include dir.
  - New .glsl files need no entry.

## 17. Files the implementation touches
- **New:**
  - OVP/VulkanClient/SceneSsr.h, SceneSsr.cpp, SsrMath.h, SsrMath.cpp;
  - shaders/Ssr.glsl, shaders/SsrPrm.glsl, shaders/HiZ.glsl;
  - Tests/SsrMath.Test.cpp.
- **SSR-only edits:**
  - shaders: NewPlanet.glsl (141-143, 181, 652, 660), Mesh.fx (BaseTilePS), PBR.fx (PBR_PS env, FAST_PS), Vessel.fx (AdvancedPS env), Metalness.fx (263), PbrMesh.fx (82, 109, 141, 144, 349, 361), D3D9Client.fx (99, 153, 535, 675);
  - C++: VVessel.cpp (716), VBase.cpp (381, 412), PbrRender.h/.cpp, VPlanet.h (60), VPlanetAtmo.cpp (47, 165).
- **Shared with other features (integrator):**
  - Scene.h/.cpp (hooks, labels, SsrActive), SceneFx.cpp (37, 273), Surfmgr2.cpp (1956, 1973);
  - D3D9Effect.h/.cpp (BaseMacros, handles);
  - D3D9Config.h/.cpp, resource.h, D3D9Client.rc, VideoTab.cpp;
  - doc/VulkanClient.html, README.md, Doc/Orbiter User Manual Linux/NEW.tex and LAUNCHPAD.tex;
  - OVP/VulkanClient/CMakeLists.txt, Tests/CMakeLists.txt;
  - PostLook.cpp:248 (Look owner: sharpen from TaaActive()).

## Rev 2 (implementation)
Branch ue5-batch (HEAD 35e2949c). Rev 2 overrides the body where they differ.

**Review MUSTs**
- M1 (one UBO binding): no binding 3. `SsrPrm gSsr` is appended at the end of `PlanetPSBlock` (binding 2) under `_SSR`, after the shadows' `gCsm`. SceneSsr::PlanetBind writes it on every Earth Render call, in every pass, zero (valid 0) when nothing is traced.
- M2 (Hi-Z bounds): L = min(9, ⌊log2 max(W,H)⌋ + 1) levels. Level 0 is padded to Wp × Hp, multiples of 2^(L−1) (2048 × 1280 at 1080p). PSHiZ0 writes 0 (sky) outside W × H, so every reduction is an exact 2 × 2 max. The walk also clamps each cell to [0, size_k − 1], with size_k = textureSize(tSsrHiZ, 0) >> k. tHiZTmp is Wp/2 × Hp/2 with L − 1 levels.
- M3 (walk toward the camera): at level > 0, when the segment is not wholly in front of the cell, t = clamp(t*, t, tx) only when s1 < s0; otherwise t is kept. Then lvl − 1. The level-0 test is the same in both directions: a hit when the segment is not wholly in front and its nearest z in the pixel is ≤ z_s + τ0 + τrel·z_s. SsrMath tests the walk against a dense march for rays both away from and toward the camera.
- M4 (dry base): F = EnvBRDFApprox(F0, √α, N·V) (Karis 2014). lit = mix(lit, L, F·conf), with no Lsky: where SSR misses, the legacy pixel stays. Early-out F < 0.03. The same in BaseTilePS, FAST_PS and PBR_PS, kind 1 only. The dry model is fixed: vSurf = (1, 0.30, 0.04, 1). There is no SSRWetness key (one switch). SsrMath::WetModel is kept, so a later weather hook only changes BaseParams.

**Review SHOULDs**
- S1 (water haze): the trace runs in the ripple block, and its hit and Fresnel weight are kept in locals. The mix is done after `sct`, just before `cTex += cRfl·0.75`:
  - finite hits: L = max(HDRInv(y) − I_w, 0)/max(atn_w, 1e-3), with I_w = (ray·RayPhase + mie·MiePhase)·(1 + fNoise);
  - sky lookups: HDRInv(y) as it is;
  - × (1 − fFoam) under `_WATERFX`.
- S2: y is clamped to 0.985 before both HDRInv branches (exp and filmic). Tested at y = 0.985, 1 and 2.
- S3 (sky lookup): only when c1 was not near-clipped and the walk reached t = 1. The pixel is (R, 0)·mSky with w > 0, and level 0 must be 0 there.
- S4: there is no data-only mode and no HiZ()/Color() for SSGI. SceneSsr exists only with SSR = 1.
- S5 + INT-5 (no double fog): the capture comes after the TAA resolve, so it already holds the fog. Capture strips it: when SceneFx::FogTex() drew this frame, PSUnfog writes c = (c' − f.rgb)/max(1 − f.a, 0.05) into tSsrCol (premultiplied over-blend, the FogTex contract in SceneFx.h). The fog pass then fogs each reflecting pixel once, for camera to reflector. The reflector-to-hit fog is not added (accepted).
- S6/INT-6: done by the integrator (bLabelsLate ORs Config->SSR; sharpening uses TaaActive()).
- S7/INT-4: the depth pass is always full range, so nothing is needed.
- S8/INT-8: ownership is as INT-8. The sampler_states sit next to the Ssr.glsl include, textually after the shadows' CsmS, so D3D9Client.fx gets 29/30 and PbrMesh.fx 30/31.

**Integration decisions that change the body**
- There is no BeginFrame hook. SceneSsr prepares the frame at its first main-pass parameter request, after UpdateCameraFromOrbiter. The reset rules are tested there:
  - the proxy, focus, target or internal/external changed;
  - no capture last frame;
  - the camera jumped more than 1 km + 2× the last move (proxy frame), or turned more than 45°;
  - mesh-debugger camera or wireframe.
- Capture(target) only. Vessel frames come from Scene's visual list (SceneSsr is a Scene friend). PostTaa::Output() comes through scn->pTaa.
- NewPlanet: tSsrCol 26 and tSsrHiZ 27 (not 21/22). VPlanetAtmo adds `_SSR` only for `bWater && bRiples && SSR && !EnableMeshDbg`, and `#error` guards `_DEVTOOLS`.
- D3D9Client.fx samplers 29/30 and PbrMesh.fx 30/31 (INT-3).
- D3D9Effect gets only `_SSR` in BaseMacros. SceneSsr looks up gSsr/tSsrCol/tSsrHiZ itself. The FX block starts zero-filled, so gSsr is valid 0 by default.
- PbrRender shares gSsr and both textures with the variants only when SSR = 1.
- Surfmgr2.cpp (not in my shared list): one line, `SceneSsr::PlanetBind(...)` after depthRec is set. It is listed for the integrator. The water is valid only when last frame's proxy terrain was recorded (NIT 5).
- Normal target (INT-4, SceneFx::Normal by ssgi): not used. Every host traces from its own exact normal (wave normal, normal maps, clearcoat). The hit surface's normal would only serve a back-face test, and PbrMesh.fx has no free sampler for it. Back-face hits are kept as hits: the occlusion is right, and only their colour is the front face's (R2, accepted).
- Water cone and stochastic α_ssr = 0.35·fRgh (NIT 8), tuned in the Coast check.
- NITs: 2 (the includes are inside `#if defined(_SSR)`, and Ssr.glsl has no uniform or sampler declarations), 3 (PrepareSample of both textures at the end of Capture), 4 (sky weight 1 for vessels and bases, 0 with the planetarium on), 6 (AdvancedPS LOD ×10), 7 (water early-out 0.75·fFS.y < 0.01).
