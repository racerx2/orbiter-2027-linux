# Design: whole-scene sun shadows (SceneShadows)

Repo: Orbiter-linux-custom (/home/racerx/Projects/Orbiter-linux-custom), branch Orbiter-linux-custom, HEAD dd4e087c, clean tree. Read-only survey; every line number below is from that tree. Builds on design-visual-batch.md (Rev 2/3 as built) and vfx-changes.md.

## 0. Rules
- One switch, `SceneShadows` in VulkanClient.cfg, read once at start. Off: no SceneShadows object, no atlas, no `_CSM`/`_CSMCAST` macro anywhere, no new C++ branch taken: legacy pixels and SPIR-V.
- Own code from published work: cascaded shadow maps (Engel, ShaderX5 2006), camera-centred clipmap levels (Epic, UE5 Virtual Shadow Maps docs 2021), texel snapping (Valient, ShaderX6 2008), normal offset (Holbert, GDC 2011), PCSS blocker search (Fernando, NVIDIA 2005), interleaved gradient noise (Jimenez 2014), best-candidate disk (Mitchell 1991). No GPL, Khronos or vendor sample code.
- One-line comments. 2D panel, HUD, MFDs, GDI, labels untouched; nothing drawn over the sim screen.
- PBR/glTF, XR vessels, legacy Mesh/Metalness/Vessel paths and add-ons keep working.

## 1. What it does and why
- Today (Custom):
  - Focus-vessel ortho maps: Scene::RenderShadowMap (Scene.cpp:2575-2668), R32F colour `1 - z` + D24/D32 DS per LOD (ptShmRT/psShmDS, Scene.cpp:259-268), drawn again per vessel cluster (1869-1944). Received by Vessel/PBR/Metalness/PbrMesh through ComputeShadow (Common.glsl:380) and by the terrain (NewPlanet.glsl:547-554) only below 10 km and only inside the map's sphere (Surfmgr2.cpp:1178-1205).
  - Stencil projections on a ground plane: vessels (VVessel.cpp:1053), base structures (VBase.cpp:482), particles.
  - Terrain on terrain: horizon maps (fH, NewPlanet.glsl:558-564; trees Tree.glsl:127). Terrain on objects: CPU march TerrainSunVisibility (VPlanet.cpp:733), folded into sun colour per vessel (VVessel.cpp:1517), per base (VBase.cpp:428), per building cell (BldgMgr.cpp:523).
  - Nothing casts on buildings, trees or the ground except the focus cluster; buildings and trees cast nothing.
- New: one camera-centred cascade atlas per main frame, cast by terrain relief, bases, streamed buildings, 3D trees and all vessels; received by terrain, base tiles, base structures, buildings, trees and vessels. 0-4.5 km sharp, out to 42 km soft, faded into the horizon maps by 42 km.
- **Decision: a new module SceneShadows.h/.cpp, not an extension of RenderShadowMap.** The legacy maps are per-vessel spheres, re-rendered several times a frame between vessel draws, with LOD-sized targets and a receiver contract (gLVP/gSHD/ShadowS) shared by every mesh draw. Turning that into cascades would change the legacy image and the vessel draw order. The legacy maps stay as they are (best for vessel self-shadow detail and in orbit); SceneShadows adds a second, independent visibility that receivers combine with min() (section 10).
- Casters reuse the existing depth paths: SceneDepth.glsl TerrainDepthVS/PS for tiles and Mesh::RenderShadowMap opt 2 (NewMesh.glsl NormalDepth_VS/SceneDepth_PS: PBR alpha modes, legacy alpha test, glass left out) for vessels, bases and buildings. Trees get a caster variant of Tree.glsl.

## 2. Where it hooks (Custom file:line)
- Scene.cpp:346-348 ctor: `if (Config->SceneShadows) pShd = new SceneShadows(gc, this);` after pFx. Dtor 381-383: SAFE_DELETE(pShd).
- Scene.cpp:1648 (after ComputeLocalLightsVisibility, before the main Push at 1662): `if (pShd) pShd->Render();`
  - Camera, RenderList (1602-1611), UpdateCamVis (1599: planets, BldgMgr::Update and TreeMgr::Update at VPlanet.cpp:827-828) are done; nothing of the main target is open yet.
  - It also sets bStencilShadow = false on covered vessels, after the RenderList loop set it true (1609) and before the legacy focus code (1746).
- Scene.cpp:1955, after `pTaa->Resolve`: `if (pShd) pShd->AfterResolve();` sets the noise phase to the stable hash (glass, exhausts and add-on meshes drawn later are not averaged by TAA).
- Scene.cpp:2016, after `RenderPbrDeferred(false)`: `if (pShd) pShd->EndMain();` turns receivers off before the VC (2058-2079), the overlay and next frame's custom-camera and env turns.
- New pass id RENDERPASS_SCENESHADOW 0x000A (Scene.h:57); its label "SCENESHADOW" in both arrays at D3D9Client.cpp:1207 and 1241 (index 10, else LogDbg reads past the array).

