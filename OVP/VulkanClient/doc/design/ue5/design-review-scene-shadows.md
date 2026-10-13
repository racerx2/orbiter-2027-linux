# Design review: scene shadows (SceneShadows)

Checked design-scene-shadows.md against Orbiter-linux-custom HEAD dd4e087c (clean tree) and against design-ssr.md, design-volumetric-plumes.md, design-ssgi.md and design-height-fog.md. Paths are under OVP/VulkanClient/ unless shown otherwise. Read-only: nothing was built or run.

Checked and correct, no change needed:
- Hooks: Scene.cpp:346-348, 381-383, 1599-1611, 1648, 1662, 1955 and 2016.
- VkDev:
  - a NULL colour target is accepted (VkCore.cpp:1047);
  - depth-only BeginRendering works (1107-1161);
  - EndRendering moves a sampled depth target to SHADER_READ_ONLY (1163-1172);
  - Clear is clipped to the viewport and scissor (1174-1205).
- A D32 VkTex gets a depth-aspect view (VkCore.cpp:257), and so does its VkSurf (480-491).
- Sampler numbering is by order (VkShader.cpp:566). D3D9Client.fx has 24 sampler_states and PbrMesh.fx has 25, so CsmS lands on 28 and 29.
- There is no 2D_ARRAY default texture (VkCore.cpp:1580-1590).
- Tiles out of view still refine by distance (Tilemgr2_imp.hpp:163-173).
- The receiver uv convention matches Common.glsl:386.
- The D32 ulp figures, the penumbra w = d·2tanω, and the 0.0093/0.0062 sun sizes are right.
- Binding 3 is free in every receiver today.
- The SPIR-V carries no line info (default SpvOptions, VkShader.cpp:197), so shader text inserted under #if leaves the compiled passes byte-identical.

## MUST

1. **The texel snapping does not hold: the light basis turns about the planet centre.**
   - §4.2 builds B from the live L_p every frame. L_p turns about the pole at the planet's spin rate ω (7.29e-5 rad/s on Earth). B·g_p therefore turns about the planet centre, not about the box.
   - Write s.xy = B(g_p − c_p) + (B·c_p − m). The first term is the "0.075 texel/s at the box edge" of §4.2.
   - The second term is the camera's sub-texel residual after each re-snap of m. It slides at |c_p|·ω·cos(lat) ≈ 410 m/s at KSC, which is 6.8 m per 60 Hz frame: 87 texels in C0, 11.6 in C1, 1.5 in C2, 0.16 in C3.
   - Every static edge is rasterised at a new phase every frame. C0-C2 shimmer fully, which is what §4.1 set out to prevent. §17's stability test (static point within 1e-3 texel over 120 frames, spinning planet) fails against these maths.
   - Fix: freeze the basis.
     - Keep B_ref, built from L_ref, for all cascades.
     - Re-base only when angle(L_p, L_ref) > ε_L. With ε_L = 1/2048 rad that is every 6.7 s at ×1, with a 5 cm direction error on a 100 m shadow.
     - Force-refresh all four tiles on the re-base frame.
     - Use L_ref for vL, the normal offset and the bias.
     - Add a test: planet spinning at ω, camera still, 600 frames; a static point keeps its uv except on re-base frames.

2. **ShaderClass::Setup cannot draw depth-only, and it resets the viewport.**
   - D3D9Util.cpp:1817-1824 takes the target size from GetRenderTarget(), which is rtColor (VkCore.h:205).
     - With a NULL colour it logs "ShaderClass::Setup No render target is set" and returns before BindShaders.
     - Otherwise it sets the viewport to the whole colour target.
   - Every caster path goes through it: terrain (as RenderDepth, Surfmgr2.cpp:1842), Mesh::RenderShadowMap (MeshShader derives from ShaderClass, Mesh.cpp:3066) and TreeMgr::DrawCells (TreeMgr.cpp:513).
     - The depth-only atlas of §3 therefore draws nothing.
     - The R32F fallback draws, but each Setup drops the per-cascade SetViewport, so all four cascades land on the whole 4096² atlas.
   - Fix:
     - (a) In Setup, add `if (!pTgt) pTgt = pDev->GetDepthStencil();` (VkCore.h:212). That branch only logs an error today, so the legacy image is unchanged.
     - (b) Keep the viewport on the whole atlas, as Setup leaves it. Fold the tile into the caster clip matrix: x' = x/2 + (i&1) − 1/2, y' = y/2 + 1/2 − (i>>1). The receiver uv stays as in §4.2.
     - (c) Clip each cascade with SetScissor(tile rect). ScissorArea uses rtDepth when there is no colour target (VkCore.cpp:1215-1219), and Clear honours the scissor (1199).

