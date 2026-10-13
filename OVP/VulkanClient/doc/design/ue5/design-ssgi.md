# Design: screen-space global illumination (SSGI, one diffuse bounce)

Repo: Orbiter-linux-custom (/home/racerx/Projects/Orbiter-linux-custom), branch Orbiter-linux-custom, HEAD dd4e087c. Paths are under OVP/VulkanClient/ unless shown otherwise. All line numbers were re-read in this tree. Builds on design-visual-batch.md (Rev 2 and Rev 3), map-postprocess.md and vfx-changes.md.

## 0. Rules
- Switch `SSGI` in VulkanClient.cfg, read once at start. Off means the legacy image:
  - `_SSGI` is added to the three depth-pass shaders only when the switch is on, so their SPIR-V is unchanged when it is off;
  - no extra render targets, no IPI and no passes; FlagPass keeps its TAA-only gate.
- Our own code, written from published work:
  - Jimenez, Wu, Pesce, Jarabo 2016 (GTAO slices, the cos·|sin| slice measure);
  - Therrien, Levesque, Gilet 2023 (the visibility bitmask with a thickness). The brief's "Mayaux" is HBIL (Mayaux 2018), the earlier horizon-only method. It cannot see behind thin objects, so it is not used;
  - Ritschel, Grosch, Seidel 2009 (SSDO: on-screen lit colour as the sender radiance);
  - Cigolle et al. 2014 (octahedral normals);
  - Jimenez 2014 (IGN noise).
  - No GPL, vendor or Khronos sample code.
- One-line comments. Never touched: the 2D panel, HUD, MFD, GDI and the VC cockpit. The cockpit draws after AfterOpaque, Scene.cpp:2051-2079.
- No change to the lit shaders (NewPlanet TerrainPS, Metalness, PBR, PbrMesh, Mesh.fx). The bounce is added after the opaque scene, inside SceneFx.

## 1. What it does and why
- One bounce of diffuse light from what is on screen:
  - sunlit runway or tarmac onto vessel bellies and building walls;
  - a sunlit canyon wall onto the shadowed wall facing it;
  - lit regolith onto the shadow side of a lander on the Moon;
  - a vessel's own lit wing onto its fuselage;
  - runway and vessel lights onto nearby hulls at night.
- **Method: the visibility bitmask on GTAO slices, not ray marching.**
  - Each sample along a slice sets the sectors that its front-to-back span covers, assuming a thickness.
  - Sectors that are newly covered take that sample's radiance.
  - This gives a cosine-weighted gather over the whole slice, and objects behind thin ones still count.
  - It is lower in noise than rays at the same number of taps, and needs no HiZ.
- **Radiance source: this frame's lit opaque scene,** copied after SSAO and the volume-cloud shadows and before the volume clouds.
  - There is no lag and no reprojection error in the source, and no feedback, so it is exactly one bounce.
  - The previous TAA output was rejected as the source. Its sky and labels are mixed in, it needs reprojection for every tap, and a disocclusion samples the wrong surface.