## 3. Passes and buffers
- **Atlas:** one VkTex 4096 x 4096 VK_FORMAT_D32_SFLOAT, usage DEPTH_STENCIL_ATTACHMENT | SAMPLED, and a VkSurf on it. Four 2048 x 2048 tiles: cascade i at ((i & 1), (i >> 1)) x 2048. 64 MB. Not cleared as a whole; each refreshed tile is cleared through the viewport (VkDev::Clear clips to the viewport, VkCore.cpp:1196-1205).
  - Fixed 2048 per cascade (ShadowMapSize stays the legacy maps' key).
  - Start-up check: FormatHas(D32_SFLOAT, SAMPLED_IMAGE | DEPTH_STENCIL_ATTACHMENT) as D3D9Frame.cpp:322-326 does for the legacy maps; if not, log "SceneShadows: no sampled D32 depth" and stay off.
- **Caster pass, depth only:**
  - Save device state and targets (as ImageProcessing::Execute does), `dev->SetRenderTarget(NULL, sAtlas)` (VkDev::SetRenderTarget accepts a NULL colour, VkCore.cpp:1047; BeginRendering then attaches depth only, 1107-1161), BeginPass(RENDERPASS_SCENESHADOW).
  - Depth test and write on, LESS_OR_EQUAL; blend off; cull NONE (open Orbiter meshes, both winding conventions); SetDepthBias(0, 1.5) slope only.
  - Per refreshed cascade: SetViewport and SetScissor to its tile, Clear(depth 1.0), draw its casters (section 5).
  - Restore: SetScissor(NULL), PopPass, targets and state back. EndRendering moves the atlas to SHADER_READ_ONLY (VkCore.cpp:1163-1170).
- No colour, no new render-target formats, no per-frame allocation. Fallback if depth-only rendering misbehaves in the framework: an R32F colour atlas plus a D32 DS, as the legacy maps (+64 MB).

## 4. Cascades
### 4.1 Split scheme
- Camera-centred nested levels (UE5 directional clipmap idea), not view-frustum slices: a level depends only on the camera position, so turning the camera never changes a map (no rotation shimmer, orbiting a parked vessel is the common Orbiter view), and coarse levels can be reused across frames (4.4).

| Cascade | Box half-size r | Texel (2048) | Refresh | Terrain LOD cut | Trees |
|---|---|---|---|---|---|
| C0 | 80 m | 7.8 cm | every frame | leaves | meshes |
| C1 | 600 m | 59 cm | every frame | leaves | meshes < 150 m, impostors beyond |
| C2 | 4.5 km | 4.4 m | every 2nd frame | spacing >= 8.8 m | impostors |
| C3 | 45 km | 44 m | every 4th frame | spacing >= 88 m | none |

- Receiver selection by camera distance d: the first i with d < 0.94 r_i; band 0.80-0.94 r_i blends into i+1. Beyond C3: no CSM.
- Fade: weight k = 1 - smoothstep(30 km, 42 km, d); vis = mix(1, vis, k). 42 km + 2 km stale shift + 10-texel kernel (0.44 km) stays inside r_3.

### 4.2 Light frame and texel snapping (planet frame, double on the CPU)
- Render space is camera-relative with global axes and the ground turns up to 465 m/s in it, so a global-frame grid would slide under static ground and shimmer. The grid is fixed in the proxy planet's frame.
- Per frame, double: R_p (oapiGetRotationMatrix of the proxy), P_p, camera C (Scene::GetCameraGPos), sun S.
  - L_p = R_pᵀ unit(S - C) (toward the sun, planet frame); pole a = (0,1,0).
  - X = unit(a × L_p) (fallback (1,0,0) × L_p when |a × L_p| < 0.05, with hysteresis), Y = L_p × X, rows B = (X, Y, -L_p): x, y across the light, z away from the sun.
  - Camera in light space: q = B R_pᵀ (C - P_p).
  - Cascade i: texel t_i = 2 r_i / 2048; snapped centre m = round(q.xy / t_i) t_i; depth from z0 = q.z - r_i - 20 km (casters up to 20 km toward the sun) to q.z + r_i.
- A camera-relative point v maps to s = B R_pᵀ v + (q - (m, z0)). For a static ground point g_p (planet frame), s.xy = B g_p - m: independent of the camera except whole-texel steps of m, and independent of the planet's rotation. Only L_p's own drift (0.004 deg/s) moves the grid, 0.075 texel/s at the box edge: real sun motion, no shimmer.
- GPU gets float: the rotation B R_pᵀ and a translation with |xy| < t_i/2 and z = r_i + 20 km. No large numbers reach the GPU.
- Caster clip matrix: x = s.x/r_i, y = s.y/r_i, z = s.z/(2 r_i + 20 km), w = 1 (D3D [0,1] depth, the framework's negative viewport keeps y up). Receiver matrix: the same, then uv = (x/2 + 1/2, 1/2 - y/2) (as ComputeShadow, Common.glsl:386) scaled 0.5 and offset to the tile.
- D32F precision at z near 1 over C0's 20.16 km range: 1.2 mm; no depth clamp needed (no VkCore change).

### 4.3 Gates (SceneShadows::Render decides once per frame)
- Main pass; camera proxy is a vPlanet with surfmgr2 and its ElevMode not Spherical (last frame); camera height over the loaded ground (GetLoadedElevation at the camera) < 42 km; sun elevation at the camera's ground point > -3 deg.
- Otherwise: no caster pass, receivers off (gCsm.vF.w = 0), stencil shadows as legacy. In orbit the cost is a few CPU checks.

### 4.4 Refresh schedule and reuse
- C0, C1 every frame. C2 on even frames, C3 on frames = 1 mod 4 (never more than 3 tiles a frame).
- A tile is also refreshed when: the camera moved more than 0.05 r_i in its light space since it was drawn; L_p turned more than 1/2048 rad (half a texel at the box edge); proxy, focus or camera mode changed; it was never drawn.
- A reused tile keeps (B_k, m_k, z0_k) of its frame k; this frame's receiver matrix is rebuilt from them with this frame's camera and R_p (4.2), exact for static ground. Moving vessels lag (n - k) frames in C2/C3 only (C2: at most 1 frame, 3.3 m at 200 m/s on 4.4 m texels).

## 5. Casters and culling per cascade
- Cull test (CsmMath::CullSphere): caster sphere (camera-relative centre c, radius ρ) to light space s = B R_pᵀ c + T_i; drawn when |s.x|, |s.y| < r_i + ρ and z0 - ρ <= s.z <= q.z + r_i + ρ (behind the receivers it can shadow nothing). Casters with ρ < 0.5 t_i are skipped in that cascade.
- **Terrain** (proxy surfmgr2): new `TileManager2<SurfTile>::RenderCasters(sh, mClip, cull, minSpacing)` in Surfmgr2.cpp next to RenderDepth (1835).
  - Under loader->WaitForMutex(), as RenderDepth.
  - Walks the tile tree as last frame's ProcessNode left it: out-of-view tiles are refined by distance too (Tilemgr2_imp.hpp:163-173 keeps stepping down), so the tree around the camera is complete.
  - A node draws its own mesh when it is a leaf (ForRender or Invisible) or its grid spacing (pi R / 2^lvl / GridRes) >= minSpacing; else it descends into TILE_ACTIVE children. Subtrees whose sphere fails the cull are skipped.
  - Tile matrices are recomputed for this frame: SetRenderPrm with the planet's current dmWorld, then WorldMatrix(tile) (Tilemgr2.cpp:1186, 1221); tile->mWorld is not touched. Shader: SceneShadows' own SceneDepth.glsl ShaderClass.
- **Bases:** new vBase::RenderCasters: structure_as only (tilemesh and structure_bs are flat ground and would self-shadow the base tiles), skipped when terrainSun <= 0 (its shadow lies in terrain shadow); RenderShadowMap(&mWorld, &mClip, 2) per mesh after the cull.
- **Buildings:** new BldgMgr::RenderCasters, RenderDepth (BldgMgr.cpp:467-490) with the light cull in place of the camera frustum, cells with c.vis <= 0 skipped; C0-C2 only.
- **Trees:** new TreeMgr::RenderCasters using DrawCells with a caster mode and a second ShaderClass "TreesCsm" built with `_CSMCAST` (TreeMgr.cpp:110-111); cells culled by the light box; C0-C2 per the table in 4.1.
- **Vessels:** every active vessel (vobjFirst, as Casters at Scene.cpp:1718), not only visible ones; per cascade `v->Render(dev, false)` after the cull.
  - VVessel.cpp: RENDERPASS_SCENESHADOW joins the SHADOWMAP rules at 664 (terrainSun <= 0: return) and 683 (exterior meshes only). The dispatch at 794-799 gets `else if (pass == RENDERPASS_SCENESHADOW) mesh->RenderShadowMap(pWT, scn->CasterVP(), 2);`
- Not casters: clouds (the volume and 2D cloud shadows stay as they are), particles, exhausts, beacons, base tiles.

## 6. Bias, soft edges, noise
- Raster: slope bias 1.5 (SetDepthBias(0, 1.5)).
- Receiver normal offset along the geometric normal n: p' = p + n · 1.5 t_i · sinθ, θ = angle(n, L) (Holbert 2011). Depth bias b = t_i (1 + min(tanθ, 4)) · kb + zb, in metres, divided by the depth range.
  - kb = 3 where the legacy map is on for that draw (gShadowsEnabled): the legacy map carries the fine self-shadow, the CSM only far casters, so no CSM acne on hulls.
  - Trees: n = L and zb = 0.5 × tree height (own crown ignored; neighbours still shadow).
- Soft edges, per cascade (PCSS-lite):
  - Blocker search: 4 textureGather taps (16 texels) on an 8-point best-candidate disk of radius 4 texels, rotated by φ. No blocker: return 1. All 16 blocked and the centre gather blocked: return 0.
  - Penumbra: w = (z_r - z_b,avg) · 2 tan ω, with ω the sun's angular radius from its size and distance (gCsm.vL.w; Earth 0.0093, Mars 0.0062).
  - Filter radius R = clamp(w / (2 t_i), 1, 4) texels. If w / (2 t_i) > 4 and i < 3, the lookup moves to cascade i+1 (wide physical penumbra from far casters without a wide kernel).
  - PCF: 8 gathers on the disk × R, rotated by φ; each gather is 2×2 bilinear-weighted compares (fract(uv · 4096 - 0.5)). Taps clamped to the tile rect minus one texel.
- Noise φ:
  - TAA running (gCsm.vF.z = frame % 8, set while Scene::TaaActive()): φ = 2π IGN(gl_FragCoord.xy + 5.588238 · vF.z), as Tree.glsl:113-114, so TAA averages it.
  - TAA off, or after the resolve (AfterResolve sets vF.z = -1): φ = 2π hash(floor(uv · 4096)), fixed per shadow texel, so it does not crawl with the camera.
- Cascade band: with TAA, pick i or i+1 per pixel by the same noise against the band weight. Without TAA, evaluate both and mix (only in the band, about 14 % of a level).
- Pixels with n·L < -0.05 skip the lookup (unlit already).

## 7. Shaders: entries, macros, bindings

### 7.1 New include shaders/SceneShadow.glsl
```
struct CsmPrm {
	mat4 mC[4];	// camera-relative world to atlas uv (xy) and depth 0..1 (z), per cascade
	vec4 vC[4];	// texel m, 1 / depth range m, select radius m (0.94 r), r m
	vec4 vL;	// toward the sun (xyz), 2 tan of the sun's angular radius (w)
	vec4 vF;	// fade start, fade end m, noise phase (-1 stable hash), on
};
```
- `#if defined(_CSM) || defined(VKFX_REFLECT)`: `layout(binding = 3, row_major, scalar) uniform CsmBlock { CsmPrm gCsm; };`.
  - Binding 3 is free in every receiver: NewPlanet (0, 1, 2), Tree (0), D3D9Client.fx (FxParams 0, FxLegacyAtmo 1), PbrMesh.fx (PbrShared 0, PbrMaterialBlock 1).
  - VKFX_REFLECT is needed because VkEffect's parser lists every uniform block, #if or not, and its reflection compile references the member (VkShader.cpp:598-627).
- `#if defined(_CSM) && defined(STAGE_PS)`: `float CsmVis(sampler2D tC, vec3 posW, vec3 nW, float kb, float zb)`, with the sampler as a parameter so each file passes its own binding. It uses gl_FragCoord, hence the stage guard.
- C++ mirror `CsmGpu` in SceneShadows.h, 352 bytes, static_assert.

### 7.2 Receivers

| File | Entry, line | Macro | Sampler | Change |
|---|---|---|---|---|
| NewPlanet.glsl | TerrainPS, after the horizon block 558-564 | `_CSM` | tShadowMap, binding 25 (reused) | `fShadow = min(fShadow, CsmVis(tShadowMap, -frg.camW.xyz, normalize(frg.nrmW), 1, 0))`; under _CSM without _WATERFX the specular at 880 uses fShadow (min(fH, csm)) for fH |
| Tree.glsl | TreePS 127-129 | `_CSM` | tCsm, binding 4 (Tree has no sampler) | `fH = min(fH, CsmVis(tCsm, posW, L, 1, 0.5 h))`; h is a new TreeVS member under _CSM |
| Vessel.fx | AdvancedPS 131-133 | `_CSM` | CsmS (D3D9Client.fx, binding 28) | `#if _CSM` combined line, `#else` the legacy line untouched |
| PBR.fx | 209-211, 484-487 and 509-511 | `_CSM` | CsmS | same; fShadow at 485 becomes the min and is declared with SHDMAP 0 too, so the fSun line at 510 runs under `SHDMAP > 0 \|\| _CSM` |
| Metalness.fx | 292-294 | `_CSM` | CsmS | same |
| Mesh.fx | BaseTilePS 207-227 | `_CSM` | CsmS | d and s of the sun term (221) × CsmVis |
| PbrMesh.fx | 512-515 | `_CSM` | CsmS, binding 29 | same min with the legacy |

- Combination in mesh shaders: `f = 1; f = legacy (as today, SHDMAP > 0 only); f = min(f, CsmVis(CsmS, -frg.camW, normalize(frg.nrmW), gShadowsEnabled ? 3 : 1, 0)); cSun *= f;`. With ShadowMapMode 0 (SHDMAP 0), only the CSM term remains.
- **Bindings, the NewPlanet problem.** NewPlanet.glsl has no free sampler (31 is the last, 24 shared, 26-28 dev tools). Solution: under _CSM the terrain stops sampling the focus-vessel map and binding 25 (tShadowMap, already a sampler2D) carries the atlas. The focus vessel and its cluster are CSM casters, so the terrain loses nothing.
  - Surfmgr2 under SceneShadows: the legacy block 1178-1205 is skipped (fc->bShadows stays false, the per-tile SetTexture at 1204 is not done); TileManager2<SurfTile>::Render binds the atlas once per planet with IPF_CLAMP | IPF_POINT.
  - The legacy terrain projection was only used below 10 km, where the CSM is active.
- An atlas, not a sampler2DArray: VkEffect's sampler_state parser maps every type except samplerCube/sampler3D to a 2D view (VkShader.cpp:567), and VkDev::DefaultTexture has no 2D_ARRAY default (VkCore.cpp:1580-1590), so an array would need framework changes and a second NewPlanet declaration. The atlas works with every existing sampler2D path.
- **D3D9Client.fx:** `uniform extern texture gCsmMap;` and `sampler2D CsmS = sampler_state { Texture = <gCsmMap>; MinFilter = POINT; MagFilter = POINT; MipFilter = NONE; AddressU = CLAMP; AddressV = CLAMP; };` after Planet3S (526-534), inside `#if defined(_CSM)`.
  - It is the 25th sampler_state, so binding 28; it must stay last so no other binding moves.
  - Other batch features adding samplers append after it (28 + n <= 31).
- **PbrMesh.fx:** the same after IridThickS (141), binding 29.
- `#include "SceneShadow.glsl"`: D3D9Client.fx before Particle.fx (675), so Mesh.fx, Vessel.fx, PBR.fx and Metalness.fx all see it; PbrMesh.fx before Common.glsl (144); NewPlanet.glsl after Scatter.glsl (8); Tree.glsl top. Includes are expanded as text before parsing (VkShader.cpp:38-75), so the file has an include guard. Whole file inside #if, so with the switch off it adds no token.
- Macro routes: `_CSM` in D3D9Effect::BaseMacros (D3D9Effect.cpp:296-337; also the PBR effects, design-pbr 6), in the Mars/Moon and Earth flag strings after VPlanetAtmo.cpp:146 and 170, in TreeMgr's flags (TreeMgr.cpp:110). Clouds, giants and HazeMgr do not get it.

### 7.3 Casters
- SceneDepth.glsl and NewMesh.glsl: unchanged.
- Tree.glsl `_CSMCAST` (second ShaderClass "TreesCsm"):
  - VS: mesh/impostor choice by camera distance against a per-tree threshold MeshEnd_c + FadeW · hash(iCol.z), complementary between the two draws, so there is no screen-door.
  - Impostor faces the sun: across r = unit(up × Ls), up axis u = unit(up - Ls(up·Ls)), height scaled by cos e + w sin e (crown seen from the sun; w the kind's width ratio, e the sun elevation).
  - PS: the crown/trunk outline discard only, no dither, no colour.

## 8. Data needed and where it comes from
- Sun position and size: oapiGetObjectByIndex(0), oapiGetGlobalPos, oapiGetSize. Proxy planet: Scene::GetCameraProxyVisual(), oapiGetRotationMatrix, GlobalPos, dmWorld (updated in UpdateCamVis 1599).
- Camera: Scene::GetCameraGPos; ground under it: vPlanet GetLoadedElevation.
- Casters: vobjFirst (vessels), vPlanet's surfmgr2 tree, vbase[] (proxy only, render flag 0x20 as VPlanet.cpp:1336), bldgmgr, treemgr.
- terrainSun per vessel/base and c.vis per building cell: last values (caster skip only).
- Frame id and TaaActive from Scene; ElevMode from surfmgr2.

## 9. C++ changes per file
- **SceneShadows.h/.cpp (new):**
  - Ctor: atlas, surface, own SceneDepth ShaderClass; logs "SceneShadows: atlas 4096 D32, on".
  - Render(): gates, CsmMath frame data, refresh decision, caster pass (3, 5), the receiver block, FX values (D3D9Effect::eCsm, eCsmMap), stencil flags.
  - AfterResolve(), EndMain().
  - Accessors: Active(), Atlas(), Gpu() (the block, with `on` and the phase for the current pass), CasterVP(), Cull(c, ρ), Covers(c, ρ) (centre distance + ρ < 36 km, the fade middle).
- **CsmMath.h/.cpp (new, pure):** Basis, Snap, Matrices (double in, float out), CullSphere, Schedule, Penumbra, FadeWeight, NormalOffset/Bias, Disk(8).
- **Scene.h/.cpp:**
  - Member `class SceneShadows *pShd = nullptr;`, friend SceneShadows (Camera, vobjFirst, sunLight).
  - RENDERPASS_SCENESHADOW; `const D3DXMATRIX *CasterVP() const`; `bool ShadowsCover(const D3DXVECTOR3 &c, float r) const`.
  - Hooks at 1648, 1955, 2016; ctor/dtor.
- **D3D9Client.cpp:** labels at 1207 and 1241.
- **D3D9Effect.h/.cpp:** handles eCsm ("gCsm"), eCsmMap ("gCsmMap") next to eShadowMap (454); `_CSM` in BaseMacros.
- **PbrRender.h/.cpp:** "gCsm" added to SharedNames (49-50) when SceneShadows is on; hCsmS handle; BeginVariant (375-386) copies the CsmS texture as it does ShadowS.
- **VPlanetAtmo.cpp:** `_CSM` flags (146, 170).
- **Surfmgr2.cpp, Tilemgr2.h:**
  - RenderCasters (declared next to RenderDepth, Tilemgr2.h:537).
  - In Render (1861): gCsm and the atlas bound per planet (on only for the main pass and the proxy).
  - Legacy shadow block skipped under SceneShadows (1178-1205).
- **VPlanet.h/.cpp:**
  - `RenderCasters(dev, sh, SceneShadows&)` next to RenderDepth (1333), for tiles, bases, buildings, trees.
  - RenderBaseShadows (1316): in the main pass, bases the CSM covers get no stencil shadow.
- **VBase.h/.cpp:** RenderCasters (next to RenderDepth 439); RenderGroundShadow (482) returns when covered in the main pass.
- **BldgMgr.h/.cpp:** RenderCasters (next to 467).
- **TreeMgr.h/.cpp:**
  - Caster ShaderClass and RenderCasters (DrawCells caster mode: light cull, per-cascade bands, no frustum).
  - Receiver: gCsm and tCsm in the main mode; `_CSM` in the flags.
- **VVessel.cpp:** SCENESHADOW at 664, 683, 794-799. bStencilShadow is set by SceneShadows; RenderGroundShadow (1056) already returns on it.
- **D3D9Config.h/.cpp, VideoTab.cpp, resource.h, D3D9Client.rc:** the switch (section 11).
- **CMakeLists.txt** (OVP/VulkanClient:75-80): SceneShadows.cpp, CsmMath.cpp next to SceneFx.cpp. **Tests/CMakeLists.txt** (after 224): `add_unit_test(CsmMath.Test .../CsmMath.cpp)` plus the include dir, as LookMath.
- Unchanged: NewMesh.glsl, SceneDepth.glsl, Mesh.cpp, VkCore, IProcess, ShaderParams/FlowControl structs (no static_assert change).

## 10. Interplay
- **Focus-vessel and cluster maps (legacy):** kept as they are. On meshes the shader takes min(legacy, CSM); min, not product, because both estimate the same sun visibility (no darker overlap in the vessel's own penumbra). kb = 3 on those draws keeps CSM self-shadow out. Terrain: legacy projection off, the CSM carries the focus vessel. Base structures and buildings: the FX toggle at VPlanet.cpp:1035-1046 stays, so the focus map still lands on hangars at full resolution, min'd.
- **Stencil shadows:**
  - Vessels covered by the CSM get bStencilShadow = false in the main pass (VVessel.cpp:1056 then skips).
  - Bases covered skip RenderGroundShadow.
  - Particles keep their ground shadows (not CSM casters).
  - Custom cameras and env maps keep all stencil shadows (the flag only acts in MAINSCENE).
  - So no stencil darkening over a CSM shadow. Beyond 36 km, stencil as legacy, and the CSM there has faded below 0.5.
- **Horizon shadows:**
  - Terrain fShadow = min(fH, mix(1, csm, k)): the CSM's sharp relief near the camera, the horizon map's full-range shadow everywhere, no double.
  - Trees the same with their per-tree fH.
  - Terrain-on-objects (CPU terrainSun folded into sun colour) stays a product with the CSM. They agree except in a ridge penumbra while the sun disc crosses it (minutes at sunset), where the product is slightly darker. Accepted; open question 15.3.
- **2D cloud shadow (fShd) and volume cloud shadows (SceneFx VolShadow composite):** different occluders, so products, unchanged. _VOLSHD's fade of fShd (NewPlanet.glsl:857-858) is untouched.
- **TAA:**
  - Maps are not jittered (own matrices; SetCameraAperture's jitter applies only to the camera projection). Receivers use world positions.
  - Static shadows are planet-stable, so the history is valid. Moving vessel shadows are handled by the clamp like any moving content. Noise per 6.
- **SSAO:** independent (ambient only). **Volume clouds:** draw over the shadowed scene as today.
- **Shafts and flare:** unchanged (sun-point tests). Shadowed ground lowers the shaft mask's false hits slightly.
- **Water (_WATERFX):** the glint is × fShadow × fShd (NewPlanet.glsl:878), so vessels, buildings and terrain now kill the glint. Without _WATERFX under _CSM the glint uses fShadow in place of fH.
- **Filmic, exposure, LightBlur:** no change; auto-exposure sees a slightly darker scene and adapts as designed.
- **VC cockpit:** no CSM on the interior (EndMain is before 2058). The exterior seen through the windows is shadowed, including the focus vessel's own ground shadow.
- **Add-on meshes** drawn in GENERICPROC_RENDER_EXTERIOR (between 1955 and 2016) receive through the FX like vessels.

## 11. Config, dialog, docs
- D3D9Config.h after 122: `int SceneShadows; // not upstream: sun shadows of terrain, bases, buildings, trees and vessels near the camera (0,1)`. D3D9Config.cpp defaults after 122 (`SceneShadows = 1;`), read after 246 (`max(0, min(1, i))`), write after 368.
- Dialog: Effects group, row 2 after Heat haze: `AUTOCHECKBOX "Scene shadows", IDC_FX_SCENESHD, 290, 428, 60, 8` (D3D9Client.rc after 286). resource.h `IDC_FX_SCENESHD 3054` (the integrator assigns 3054-3058 across this batch). VideoTab.cpp after 936 and 1033.
- Docs: VulkanClient.html Effects list (after 678) `<li>Scene shadows (<code>SceneShadows</code> 1)</li>`, one sentence on the 42 km reach and that 0 restores the legacy shadows. README.md:221-226 key list, NEW.tex, LAUNCHPAD.tex.

## 12. Cost (1080p, NVIDIA desktop, RTX 3060 class)
- Targets:
  - Map pass ≤ 1.0 ms average, ≤ 1.6 ms on C3 frames.
  - Receivers ≤ 0.5 ms (lit pixels pay 1 transform + 4 gathers; penumbra pixels 12-16 gathers).
  - CPU ≤ 0.3 ms (tile walk, culls).
  - 64 MB VRAM. 0 when inactive (orbit).
- Budget drivers:
  - Terrain C3, about 80 L11 tiles × 8k triangles every 4th frame; C2 about 225 tiles every 2nd frame.
  - Forest C2 impostors: up to 255k instances × 2 triangles at 130/ha within 2.5 km.
  - City building cells. Vessels are small.
- Measure:
  - Temporary vkCmdWriteTimestamp pair around SceneShadows::Render (as agent vshd did), mean over 300 frames, per scenario in section 16.
  - Receivers: frame interval with SceneShadows 1 vs a build where CsmVis returns 1, same scenarios.
  - Log nothing per frame in the final build.
- Levers if over budget: C1 tree meshes to 80 m; C3 every 8th frame; C3 LOD cut 176 m.

## 13. PBR/glTF/XR notes
- Casters: Mesh::RenderShadowMap opt 2 already handles PBR groups: alpha MASK/BLEND tested on the base texture, transmission > 0 skipped (Mesh.cpp:3045-3138). glTF vessels and XR vessels (ordinary vVessel meshes) cast with no change.
- Receivers: PbrMesh.fx via the shared include and CsmS (binding 29); PbrRender copies gCsm and the texture per variant (BeginVariant). Deferred glass (RenderPbrDeferred(false), Scene.cpp:2016) still sees the CSM (EndMain is after it) with the stable-hash noise. VC glass does not.
- Nested glass/transmission: unchanged; the glass itself casts no shadow (as the legacy stencil, Mesh.cpp:3187).

## 14. Legacy path (switch off)
- pShd null: the hooks are `if (pShd)`; Surfmgr2/VPlanet/VBase/BldgMgr/TreeMgr/VVessel test `Config->SceneShadows` or `scn->ShadowsCover()` (false without pShd).
- No `_CSM`/`_CSMCAST` in any macro list. SceneShadow.glsl is all inside #if, the CsmS sampler_state and gCsm block sit inside #if, CsmS is the last sampler. Every compiled pass is byte-identical. The VkEffect reflection table gains unused entries only.
- No atlas, no extra pass, legacy maps, stencil shadows and horizon shadows exactly as today.

## 15. Risks and open questions
1. Depth-only rendering (NULL colour) is a new framework path; validation must stay clean. Fallback in 3.
2. Terrain casters come from last frame's tile tree: a tile refined this frame shows in the CSM a frame late (invisible at these texel sizes). Edge cracks of out-of-view tiles (no MatchEdges) can leak sub-texel light.
3. CPU terrainSun × CSM in ridge penumbrae (10). A per-object `gCsmObj` with min() would need the sun-colour folds in VVessel/VBase/BldgMgr undone in every pass. Left for a later step if visible.
4. Tree impostor casters vs camera-facing impostor receivers: the 0.5 h bias hides self-shadow but also drops shadows from trees within half a height toward the sun.
5. Forest and city caster counts are scene-dependent; the levers in 12 may be needed.
6. Sun near the planet's pole axis (never on Earth, possible on other bodies): basis fallback with hysteresis, one re-snap when it switches.
7. ElevOverlay dev tool (FlowVS.bElevOvrl) moves vertices only in TerrainVS; with the overlay on, casters and receivers disagree. Dev-only, accepted.
8. Mesh-based moons (no surfmgr2): CSM inactive.
9. Default: proposed 1, as the other batch features. The owner may ship 0 until the checks pass.
10. Note for the TAA owner (not this feature): Surfmgr2.cpp:1973 records depthTiles only when SSAO or VolClouds is on, so with TAA alone the terrain is missing from tDepth and is reprojected through the planet sphere.

## 16. Checks (scenarios, pass/fail)
- **Off, legacy:**
  - SceneShadows 0 is pixel-identical to dd4e087c in KSC DG, Alps High, Carpathians Under, Orbit Alps, Gfx Forest.
  - glslangValidator SPIR-V of TerrainPS/TerrainVS (Earth, Mars, Moon flags), Tree, every D3D9Client.fx and PbrMesh.fx pass is byte-identical without the macro.
- **KSC, DG parked, sun 30 deg:**
  - DG shadow on base tile and terrain, one shadow only (luminance ratio in shadow vs legacy stencil-off reference within 2 %).
  - VAB shadow on the pad and on the DG. No stencil blob.
  - ShadowMapMode 0/1/3 × TerrainShadowing 0/1/2 all run, with no double darkening where legacy map and CSM overlap.
- **Streamed city at 500 m, sun 20 deg:**
  - Building shadows on streets and other buildings.
  - Flat lit roofs: frame-to-frame p99 change ≤ 2/255 (TAA off), no acne bands.
- **Forest close-up and at 1 km:**
  - Tree shadows on ground and neighbours, crowns not self-darkened in stripes.
  - TAA on: static camera frame-to-frame p99 ≤ 4/255 (the TAA residual).
  - TAA off: two static frames identical.
- **Shimmer:**
  - Scripted camera rotation only: atlas tiles C0-C3 identical before and after (temporary readback hash).
  - Slow dolly 1 m/s: no edge crawl visible at 1:1. Debug log: whole-texel snap steps only.
- **Alps High at sunset:** ridge shadows continuous across 30-42 km (radial luminance profile with no step > 3/255), CSM and horizon edges coincide within one C3 texel.
- **Moon:** lander shadow on regolith, CSM on, no atmosphere path (NewPlanet.glsl:787).
- **Carpathians Under:** 2D and volume cloud shadows look as before outside casters; inside a building shadow the product, no brightening.
- **Coast 300 m with a vessel:** glint removed in its shadow.
- **VC in LEO and at KSC:** interior unchanged, exterior shadowed. LEO: SceneShadows inactive (log once per state change), cost ~0.
- **Custom camera MFD and env maps:** no CSM there, stencil shadows present, no artifacts.
- **Time warp ×100 at sunset:** shadows sweep; no pops at C2/C3 refreshes.
- **PBR bench, glTF DG, nested glass:** shadows on glTF materials; masked foliage casts holes; glass casts none.
- **General:** no validation-layer errors; client log free of new [ERROR]; ctest passes; cost per section 12 in KSC, forest, city, Alps High, LEO.

## 17. Pure maths and tests (CsmMath, Tests/CsmMath.Test.cpp)
- Basis: orthonormal, right-handed, z = -L_p; continuous as L_p moves; fallback switch with hysteresis only near the pole.
- Snap: |q - m| ≤ t/2 per axis. A static ground point keeps its texel index under camera moves of < 1 texel, and shifts by whole texels otherwise.
- Stability:
  - Heliocentric planet at 30 km/s, spinning, tilted; camera at 2 km flying 250 m/s and turning.
  - A static ground point maps to the same atlas uv within 1e-3 texel over 120 frames, with the matrices rounded to float as the GPU gets them.
  - Camera rotation alone leaves every matrix unchanged.
- Reuse: a tile drawn at frame k, read at frame k+3 with the rebuilt matrix, gives the static point the uv of frame k (within 1e-3 texel).
- Ranges: C0's 20.16 km depth range resolves 5 mm at z near 1. Box margins: 0.94 r + 0.05 r + kernel ≤ r for all cascades.
- Cull: spheres inside, outside, straddling each face; behind-the-receivers rejected; toward-sun within 20 km accepted.
- Schedule: periods 1, 1, 2, 4; never more than 3 tiles a frame; forced refresh on shift > 0.05 r, sun turn > 1/2048 rad, reset.
- Penumbra and filter radius monotonic in blocker distance; the hand-over to i+1 triggers exactly above 4 texels.
- Bias/normal offset bounded for θ up to 89.9 deg. Fade weight 1 below 30 km, 0 from 42 km.
- Disk(8): best-candidate points in the unit disk, minimum distance ≥ 0.45, fixed seed; SceneShadow.glsl's table is its output.

## 18. Files the implementation touches (for the ownership split)
- New: OVP/VulkanClient/SceneShadows.h, SceneShadows.cpp, CsmMath.h, CsmMath.cpp, shaders/SceneShadow.glsl, Tests/CsmMath.Test.cpp.
- Shared with other batch features (integrator coordinates):
  - Scene.h, Scene.cpp, D3D9Config.h/.cpp, VideoTab.cpp, resource.h, D3D9Client.rc, docs, CMakeLists.txt, Tests/CMakeLists.txt.
  - shaders/NewPlanet.glsl (TerrainPS, also height fog), D3D9Client.fx (sampler order: CsmS last but new ones after it), Vessel.fx / PBR.fx / Metalness.fx / PbrMesh.fx / Mesh.fx (receiver lines, also SSR, SSGI, fog), Tree.glsl (also fog), D3D9Effect.cpp (macros), VPlanetAtmo.cpp (flags).
- Mine alone: Surfmgr2.cpp (+ Tilemgr2.h), VPlanet.h/.cpp, VBase.h/.cpp, BldgMgr.h/.cpp, TreeMgr.h/.cpp, VVessel.cpp, PbrRender.h/.cpp, D3D9Client.cpp (pass labels).

## Rev 2 (implementation) — overrides the body above
Integration (ue5-integration.md):
- Pass id RENDERPASS_SCENESHADOW = 0x000A, FLOWMAP 0x000B (INT-1; replaces MUST 6). Labels done by the integrator.
- No binding-3 block (INT-2; replaces §7.1 and MUST 5/8): `CsmPrm gCsm;` under `#if defined(_CSM)` at the end of PlanetPSBlock (NewPlanet, binding 2), FxParams (D3D9Client.fx), PbrShared (PbrMesh.fx), TreeBlock (Tree.glsl, binding 0); SSR's gSsr follows it. Off: blocks byte-identical, no extra UBO.
- Samplers (INT-3): CsmS 28 (D3D9Client.fx, after Planet3S), 29 (PbrMesh.fx, after IridThickS; PbrMesh then has 30-31 left for SSR), NewPlanet tShadowMap 25 carries the atlas, Tree tCsm 4.
- Hooks (integrator): BeforeMain at Scene.cpp:1671 after RenderFlags/PHASE_ALL (MUST 4), AfterResolve 1969, BeforeCockpit 2032 (= EndMain). Scene.h unchanged: other files reach the module through static SceneShadows::Get()/Enabled().
- Files: owned per INT-8; smallest edits outside it, listed in ue5-shadows-changes.md: D3D9Util.cpp (Setup, MUST 2), VPlanetAtmo.cpp (`_CSM` flag lines).

MUST:
1. Basis frozen: B_ref from L_ref (planet frame); re-based when angle(L_p, L_ref) > 1/2048 rad, on a proxy change or re-activation; all four tiles forced that frame. vL, normal offset and bias use L_ref. Each tile keeps its own (B, m, z0) and its receiver matrix is rebuilt every frame from them with this frame's R_p and camera. Tests: spin 600 frames, fly 120 frames, reuse k+3.
2. ShaderClass::Setup takes the depth target when no colour target is set (one line). Viewport stays the whole 4096² atlas; the tile is folded into the caster clip matrix (x/2 + (i&1) − 1/2, y/2 + 1/2 − (i>>1)); SetScissor(tile) per cascade; Clear(depth 1) is clipped by it. No colour, no R32F fallback needed.
3. Terrain caster walk ignores tile->state: a node descends when all four children are TILE_VALID with a mesh and the children's grid spacing (πR/2^lvl/GridRes/2) is not below the cascade cut (0, 0, 8.8 m, 88 m), else draws itself; sphere cull per node with margin; matrices WorldMatrix() after SetRenderPrm(dmWorld) of this frame; under the loader mutex.
4. Hook after RenderFlags (done by the integrator); bases are casters whenever active and terrainSun > 0, no 0x20 test.
7. BaseTilePS call inside `#if defined(_CSM) && defined(STAGE_PS)`, using frg.CamW.

SHOULD:
1. Ownership per INT-8 (above).
2. Sampler order per INT-3.
3. Casters use D3D9Mesh::RenderShadowMap opt 1 (upstream NormalDepth: legacy 0x20 alpha test and PBR alpha modes by discard, PBR transmission skipped; its colour goes nowhere). No opt 3: Mesh.cpp and SSGI's opt 2 untouched. Terrain and tree casters have their own shaders (new CsmCast.glsl, empty PS), not SceneDepth.glsl.
4. TreeMgr::DrawCells writes gCsm on every call; on only in the main colour pass of the proxy.
5. Tree receiver normal = gCsm.vL (θ = 0).
6. Covers(c, ρ, alt): stencil shadow point c − L·alt/sin e inside the receiving cascade (d + ρ < 0.94 r_k), ρ ≥ 0.5 t_k, d < 30 km. BeforeMain sets bStencilShadow false on covered active vessels and back to true on those it turned off last frame; bases skip the stencil per covered structure (main pass).
7. Hand-over: R = w/(2t) in [4, 8] blends PCF_i(4) to PCF_{i+1}(max(w/(2t_{i+1}), 0.5)); R < 1.5 blends a single bilinear gather into the 8-tap disk. Tested continuous.
8. Receiver plane depth bias (Isidoro 2006) from the geometric normal, analytic (dFdx breaks where quad pixels pick different cascades): each gathered texel is compared with its own plane depth. Constant bias 0.25·t·kb + zb (+ terrain LOD bias 0.2/0.5/2/20 m per cascade for terrain receivers); raster slope bias 1.0. Normal offset 1.0·t·sinθ.
9. Stable hash on the world texel index: vO.xy = (m_x/t − 1024, m_y/t + 1024) mod 4096 per tile, exact in float.
10. D32 probe in D3D9Effect::D3D9TechInit before any compile; SceneShadows::Enabled() = Config->SceneShadows && probe, used for every macro and branch; atlas->img checked (Ok()), else receivers stay off.
11. Cut rule as in MUST 3.

Other decisions:
- CsmPrm (416 B): mat4 mC[4] (atlas u, v, depth); vec4 vC[4] (t, D, 0.94 r, 0.80 r); vec4 vO[4] (world texel offset xy, terrain bias z); vec4 vL (to sun, 2 tan ω); vec4 vF (30 km, 42 km, frame % 8 or −1, on).
- SceneShadow.glsl is included twice (first defines CsmPrm, second CsmVis after gCsm and the sampler); it has no `uniform`/`texture x;` text, so VkEffect's parser sees nothing in it. Blocker search = centre + 4 disk gathers (5); PCF = 8 gathers.
- TerrainShadowing 0 does not switch the cascades off (own switch); NIT 9: no c.vis test on building cells.
- Cost: a timestamp pair around the caster pass, five summary lines in the client log after activation, nothing per frame.