3. **The terrain caster walk reads tile states that the secondary passes wrote.**
   - With the defaults EnvMapMode 1, bIrradiance 1 and CustomCamMode 1 (D3D9Config.cpp:62, 98, 71), most frames start with an env, custom-camera or irradiance turn (Scene.cpp:1490-1582, dwTurn++ at 2416).
     - Each turn renders the planets (2781-2784), so Surfmgr2 Render runs ProcessNode from that turn's camera (Surfmgr2.cpp:1979-1980).
   - ProcessNode rewrites tile->state in every pass (Tilemgr2_imp.hpp:144, 158, 171, 245). Only MAINSCENE deletes children (157).
     - The env camera sits at a vessel with a 90° aperture, and apr scales with GetTanAp (204-205).
     - A custom camera can be up to 15 km away (Scene.cpp:1514).
   - So at 1648 the ForRender/Active leaf set is usually another camera's, one or more levels coarser, and it changes with the turn. Terrain casters change LOD from frame to frame, and C0/C1 relief shadows jump at about 20 Hz.
   - Fix: walk without tile->state. Descend while all four children are TILE_VALID and have a mesh, and the spacing cut (SHOULD 11) allows it.

4. **The hook runs before the main pass's RenderFlags, so base casters drop out.**
   - §8 gates bases on "render flag 0x20 as VPlanet.cpp:1336".
   - RenderFlags becomes 0xFFFFFFFF only at Scene.cpp:1655, after the hook at 1648. Until then it holds the last turn's flags (RenderSecondaryScene, 2742). With EnvMapMode 1 the env and irradiance turns pass 0x01 (1540, 1567).
   - With the defaults, bases cast in about one frame in three, so the VAB's shadow flickers on the pad.
   - Fix: call `pShd->Render()` after 1658 (after RenderFlags, PHASE_ALL and pbrDeferred.clear) and before the Push at 1662. This also pins PbrRender to PHASE_ALL, so VVessel.cpp:740 can never skip caster meshes.

5. **NewPlanet binding 3 is claimed twice.**
   - SSR puts `SsrBlock { SsrPrm gSsr; }` at binding 3 in Earth TerrainPS (design-ssr §4, "after 181"). §7.1 here puts CsmBlock at binding 3 as well, and Earth gets both `_CSM` and `_SSR`.
   - NUBOS is 4 (VkCore.h:302), and 0-2 are taken (NewPlanet.glsl:125-126, Scatter.glsl:101).
   - Fix (integrator): one batch block at binding 3, `uniform BatchBlock { CsmPrm gCsm; SsrPrm gSsr; }`.
     - Declare it whenever `_CSM || _SSR` is set, with both structs always included, so the offsets do not depend on which macros are on.
     - Each owner sets its own member by handle (D3D9Util.cpp:1902-1913). Zero data means off.
     - Tree.glsl keeps a CSM-only block.

6. **RENDERPASS 0x000A is taken twice.**
   - Plumes defines `RENDERPASS_FLOWMAP 0x000A` after Scene.h:57 and renders vessels in it through vVessel::Render (VVessel.cpp:683, 794).
   - With both passes on the same value, the else-if dispatch at 794-799 sends one pass down the other's branch.
   - Fix:
     - FLOWMAP keeps 0x000A. It goes through gc->PushRenderTarget, so it needs label index 10.
     - SCENESHADOW becomes 0x000B.
     - Both label arrays (D3D9Client.cpp:1207, 1241) gain "FLOWMAP" and "SCENESHADOW".