- **Albedo: real albedo from the depth pass,** not an estimate.
  - SceneFx's DepthPass already draws every receiver: terrain tiles, bases, buildings, trees and vessels.
  - Under `_SSGI` it writes two more targets: albedo, and normal plus class.
  - **Why not estimate it:**
    - an estimate made from the lit colour (luminance-based, or "colour ÷ modelled light") cannot tell a cast shadow from a dark material;
    - in a cast shadow it gives an albedo near 0, which kills the effect exactly where it should show (shadowed canyon walls, ground in a vessel's shadow);
    - "multiply-in" (colour × GI) fails the same way, because dark receivers stay dark.
  - **Why not an albedo MRT in the main pass:** every pipeline of the MSAA main pass, including add-ons, would need a second attachment.
  - **Why the depth pass is cheap:** it has only three shader entries, all of them ours (SceneDepth.glsl, NewMesh.glsl SceneDepth_PS, the Tree.glsl distance pass).
- **Units.** Everything is worked in "exposed linear" units, the inverse of each family's display curve.
  - The terrain's 1 − e^(−u) inverts exactly, and so do the filmic fit, the mesh curve u(1+u⁴)^(−¼) and the airless curve u^γ.
  - Exposure cancels.
  - A per-family scale puts terrain and meshes on one radiance scale (§5.1).
- **Double counting.** The ambient that the shaders already add is assumed to come from every direction.
  - In each sector that a sender covers, only the sender's excess over that assumed ambient is added: max(L_s − L_amb, 0).
  - Vessels whose irradiance probes already contain the ground do not take terrain senders (§5.4).
- **Not done:**
  - large-scale darkening of the sky ambient. The legacy brightness is kept, and SSAO stays the darkening;
  - bent-normal sky relighting, which would need changes in the lit shaders;
  - exhausts and particles as senders, because they draw after AfterOpaque.

## 2. Where it hooks
- **Scene.cpp: no change.** Everything runs inside SceneFx::AfterOpaque (called at Scene.cpp:1954), before PostTaa::Resolve (1955), the jitter reset (1956), the late labels (1957-1961), Haze (1962) and Measure (2084).
- **SceneFx.cpp AfterOpaque (263-285):**
  - After 273: `bool ssgi = WantSSGI(vessels);`. 274: `if (!ssao && !clouds && !taa && !ssgi)`. 277: FlagPass when `(taa || ssgi) && sFlag`.
  - After 280 (VolShadow), before 281 (Clouds): `if (ssgi) Ssgi(target, vp); else GiDrop();`. The senders include SSAO and the cloud shadows; the volume clouds then cover the result.
- **DepthPass (287-302):** the MRTs are bound before the Clear at 292, which clears every bound target (VkCore.cpp:1180). The mesh class is set before 295 (bases and buildings) and before 296 (vessels).
- **FlagPass (304-333):** after the Clear at 323, the two MRTs are bound again without a clear. The camera target rewrites identical values, so tAlb and tNrm are not wiped.
- **Order in AfterOpaque:** DepthPass(+MRT) → FlagPass → Ssao → VolShadow → **Ssgi** → Clouds → (Scene) TAA resolve.

## 3. Passes and buffers (1920×1080; W2 = W/2 and so on)
| Buffer | Size | Format | Written by |
|---|---|---|---|
| tAlb | W×H | A2B10G10R10_UNORM: ρ/4 (rgb) | DepthPass MRT1 |
| tNrm | W×H | A2B10G10R10_UNORM: octahedral normal in global axes (xy), class (a: 0 terrain, 1/3 trees, 2/3 bases and buildings, 1 vessels) | DepthPass MRT2 |
| tScn | W×H | RGBA16F | StretchRect of the target (resolves MSAA) |
| tSrc0 / tSrc1 / tSrc2 | W2 / W4 / W8 | RGBA16F: v·cov (rgb), cov (a) | PSGiPrep MRT0; LINEAR halvings |
| tAtn | W2 | RGBA8: transmittance A (rgb) | PSGiPrep MRT1 |
| tIns | W2 | RGBA8: display inscatter I (rgb) | PSGiPrep MRT2 |
| tGi, tGiB | W2 | RGBA16F: E (rgb) | march, blur ping-pong |
| tHist[2] | W2 | RGBA16F: E (rgb), distance (a) | temporal, ping-pong |

- About 55 MB in all. Created in InitGi only when SSGI = 1.
- tFlag (W×H R8) is also created when SSGI = 1 and TAA = 0.
- **Steps per frame when it runs:**
  1. StretchRect target → tScn.
  2. PSGiPrep at W2, into three MRTs.
  3. StretchRect tSrc0 → tSrc1 → tSrc2, LINEAR.
  4. PSGiMarch → tGi.
  5. PSGiBlur, horizontal then vertical: tGi → tGiB → tGi.
  6. PSGiTemporal: tGi + tHist[cur] → tHist[nx].
  7. PSGiComposite at full res into the target, additive (SetBlendFunc(ONE, ONE), then Execute(2, true); IProcess.cpp:282-288 keeps the device blend func when blendop ≠ 1). The output is max(y′ − y, 0), so MSAA samples are kept, not overwritten.
- The device state is restored by AfterOpaque's GetState/SetState and its target re-push (275, 282-284).

## 4. Shaders
### 4.1 Depth-pass MRT (macro `_SSGI`, the switch on only)
- **SceneDepth.glsl (terrain tiles):**
  - DepthPrm gains `vec4 vTexOff; vec4 vAlb;`. vAlb = (gamma, scale, has texture, 0).
  - TerrainDepthVS also reads iNrmL (location 1) and iTex0 (location 5), the same as NewPlanet TerrainVS (440-443). It outputs the world normal (·mW) and uv·vTexOff.zw + xy.
  - The PS samples tDiff (binding 4). ρ = pow(tex, vAlb.x)·vAlb.y, which is NewPlanet.glsl:822 (pow·TrGamma·TrExpo) with air, and tex raw without air (the airless curve applies TrExpo/TrGamma after the lighting, 788).
  - Without a tile texture, ρ = 0.25 grey.
- **NewMesh.glsl SceneDepth_PS (228-235):**
  - New block `struct PSGi { vec4 vMat; vec4 vCls; }; layout(binding = 2) uniform PSGiBlock { PSGi ps_gi; };`.
  - ρ = (vMat.w > 0.5 ? tDiff.rgb : 1)·vMat.rgb.
  - Normal = normalize(frg.nrmW), flipped to face the camera.
  - class = vCls.x.
- **Tree.glsl distance pass (118):** in the `vAmb.w > 0.5` branch only, the leaf/grain/trunk albedo (120-123, copied under the macro) and n·mW. Class 1/3.
- Encoding: oct(n)·0.5 + 0.5 in rg, class in a; albedo·0.25 in rgb (ρ up to 4).
- **Off check:** glslangValidator SPIR-V without `_SSGI` is byte-identical for all three.

### 4.2 SSGI.glsl (new IPI)
- It #includes Scatter.glsl, as VolCloud.glsl does: Const at binding 1, tSun 4, tLndRay 6, tLndMie 7, tLndAtn 8.
- GiPS is at binding 2 (row_major, scalar): vCamX/Y/Z (SceneFx::CameraRays, jitter folded), vTex, vRad (R0, kFar, pxCap, strength), vFade (15e3, 25e3, frame%64, bAtmo), vCurve (TrGamma, s_world, e, uMax), vAmbM (mesh ambient in v units), mClassW, mRigP, mRigV, mPrevVP, vTemp (α static, α moving, reset, e ratio), vDir.
- Samplers: 13 tDepthG, 14 tNrmG, 15 tAlbG, 16 tScnG, 17-19 tSrc0-2, 20 tAtnG, 21 tInsG, 22 tGiG, 23 tHistG, 24 tFlagG. Each entry sets only the samplers it uses (IPI logs unknown names).
- ppf: `_PERFORMANCE` when !bAtmoQuality (table sizes, as SceneFx.cpp:89), `_FILMIC` when Filmic, `_LIGHTGLOW` when PostProcess == 1 (as D3D9Effect.cpp:327; the mesh curve is then the identity).
- **PSGiPrep (W2), per texel; the sender radiance in v units:**
  - It takes the full-res texel that SSAO's Snap picks, and averages the 2×2 texels whose depth is within 2 % of it.
  - Without air: A = 1 and I = 0.
  - With air, A and I come from GetLandView(|P + CamPos|, normal of P), giving A = atn and I = Tw(Expo·insc) (ray·RayPhase + mie·MiePhase, as NewPlanet.glsl:889).
  - Conversions by sender class (curves in §5.1; s_c = s_world for terrain and trees, 1 for meshes):

    | Sender | Air | Formula |
    |---|---|---|
    | terrain | yes | v = (Tw⁻¹(y) − Tw⁻¹(I)) / max(A, 0.05) / s_world |
    | trees | yes | v = Tw⁻¹((y − I)/A) / s_world |
    | meshes | yes | v = Tm⁻¹((y − I)/A) |
    | terrain | no | v = Ta⁻¹(y) / s_world |
  - y is clamped to ≤ 0.98 and v to ≤ uMax = 3, so water glints and specular highlights stay bounded.
  - Outputs: tSrc0 = (v, 1), or 0 for sky. tAtn = A. tIns = I.
- **PSGiMarch (W2):**
  - Receiver set-up:
    - P = PixelRay(uv)·d. N and the receiver class come from tNrm. v_amb is computed once per pixel (§5.3).
    - Radius R = max(R0 = 25 m, 0.06·d). Its pixel radius is clamped to 4 px – 0.25·H2, and R is recomputed after the clamp.
  - Slices:
    - 2 slices, at angles (k + ξr)·π/2.
    - The slice plane is the plane through the camera that holds the rays at uv and at uv + ω (exact under perspective).
    - n_p is N projected into the plane, with angle γ against V = −P̂.
  - Samples:
    - 8 per side, at o_j = r·((j − 1 + ξs)/8)² + 1 px.
    - The source level is chosen by tap spacing: < 2 px tSrc0, < 4 px tSrc1, else tSrc2. Radiance = rgb/max(a, 1e-3).
    - A side stops when it leaves the screen. Depth 0 (sky) is skipped.
  - Thickness T by sender class:
    - terrain 1e6 (a heightfield);
    - trees 4 m;
    - bases and buildings max(1, 0.01·d);
    - vessels max(0.5, 0.005·d).
  - Per sample:
    - the front and back angles (the back is pushed T along the sample's ray) are clipped to [γ − π/2, γ + π/2] and mapped through the slice CDF F (§5.2) to a bit range;
    - new = m & ~occ; occ |= m;
    - L = W[rc][sc] · smoothstep(−0.05, 0.25, N_s·(−Δ̂)) · (1 − smoothstep(0.6R, R, |Δ|)) · max(v_s − v_amb, 0);
    - S += popcount(new)·L.
  - Per slice: E += |n_p|·M(γ)·S/32. Then E /= 2.
  - Self-plane bias: |Δ·N| < 0.003·d + 0.02 m is ignored, as SSAO's 0.002·d.
  - ξr = IGN(px + frame·5.588238) and ξs = IGN(px.yx + 17 + frame·5.588238): per-pixel, per-frame (the own history averages it, §5.5).
- **PSGiBlur:** 9 taps along vDir at W2.
  - Weight = exp(−i²/8) · exp(−|Δd|/(0.02d + 0.05)) · max(n·n_i, 0)⁸.
  - Depth and normal are read at Snap texels (as SSAO PSBlur).
- **PSGiTemporal:**
  - Q = mRigP·P, or mRigV·P where tFlag > 0 (the camera target and what is docked or attached to it).
  - Its previous pixel is Q·mPrevVP; the expected distance is |Q|.
  - The history (bilinear) is valid when it is on screen, not a reset frame, and |d_hist − |Q|| < 0.03|Q| + 0.2 m.
  - Blend: α = 0.08 at rest, 0.25 when the motion is over 1 W2 px, 1 when the history is invalid. The history is × e_now/e_prev.
  - Output (E, d).
- **PSGiComposite (full res):**
  - E is the depth- and normal-aware 2×2 upsample of tHist[nx] (SSAO PSComposite's weights × n·n_q⁸). A and I are bilinear.
  - Inputs: ρ from tAlb, y from tScn, class from tNrm.
  - Δv = strength·ρ·E·(1 − smoothstep(15e3, 25e3, d)).
  - Result by receiver class:

    | Receiver | Air | y′ |
    |---|---|---|
    | terrain | yes | Tw(Tw⁻¹(y) + A·s_world·Δv) |
    | trees | yes | Tw(Tw⁻¹((y − I)/A) + s_world·Δv)·A + I |
    | meshes | yes | Tm(Tm⁻¹((y − I)/A) + Δv)·A + I |
    | terrain | no | Ta(Ta⁻¹(y) + s_world·Δv) |
  - Output max(y′ − y, 0) with a = 0. Depth 0 outputs 0, so the sky never changes.

## 5. Data and maths
### 5.1 Curves and the common unit
- Tw(u) = 1 − e^(−u), the planet HDR() (Scatter.glsl:206-213) and Tree.glsl:134. Under `_FILMIC`, Tw(u) = ACES(0.568u) (Scatter.glsl:200-204, Tree.glsl:131-132). Its inverse is the positive root of (2.51 − 2.43y)x² + (0.03 − 0.59y)x − 0.14y = 0, then ÷ 0.568.
- Tm(u) = u(1 + u⁴)^(−¼) (Metalness.fx:393-394, PbrMesh.fx:538-542). Tm⁻¹(y) = y(1 − y⁴)^(−¼). Under `_LIGHTGLOW`, both are the identity. Mesh.fx/PBR.fx clip and do not curve; Tm⁻¹ differs from y by under 2 % for y < 0.6.
- Ta(u) = min(u, 1)^g with g = Const.TrGamma (airless terrain, NewPlanet.glsl:788). Ta⁻¹(y) = y^(1/g).
- **Common unit v = e·x** (the exposed linear radiance of a mesh):
  - Terrain shows Expo·x with Expo = expoBase·e (VPlanetAtmo.cpp:581); airless terrain shows TrExpo·x with TrExpo = trBase·e (582).
  - So s_world = Const.Expo/e with air and Const.TrExpo/e without. Trees take the planet exposure (vSunCol.w), so the same s.
  - With this, a terrain sender and a mesh receiver use one radiance scale, and auto-exposure cancels.
- **Albedo units:**
  - terrain ρ = pow(tex, TrGamma)·TrExpo (NewPlanet.glsl:822) with air, tex without air;
  - meshes ρ = tex × material diffuse, or baseColor × factor for PBR;
  - trees ρ = the procedural albedo.
  - The shaders have no 1/π and light the ground with "cL·2" (866), so the shader-unit irradiance is E = (1/π)∫L cos dω. That is exactly what the cosine-weighted bitmask returns. No extra gain.

### 5.2 Slice measure
- μ(θ) = cos(θ − γ)·|sin θ| on [γ − π/2, γ + π/2], with θ measured from V in the slice plane. This is the GTAO hemisphere measure.
- F is its CDF in closed form: ∫ = −cos(2θ − γ)/4 + θ·sin γ/2 for θ ≥ 0, mirrored for θ < 0. M(γ) is the total.
- Bits are uniform in F, so each bit has mass M/32.
- Estimator: E = (1/S)·Σ_k |n_p,k|·(M_k/32)·Σ_bits L. This is unbiased over random slice angles; a fully covered hemisphere with uniform L gives L.

### 5.3 Ambient already in the shaders (v units)
- **Terrain and tree receivers with air:**
  - v_amb = e·(cA + cAmbient·Ambient), where cA is NewPlanet.glsl:855 with fM = 0.5. It uses AmbientApprox (Scatter.glsl:345-360), GetSunColor and Const.TW_Terrain at the receiver.
  - Without air, v_amb = 0: the Moon has black shadows today, and this is the biggest visible gain.
- **Mesh receivers:** v_amb = e·Sun.Ambient (D3D9Sun.Ambient, D3D9Util.h:279), in vAmbM.

### 5.4 Class weights W[receiver][sender] (mClassW, set on the CPU)
- Every receiver class except vessels takes every sender.
- Vessel receivers, when probes run (Config->EnvMapMode && Config->bIrradiance && Scene::GetIrradianceDepthStencil()):
  - terrain senders get 0. The probe renders the planets from the vessel's centre (VVessel.cpp:1314-1411, Scene.cpp:2781-2789), so the ground is already in its ambient (Metalness.fx:266-274, PbrMesh.fx:228-235);
  - bases and buildings get 0 only in EnvMapMode 2 (flag 0x20);
  - trees and vessels get 1 (self-bounce is not in the probe; RndList.erase(this) at VVessel.cpp:1358).
- Without probes, vessel receivers take every sender too.

### 5.5 Reprojection
- SceneFxGi keeps its own previous view. It is captured as PostTaa::Capture does (PostTaa.cpp:98-128):
  - Camera.pos;
  - the proxy and camera-target R and o (oapiGetRotationMatrix/GetGlobalPos);
  - V·P with _31/_32 zeroed.
- mRigP and mRigV are built with PostMath::Rigid as affine 4×4s, in double, and uploaded as float.
- **Reset:** a proxy, focus, target or internal-camera change; the last Ssgi frame was not frame − 1; or DebugControls camera mode or wireframe (as PostTaa::BeginFrame, 70-96). A reset sets α = 1.

### 5.6 Gate
- WantSSGI = Config->SSGI && gi->ok && depthOK && (the camera is under 20 km above the ground, or a vessel is within 2 km). This is WantSSAO's test (SceneFx.cpp:155-162).
- Without a proxy planet, or without ScatterConst, a static zeroed ConstParams with TrGamma = 1 is used and bAtmo = 0, so vessels near each other in deep space still self-bounce.
- A frame that does not run drops the history.

## 6. C++ changes per file
- **SceneFx.h:**
  - members: tAlb/sAlb, tNrm/sNrm, `struct GiState; GiState *gi`;
  - private: `InitGi()`, `static FreeGi(GiState*)`, `WantSSGI()`, `Ssgi(target, vp)`, `GiDrop()`;
  - public `VkTex *GiNormals() const, *GiAlbedo() const`, valid when written this frame (for SSR).
- **SceneFx.cpp:**
  - 37: InitDepth also when SSGI. After 40: `if (Config->SSGI && depthOK) InitGi();`.
  - 53: pTerrain options `"_SSGI"` when SSGI. After 55: tAlb and tNrm W×H A2B10G10R10_UNORM (RTUsage). On failure: LogErr and SSGI off.
  - 56: tFlag when `Config->TAA || Config->SSGI`.
  - 129: FreeGi(gi), delete the MRTs.
  - AfterOpaque and FlagPass as in §2.
  - DepthPass:
    - before 292: `dev->SetRenderTargetN(1, sAlb); dev->SetRenderTargetN(2, sNrm);`;
    - before 295: `D3D9Mesh::SetGiClass(2/3.f)`; before 296: `SetGiClass(1.f)`;
    - the MRTs are unbound before PopRenderTargets.
- **SceneFxGi.cpp (new, like SceneFxShadow.cpp):**
  - GiState: the IPI, the targets of §3, the previous View, cur, the last frame, e_prev and ok.
  - InitGi: targets, then the IPI with entry PSGiPrep plus CompileShader for the other four. Any failure gives LogErr and SSGI off; SSAO, the clouds and TAA are untouched.
  - Ssgi:
    - builds the uniforms: CameraRays; Const and tables as Clouds() (SceneFx.cpp:446-452); e = scn->GetExposure(); s_world from Const; vAmbM from scn->GetSun()->Ambient·e; mClassW; the matrices;
    - runs the 7 steps.
- **Mesh.h:**
  - MeshShader `hPSG = GetPSHandle("ps_gi")`, NULL when the block is absent;
  - `static void D3D9Mesh::SetGiClass(float)` with its static storage.
- **Mesh.cpp:**
  - 3764: SHADER_SCENE_DEPTH gets opt `Config->SSGI ? "_SSGI" : NULL`.
  - RenderShadowMap opt 2 (3096-3146), when `pShader->hPSG`:
    - PBR groups bind t (baseColor, or the legacy texture) even without an alpha test, and vMat = (baseColor rgb, t ? 1 : 0);
    - legacy groups: vMat = (material Diffuse rgb, hTex ? 1 : 0);
    - vCls.x = the static class;
    - then SetPSConstants(hPSG), next to 3144-3145.
  - ps_bools and the alpha test are unchanged.
- **Surfmgr2.cpp RenderDepth (1835-1855):** when Config->SSGI, per tile:
  - `sh->SetTexture("tDiff", t->tex, IPF_LINEAR | IPF_CLAMP)`, or the grey flag when there is no texture;
  - prm.vTexOff = GetTexRangeDX(&t->texrange), as SurfTile::Render (1155);
  - prm.vAlb from the proxy's Const: (TrGamma, TrExpo) with air, (1, 1) without;
  - a larger prm struct is used only then (the block matches the macro).
- **TreeMgr.cpp:110:** `"_SSGI "` added to fl when SSGI.
- **Not changed:** Scene.cpp, PostTaa, PostLook, NewPlanet.glsl, Scatter.glsl, Metalness/PBR/PbrMesh/Mesh.fx, Cloudmgr2.

## 7. Config, dialog, docs
- **D3D9Config.h** after 122: `int SSGI;` (0,1) and `float SSGIStrength;` (0…2).
- **D3D9Config.cpp:**
  - defaults after 122: SSGI = 1 and SSGIStrength = 1.0 (the default is the owner's call, see §11);
  - read after 246, clamped;
  - write after 368.
- **Dialog:** resource.h after 196: `IDC_FX_SSGI` (the integrator picks it from 3054-3058, shared with the other four features). D3D9Client.rc after 286: `AUTOCHECKBOX "Global illumination", IDC_FX_SSGI, 290, 428, 75, 8`. VideoTab.cpp next to 936 and 1033. SSGIStrength is cfg only, as SSAOStrength is.
- **Docs:**
  - VulkanClient.html: a new `<li>` after 678-681: "Global illumination (`SSGI` 1, `SSGIStrength` 1.0): one bounce of sunlight and lamp light from nearby surfaces on screen";
  - README.md:225-226;
  - Doc/Orbiter User Manual Linux/NEW.tex:48 and LAUNCHPAD.tex:555 (the Effects list).
- **CMake:**
  - OVP/VulkanClient/CMakeLists.txt next to 75-78: SceneFxGi.cpp and SsgiMath.cpp;
  - Tests/CMakeLists.txt after 225: `add_unit_test(SsgiMath.Test …/SsgiMath.cpp)` with the include dir. Shaders copy as a directory (no list entry).

## 8. Cost
- Target on the RTX 5070 Ti at 1920×1080: **≤ 1.0 ms for Ssgi** (steps 1-7), plus **≤ 0.1 ms** extra in the DepthPass. The ceiling is 1.5 ms.
- Expected split (ms):

  | Step | ms |
  |---|---|
  | copy | 0.03 |
  | prep | 0.06 |
  | halvings | 0.01 |
  | march | 0.45 |
  | blur | 0.08 |
  | temporal | 0.04 |
  | composite | 0.08 |
  | depth MRT and terrain texel | 0.08 |
- March: 32 taps × 3 fetches (tDepth, tNrm, tSrc) at 0.52 Mpx.
- **Measurement:**
  - temporary vkCmdWriteTimestamp around Ssgi and around DepthPass with SSGI on and off, as the vshd agent did;
  - mean of 300 frames in KSC Belly, Canyon and Gfx Forest; the largest is reported. The code is removed after.
  - CPU: the time for the per-tile SetTexture in RenderDepth is logged once.
- **If over the target:** 1 slice with the slice angle alternating per frame (the history still converges), and 6 steps.

## 9. Interplay
- **TAA:**
  - The composite lands before Resolve, so TAA anti-aliases the GI edges. TAA's history holds stable GI, because GI has its own history.
  - CameraRays carries the jitter (SceneFx.cpp:384-399), so P matches the jittered tDepth.
  - The half-px jitter is ignored in the W2 reprojection.
  - Late labels draw after the composite (Scene.cpp:1957).
- **SSAO:** it stays its own pass and is not merged:
  - its 1.5 m radius has 12 dense taps, while the GI radius is 25 m and up and screen-proportional; 8 quadratic steps put only 1-2 taps inside 1.5 m;
  - SSAO darkens all light (with its 0.6·lit reduction) while SSGI only adds;
  - merging would change the tuned AO look and tie the two switches together, to save about 0.15 ms;
  - they share the depth pass, Snap, IGN and the blur pattern;
  - senders include the AO darkening, and the bounce is not darkened by AO, which is right: the bounce is what fills the occluded region.
- **Volume clouds and their shadows:** senders include the cloud shadows. The clouds composite after and cover the result. The sky is never changed.
- **Shafts and flare:** unaffected. Their sky test needs tDepth == 0, and those pixels get no GI. The flare visibility is unchanged.
- **Water:** sender v ≤ 3 limits the glint; water receivers take ρ from tDiff. The coast check is in §13.
- **Filmic:** `_FILMIC` selects the exact inverse and forward ACES fit for terrain and trees. Meshes keep Tm, as filmic leaves them unchanged (M6/M7).
- **Exposure:**
  - v units cancel e, and the history is rescaled by e_now/e_prev.
  - Measure runs after (Scene.cpp:2084), so auto-exposure sees the real added light; deep valleys come out slightly darker after adaptation.
- **LightBlur (PostProcess = 1):**
  - the target is the RGBA16F offscreen, so the additive Δ works above 1;
  - `_LIGHTGLOW` makes the mesh curve the identity;
  - LightBlur's own curve applies after.
- **Heat haze:** it copies the target after the composite, so the shimmer includes GI.
- **VC cockpit and glass:** they draw after, so they are not receivers. Glass shows the GI'd scene behind it.
- **Night lights and runway lights:** emissive pixels bounce as senders (bonus). Light sprites that have no depth act as if at the ground behind them.

## 10. PBR, glTF and XR
- glTF opaque and MASK groups are in SceneDepth opt 2 (Mesh.cpp:3096-3108).
  - Under SSGI the baseColor texture is bound for opaque groups too, and the factor goes in vMat.
  - BLEND and transmission groups are skipped (3098-3099), so glass is neither a sender nor a receiver.
  - Animated groups use pGrpTF (3134-3141) as today.
- **Albedo convention:**
  - PbrMesh shows Tm(LinearToSrgb(x)·e) (PbrMesh.fx:538-542), so its "v" is in sRGB-encoded space, like the legacy display-space lighting.
  - The baseColor is written as stored (sRGB-encoded).
  - The bounce on PBR mid tones can differ by up to about 1.5×. This is tuned or accepted in the check (§11).
- Emissive PBR parts show in tScn and so act as senders. Emission is not in ρ.
- XR vessels and legacy meshes take the legacy-group path. Metalness probe rules as in §5.4.
- PbrRender is unchanged. tNrm holds the interpolated vertex normal, not the normal map.

## 11. Risks and open questions
1. **MRT through ShaderClass/MeshShader draws:**
   - it must be confirmed that VkCore's pipeline key includes the extra colour attachments of SetRenderTargetN (VkCore.h:206). IPI MRT works (VolMarch);
   - FlagPass must bind the MRTs after its Clear.
   - Fallback: FlagPass leaves locations 1/2 unbound; Vulkan discards those writes.
2. **Shared G-buffer with SSR:** tNrm (global octahedral normal plus class) could serve SSR. The integrator decides whether the macro becomes `_GBUF`, set when SSGI || SSR, and who owns the depth-pass shaders. The shadows agent may also edit NewMesh.glsl and Mesh::RenderShadowMap.
3. PBR sRGB-vs-linear albedo (§10).
4. The probe rule is approximate: in EnvMapMode 1 the probe has no base tiles (the runway), so a DG on the SLF still takes the runway bounce from SSGI. That is right, but the result depends on the mode.
5. **Screen-space limits:**
   - senders off screen or hidden are missing, so GI fades at the screen edges while panning;
   - back faces are only partly handled by the facing test;
   - sender radiance includes specular, which is clamped.
6. **Lag:** α 0.08 gives about 12 frames of lag for moving bounce sources (a taxiing vessel's bounce on the ground). Non-target moving vessels fail the depth test and show fresh, blurred noise.
7. Terrain normals are the tile vertex normals, so relief finer than the 64-grid is not in the GI.
8. The display inscatter and transmittance for trees and meshes are approximated from GetLandView, not the per-vessel gSun.Transmission/Inscatter. The error is small within 25 km.
9. **Default:** SSGI = 1 is proposed, like the other effects. The owner decides.
10. Large-scale sky occlusion, bent-normal sky light, and exhausts as light sources are left for later.
11. Water receivers use tDiff, not the water shader's colour.
12. At time warp ×100 the sun moves quickly, and the history lags by α. The e-ratio rescale does not cover lighting changes.

## 12. Pure maths: SsgiMath.h/.cpp + Tests/SsgiMath.Test.cpp
- The unit has no Vulkan or oapi code, and is mirrored in GLSL.
- Contents:
  - Tw/Tw⁻¹ (exp and filmic), Tm/Tm⁻¹, Ta/Ta⁻¹;
  - SenderV(y, A, I, class, air, s) and Composite(y, A, I, ρE, class, air, s);
  - OctEncode/OctDecode with 10-bit quantising;
  - SliceMass(γ), SliceCdf(θ, γ) and BitMask(θa, θb, γ);
  - Radius(d) and PixelRadius;
  - ClassWeights(envMode, irradiance, probes);
  - and, test only, a CPU march over analytic height fields.
- **Tests:**
  - **Curves:** inverse∘forward round trip within 1e-4 for y ∈ [0, 0.98]; monotonic; the filmic inverse matches a bisection.
  - **Composite:**
    - Δy ≥ 0, and y′ ≤ 1 for terrain;
    - additive: Composite(Composite(y, a), b) = Composite(y, a + b) within 1e-4 for terrain, trees and meshes;
    - ρE = 0 changes nothing.
  - **Octahedral:** 10k random unit vectors round-trip within 0.15° after quantising.
  - **Slice CDF:** F = 0 and 1 at the ends; monotonic; matches the numeric integral of μ within 1e-4 for γ in ±80°.
  - **Estimator vs brute-force cosine-weighted visibility:**
    - a receiver at the bottom of a pit with wall elevation α ∈ {10°, 30°, 60°} gives E/L = sin²α within 3%, averaged over 512 slice rotations, for view angles 0/45/70° and normal tilts 0/30°;
    - a half-space wall gives the analytic form factor within 3%;
    - a thin slab of thickness T lets the sectors behind it through.
  - **Class weights:** the (mode, irradiance) table for (0,0), (1,1) and (2,1).
  - **Radius:** continuous at R0/0.06, and the pixel clamp holds.

## 13. Checks (1920×1080, NVIDIA, validation layers on)
- **Off:**
  - SSGI = 0 is pixel-identical to the dd4e087c build in Orbit Alps, Alps High, Carpathians Under and KSC DG;
  - SPIR-V is identical for SceneDepth.glsl, NewMesh.glsl SceneDepth_PS and Tree.glsl without `_SSGI`;
  - no SSGI line in the client log.
- **New build-tree scenarios (Scenarios/Tests):**
  - Gfx SSGI KSC Belly: a DG on the SLF at noon, camera 3 m up beside the nose gear;
  - Gfx SSGI Canyon: Grand Canyon, sun 15°, camera 3 km away;
  - Gfx SSGI Moon: a DG at Brighton Beach, sun 20°;
  - Gfx SSGI Hangar: a DG beside a KSC building;
  - Gfx SSGI Night: KSC with the runway lights.
- **Pass criteria:**
  - **KSC Belly, EnvMapMode = 0:** belly mean luminance up by at least 8 %; runway more than 50 m from the DG within 1/255 on average; sky 0.
  - **KSC Belly, probes on:** (SSGI on − off) on the belly is no larger than in the probes-off run.
  - **Canyon:** shadowed walls facing sunlit walls up by at least 5 %; sunlit areas change by less than 2 %.
  - **Moon:** the vessel's shadow side and shadowed regolith next to lit regolith up by more than 3/255; black sky unchanged.
  - **Hangar:** the wall base next to sunlit tarmac is brighter, with no halo around the DG in front of it.
  - **Stability:** static camera, TAA on and off, frame-to-frame p99 ≤ 2/255 in the GI'd areas after 30 frames.
  - **Motion:** Gfx TAA Chase: no trail over 4 px behind the DG; frame-to-frame p99 on the vessel ≤ 4/255.
  - **Other combinations:** PostProcess = 1, Filmic = 1, TAA = 0, SSAO = 0, VolClouds with VolCloudShadows (Alps High), Look KSC Sunset (flare and shafts as before), Water Coast 300 (no patch brighter than +10/255 on hulls next to the glint), the VC in LEO (cockpit pixels unchanged), the custom camera MFD (unchanged), the PBR bench and the glTF DG (no NaN, plausible), XR2 at KSC, Gfx Forest (trees receive and send, no shimmer), Night (lights bounce, no blow-out).
  - **No regressions:** no pixel darker than with SSGI off (additive), no NaN or Inf, no validation errors, no shader errors in the log.
  - **ctest:** SsgiMath.Test passes.
  - **Cost** as in §8.

## 14. Files the implementation touches
- **New:**
  - OVP/VulkanClient/SceneFxGi.cpp
  - OVP/VulkanClient/shaders/SSGI.glsl
  - OVP/VulkanClient/SsgiMath.h, SsgiMath.cpp
  - Tests/SsgiMath.Test.cpp
- **SSGI-owned edits:**
  - shaders/SceneDepth.glsl
  - shaders/Tree.glsl (distance pass only)
  - Surfmgr2.cpp (RenderDepth only)
  - TreeMgr.cpp:110
- **Shared with other features; the integrator splits these:**
  - SceneFx.h and SceneFx.cpp (InitDepth, AfterOpaque, DepthPass, FlagPass);
  - shaders/NewMesh.glsl (SceneDepth_PS) and Mesh.h/Mesh.cpp (RenderShadowMap opt 2, 3764). The shadows agent likely edits the same function;
  - D3D9Config.h/.cpp, resource.h, D3D9Client.rc, VideoTab.cpp;
  - OVP/VulkanClient/CMakeLists.txt and Tests/CMakeLists.txt;
  - OVP/VulkanClient/doc/VulkanClient.html, README.md, Doc/Orbiter User Manual Linux/NEW.tex and LAUNCHPAD.tex.
- **Read but not changed:** Scene.cpp, PostTaa.*, PostMath.* (Rigid and Mul are reused), Scatter.glsl (included), VolCloud.glsl, SSAO.glsl.

## Rev 2 (implementation; branch ue5-batch, HEAD 35e2949c; overrides the body)
### Integrator decisions that change the design
- INT-5: the pass is `SceneFx::Gi(target, vp)`, called by the integrator's AfterOpaque after VolShadow and before FogAndClouds (so the fog and the clouds cover the bounce). No WantSSGI/GiDrop in AfterOpaque: Gi() checks its own gate.
- INT-4: ssgi owns the depth-pass MRT. The extra targets are bound only inside DepthPass and unbound before it returns, through three one-line hooks: `GiBind(0, vessels)` after PushRenderTarget and before the Clear (gate, MRT bind, mesh class 2 for the planet's bases and buildings), `GiBind(1, vessels)` before the vessels (class 3), `GiBind(2, vessels)` before PopRenderTargets (`SetRenderTargetN(2, NULL)`, `(1, NULL)`, class -1). FlagPass binds nothing.
- INT-4: public `VkTex *SceneFx::Normal() const` and `Albedo() const` (SceneFx.h) for ssr: W×H A2B10G10R10; normal rg = oct(world normal)·0.5 + 0.5, a = class round(a·3) (0 terrain, 1 trees, 2 bases and buildings, 3 vessels); albedo rgb = ρ/4, a = family round(a·3). NULL unless written this frame. The MRT is bound when SSGI initialised and (GI runs this frame or Config->SSR), so SSR has normals in every frame with a depth pass.
- INT-2: no new UBO. NewMesh.glsl: `PSGi ps_gi` is appended to PSConstBlock (binding 1) under _SSGI (not a binding-2 block). SceneDepth.glsl: vTexOff and vAlb appended to DepthPrm under _SSGI. SSGI.glsl is its own IPI (binding 2 block, samplers 13-22).
- INT-6: bLabelsLate already includes Config->SSGI (S4 done by the integrator). INT-4 depth predicate already has SSGI (M1 done, Surfmgr2.cpp:1974). Depth is full range (S10 plumes cut gone).
### Review items
- **M1** done by the integrator (DepthWanted in Surfmgr2's depthRec).
- **M2** the MRT lives only between GiBind(0) and GiBind(2) inside DepthPass; FlagPass and every other pass see no extra target. Writes to locations 1/2 without an attachment (FlagPass, CSM casters) are dropped.
- **M3** tAlb.a = family: 0 legacy (PBR.fx/Vessel.fx/Mesh.fx), 1 Metalness (Grp.Shader == SHADER_METALNESS), 2 PbrMesh (IsPbrGroup), 3 base tile (a D3D9Mesh flag set by the base-tile constructor). The probe rule zeroes terrain senders (and bases/buildings in EnvMapMode 2) only for vessel receivers of family 1 or 2, when Config->EnvMapMode && Config->bIrradiance && scn->GetIrradianceDepthStencil().
- **M4** mesh and tree receivers: v_amb = e·(GetObjectAtmoParams' sky ambient ported to GLSL at the receiver: unit(cAmbient + 2·anti-sun SunLightColor)·fA·tw_bld·cAmbient.g·exp(−alt·iH.x)·rmI.x·6e5, via Scatter.glsl's Gauss7/RayLength; config level only in space or without air) + config level. tw_bld comes from vp->GetAtmoParams()->tw_bld (the same call UpdateScatter makes each frame). Terrain receivers use NewPlanet's cA + cAmbient·Ambient (fM 0.5). Mirrored in SsgiMath::MeshAmbient with tests.
- **M5** every inversion works on q = clamp((y − I)/max(A, 0.05), 0, qMax) (0.98 for curved families, uMax for identity ones); terrain uses max(Tw⁻¹(y) − Tw⁻¹(I), 0). The composite outputs max(y′ − (q·A + I), 0); E and every output are NaN/Inf-guarded (non-finite → 0) and E is clamped to [0, 4·uMax]. Tests: I > y, A = 0.01, y = 1, filmic y = 1.
- **M6** metalness is folded into ρ in the depth pass: ρ ·= 1 − metal; PBR metal = factor·mr.b (tMR at binding 5), legacy Metalness metal = map.g or D3D9MatExt::Metalness. Fully lit groups (UsrFlag 0x4) write ρ = 0.
- **S1** per-family inverse/forward pairs in prep and composite: legacy and base tile identity with the shaders' light clamp at 1 (Δv ≤ e·ρ − v), Metalness Tm, PbrMesh Tm∘sRGB. Round-trip tests per family.
- **S2** PbrMesh: tAlb holds the linear base colour (·(1 − metal)); UNORM fallback textures are decoded in the depth pass (PbrRender::NeedsDecode, as Mesh.cpp:2823). Composite: c = SrgbToLinear(Tm⁻¹(q)/e), c′ = c + ρ·SrgbToLinear(E/e), y′ = Tm(LinearToSrgb(c′)·e)·A + I.
- **S3** airless terrain: Δx = ρ·(LightFX(E0 + E/e) − LightFX(E0)), E0 = r/(2 − r), r = min(x/ρ, 1.9). Metalness: lights add in quadrature (LightFXSq): L = x/ρ, S = 2L²/(3.24 − L²), S′ = S + (E/e)², L′ = 1.8√S′/√(2 + S′). Both tested against the shader formulas.
- **S4** done by the integrator (INT-6).
- **S5** prep caps each sender: v_s ≤ 1.5·e·ρ_s·(2·|sun at P| + ambient at P + 0.5·night), night = clamp(−4·(up·toSun) + 0.05, 0, 1) as the night lights' on-time; ρ_s display-encoded.
- **S6** the prep has one output: tAtn/tIns are dropped; A and I are computed per pixel where needed (6 table reads in the composite), which is exact per pixel. Outputs 1-3 are set NULL before every entry anyway.
- **S7** N is flipped toward the camera for every class, and γ is clamped to [−π/2, π/2].
- **S8** a failure sets gi->ok = false only. Surfmgr2 keys the larger DepthPrm and the tDiff bind on the compiled shader (GetPSHandle("tDiff") exists only under _SSGI); Mesh keys on hPSG.
- **S9** octahedral round-to-nearest; tests: max error < 0.25°, mean < 0.1°.
- **S10** INT-4/INT-5 (order and full-range depth) settle it.
- NITs: 2 (M(γ) = cos γ + γ sin γ, bits round(32·F/M)), 3 (GetLandView(|P_g|, P_g/|P_g|)), 4 (tilted cases against brute force), 5 (additivity for y′ < 0.95), 6 (tap spacing in half-res px), 7 (round(a·3)), 9 (class −1 outside DepthPass: CSM casters skip the GI uploads) are taken. 8: Normal()/Albedo() are kept for ssr (INT-4).
### Own decisions
- tFlag stays TAA-only (no change to InitDepth or AfterOpaque): when Flag() is valid it picks mRigV as in PostTaa; without it (TAA off) vessel-class pixels use mRigV.
- Gate in GiBind(0) (it has the vessel list): ok && (camera < 20 km over the ground or a vessel within 2 km). A frame without GI drops the history.
- No proxy or no ScatterConst: a zeroed ConstParams with TrGamma 1, bAtmo 0, s = 1 (vessels in deep space still self-bounce).