7. **BaseTilePS is compiled in the vertex stage.**
   - Mesh.fx:207-227 is not inside `#ifdef STAGE_PS`, unlike AdvancedPS (Vessel.fx:61), PBR_PS and FAST_PS (PBR.fx:67, 445) and MetalnessPS (Metalness.fx:139).
   - Every pass compiles the whole text for both stages (VkShader.cpp:639-640, STAGE_* at 169), and CsmVis exists only under `_CSM && STAGE_PS`.
   - The §7.2 Mesh.fx line therefore breaks every vertex-shader compile of D3D9Client.fx when _CSM is on. All vessel and base rendering is lost.
   - Fix: wrap that call in `#ifdef STAGE_PS`, as SSR does. The member there is `frg.CamW` (Mesh.fx:181, 211), not camW.

8. **With SceneShadows 0, every FX and PBR draw still pushes an extra UBO.**
   - `#if defined(_CSM) || defined(VKFX_REFLECT)` puts the block into the reflection compile. VkShader.cpp:598-615 adds " gCsm;" to the stub whatever the #if says, and 621-625 compile the stub with VKFX_REFLECT.
   - The table then gets block 3 (BuildTable, 124-128). SetTable sizes data[3] to 352 bytes (236-242), and Push allocates, copies and pushes it on every draw (302-315).
   - This hits D3D9Client.fx and every PbrMesh variant. That is a per-draw buffer with the switch off, against §0 and §14 ("the reflection table gains unused entries only").
   - Fix: in the two .fx files, append `CsmPrm gCsm;` under `#if defined(_CSM)` at the end of FxParams (D3D9Client.fx:153, after gExposure) and of PbrShared (PbrMesh.fx:109), as SSR does with gSsr.
     - Put CsmPrm in a small CsmPrm.glsl included before line 99 / line 82.
     - With the switch off the block sizes are unchanged. With it on, the handle "gCsm" and the SharedNames copy still work.
     - Keep a binding-3 block only in the ShaderClass files (NewPlanet, Tree), which compile with the real macros.

## SHOULD

1. **§18's ownership list is wrong.**
   - Surfmgr2.cpp and Tilemgr2.h: SSR (1956, 1973), SSGI (RenderDepth 1835-1855), fog (ElevGrids next to HorizonAt, Tilemgr2.h:533-539).
   - VVessel.cpp: SSR (scope at 716) and plumes (683, the FLOWMAP branch at 794, RenderExhaust, HullBox).
   - VBase.cpp: SSR (381, 412). PbrRender.h/.cpp: SSR (BeginVariant 374-406). VPlanet.h: SSR (PlanetShader::bSsr). TreeMgr.cpp:110: SSGI (`_SSGI`).
   - Mesh.cpp RenderShadowMap opt 2 and NewMesh.glsl SceneDepth_PS are edited by SSGI. The design lists them as unchanged and relies on them.
   - Shaders:
     - NewPlanet.glsl is shared with SSR; fog does not touch it.
     - Tree.glsl is shared with SSGI, not fog.
     - The mesh .fx files are shared with SSR only; SSGI and fog leave them alone.
   - Fix: correct the list and add a merge-order note for VVessel.cpp:683/794-799 and Scene.cpp:1954-1956.

2. **The sampler order clashes with SSR.**
   - Both designs claim D3D9Client.fx binding 28 (CsmS vs tSsrCol) and PbrMesh binding 29. The numbers come from declaration order (VkShader.cpp:566).
   - Fix: CsmS first, then tSsrCol and tSsrHiZ.
     - D3D9Client.fx: 28, 29, 30.
     - PbrMesh.fx: 29, 30, 31. PbrMesh is then full (MAXBINDINGS 32, VkCore.h:303); say so in the design.
     - C++ uses handles only, never binding numbers.

3. **The casters inherit SSGI's opt 2.**
   - SSGI adds MRT albedo/normal/class outputs to SceneDepth_PS, and per-group PSGi constants to RenderShadowMap opt 2 when hPSG is set.
   - In up to three tiles a frame, the CSM would pay for:
     - albedo fetches;
     - three-target writes with no attachments;
     - per-group constant pushes;
     - and it would read SceneFx's per-pass class globals.
   - Fix: add opt 3 (caster).
     - It keeps opt 2's group filter (Mesh.cpp:3096-3121), with an alpha-test-only PS: SceneDepth_PS built without `_SSGI`, or its own SHADER_CSM_CASTER.
     - The VVessel dispatch passes 3, as do the VBase and BldgMgr casters.
     - Agree it with the SSGI owner.

4. **Trees in custom cameras read the main pass's gCsm.**
   - TreeMgr::Render also draws in CUSTOMCAM (TreeMgr.cpp:583), through the same Trees ShaderClass. Its constants persist between draws (D3D9Util.cpp:1711-1713, 1912).
   - §9 sets gCsm only in the main mode, and EndMain resets only the FX. MFD camera views therefore get on = 1 with the main camera's matrices.
   - Fix: DrawCells writes gCsm on every call, with on = MAINSCENE && depth == 0 && pShd->Active().

5. **The tree receiver normal is in the cell frame.**
   - §7.2 passes L = Prm.vSun.xyz (Tree.glsl:124) as nW. That vector is in the cell frame (TreeMgr.cpp:541-542).
   - CsmVis compares nW with gCsm.vL in camera-relative world axes, so θ, the normal offset and the bias all come out wrong.
   - Fix: pass gCsm.vL.xyz. n = L then gives θ = 0, which is the intent.

6. **Covers() drops stencil shadows that the CSM does not replace.**
   - Receivers beyond 0.94·r₂ (4.2 km) read C3. C3 skips casters with ρ < 0.5·t₃ = 22 m (§5), yet Covers() is true out to d + ρ < 36 km.
     - A DG (ρ ≈ 18 m) parked 5-36 km away loses its stencil shadow (VVessel.cpp:1056) and gets no CSM shadow either.
   - Covers() also tests the caster's centre. The stencil shadow can land up to alt/tan e away, for alt up to 100·size (VVessel.cpp:1072).
   - Fix: Covers is true only when all of these hold:
     - the projected shadow point lies inside the cascade k that receives it (distance + ρ < 0.94·r_k);
     - that cascade draws the caster (ρ ≥ 0.5·t_k);
     - d < 30 km.
   - Also set bStencilShadow = !Covers for every active vessel, not only to false. RenderVesselShadows also visits invisible vessels (Scene.cpp:3138-3141), and line 1609 resets only visible ones.

7. **The penumbra width jumps at the cascade hand-over.**
   - At w/(2t_i) = 4 the lookup moves to C(i+1), and R is clamped to at least 1 texel there.
   - The kernel radius goes from 4·t_i to 7.5·t_i (C0→C1, C1→C2), or to 10·t₂ (C2→C3). That is a 1.9-2.5× step along every long shadow edge.
   - Fix: blend over w/(2t_i) ∈ [4, 8]:
     - mix(PCF_i(R = 4), PCF_{i+1}(R = max(w/(2t_{i+1}), 0.5)), smoothstep(4, 8, ·));
     - R < 1 means a single bilinear gather.
     - Add a §17 test that the filter radius is continuous across the hand-over.

8. **The constant bias and the 4-texel blocker disk make sloped receivers shadow themselves.**
   - The bias is t(1 + min(tanθ, 4)) plus a normal offset of 1.5·t·sinθ. A search tap r texels out sees the receiver's own surface r·t·tanθ nearer the light.
   - With r = 4, any receiver with θ > 22° (tanθ > 0.4) finds blockers. That covers most terrain, roofs and hulls.
   - Consequences:
     - §12's "1 transform + 4 gathers" fast path is rarely taken, and receivers pay about 13 gathers.
     - For θ > 76° the capped bias falls below the per-texel slope error, so acne bands appear under a grazing sun (the §16 roof check).
   - Fix: receiver-plane depth bias (Isidoro, "Shadow mapping: GPU-based tips and techniques", GDC 2006).
     - Take dFdx/dFdy of the shadow coordinate and compare each tap against z + dot(offset, ∇z).
     - Keep a small constant bias.
     - Re-cost the receivers in §12.

9. **The "stable" hash moves with the snap.**
   - §6 uses φ = hash(floor(uv·4096)). A static point's uv shifts by one texel whenever m steps (§4.2).
   - With TAA off, every C0 pixel's rotation re-randomises each time the camera moves 7.8 cm. The 1 m/s dolly check in §16 would see the penumbra pattern pop about every 5 frames.
   - Fix: hash the world texel index, floor(uv·4096) + (m_i/t_i mod 2^16). Pass that integer offset per cascade in vC; it is exact in float.

10. **The failure check comes too late.**
    - Effects, planet shaders and meshes compile with the config macros at D3D9Client.cpp:658, 672 and 677, before `new Scene` at 680.
    - If the format check or the atlas allocation fails in the SceneShadows ctor, the `_CSM` shaders are already live. The Config-keyed branch still skips the legacy terrain projection (Surfmgr2.cpp:1180-1205), so the focus vessel's shadow on the terrain is lost while the CSM is off.
    - Fix:
      - Run the FormatHas test next to bShadowMap (D3D9Frame.cpp:322-326) and clear Config->SceneShadows there, before any shader compiles.
      - Check atlas->img after creating it; VkCore.cpp:244-249 leaves it null on failure.
      - Key every runtime branch on pShd && pShd->Ok().

11. **The terrain cut rule in §5 is inverted.**
    - §5 says a node "draws its own mesh when it is a leaf or its grid spacing ≥ minSpacing; else it descends". The root (spacing 625 km ≥ 88 m) would draw itself and stop.
    - Fix: a node draws itself when it is a leaf, or when its children's spacing (half its own) is below minSpacing.
    - GridRes is 1 << (4 + MeshRes), which is 32 by default (VPlanet.cpp:413, D3D9Config.cpp:75).
      - C3's 88 m cut lands on L12 (153 m spacing, 2k triangles per tile, about 4× the tile count of §12).
      - C2's 8.8 m cut lands on L16 (9.5 m).
      - Restate §12's "80 L11 tiles × 8k triangles", which assumes GridRes 64.

## NIT

1. Y = L_p × X makes (X, Y, −L_p) left-handed (X × Y = L_p), but §17 asks for right-handed. Use Y = X × L_p.
2. The design draws through dev->SetRenderTarget, not gc->PushRenderTarget, so the label at D3D9Client.cpp:1207/1241 only keeps the arrays aligned. Never call gc->PushRenderTarget(NULL, atlas): a NULL colour there keeps the current target (1221).
3. §17's "5 mm at z near 1" contradicts §4.2's 1.2 mm (2⁻²⁴ × 20.16 km = 1.2 mm).
4. §5's cull mixes the translated s.z with the untranslated z0 and q.z. Write it as −ρ ≤ s.z ≤ 2r_i + 20 km + ρ.
5. §6 says the blocker search uses 4 gathers on an 8-point disk, plus a centre gather. State which taps are used, and count 5 in §12.
6. "Never more than 3 tiles a frame" (§4.4, §17) does not hold on forced-refresh frames (re-base, sun turn). Budget the 4-tile frame.
7. Under `_CSM` without `_WATERFX`, the no-horizon-map glint at NewPlanet.glsl:881 also needs × fShadow, not only line 880.
8. Bind the atlas after pShader->ClearTextures() (Surfmgr2.cpp:1898).
9. BldgMgr refreshes c.vis only for cells in the current pass's frustum (BldgMgr.cpp:521-523). The "c.vis ≤ 0 skipped" test therefore uses stale values for the cells behind the camera, which are the ones toward the sun. Drop the test.
10. Tests/CMakeLists.txt: the entry goes after line 225 (LookMath's include line), not 224.
11. Shadows, SSR, plumes and SSGI all put their checkbox at (290, 428). The integrator needs a third Effects row; fog uses (380, 416).
12. §16 tests TerrainShadowing 0/1/2. Say whether the CSM honours TerrainShadowing 0, which today turns off all ground shadows (VVessel.cpp:1057, VBase.cpp:487).

Counts: MUST 8, SHOULD 11, NIT 12.
