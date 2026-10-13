# Code review: scene shadows (SceneShadows, UE5 batch)

Repo Orbiter-linux-custom, branch ue5-batch: 35e2949c plus the owner's uncommitted files (INT-8 list, `git diff 35e2949c`, new files whole: SceneShadows.h/.cpp, CsmMath.h/.cpp, SceneShadow.glsl, CsmCast.glsl, CsmMath.Test.cpp) and the owner's notes ue5-shadows-changes.md. Paths are under OVP/VulkanClient/ unless shown otherwise. Read-only review: nothing was built or run.

**Verified.**
- **Maths.** Checked by hand:
  - Basis rows (X, Y, −L) with Y = X × L are right-handed (X × Y = −L). Pole fallback with hysteresis (0.05 / 0.1).
  - s = B Rpᵀ v + (q − (m, z0)) for a camera-relative v. Every tile keeps its own (B, box, q). Its Xf is rebuilt every frame from this frame's Rp and cp, so a kept tile stays exact for static ground (Rev 2 MUST 1). Re-base happens past 1/2048 rad, on a proxy change and on re-activation, with all tiles forced.
  - CsmMath Fill is the row-vector (D3D) layout. The caster fold x/2 + (i&1) − ½, y/2 + ½ − (i>>1) works with the framework's negative-height viewport, the tile scissor rows and the receiver uv (½ − y/2), and the test checks it. Depth is s.z / (2r + Zext).
  - Receiver-plane gradient. M[0].xyz and M[1].xyz are ∇u and ∇v (columns, row_major block), so g = (n′x k, −n′y k) / (D cosθ). That equals the analytic plane Δz = (n′x Δx + n′y Δy) / cosθ.
  - Gathers. The textureGather offsets (x −h,+h; y +h,+h; z +h,−h; w −h,−h), the bilinear mix and the tile clamp (both gathered texels stay inside) are right.
  - Stable hash. x adds and y subtracts: vO.y − row = y/t, fixed on the ground.
  - Trees. A tree receiver with n = vL gets g = 0 and θ = 0.
  - Coverage. The margin 0.94r + 0.05r + 5.5t ≤ r holds. Receivers within r of the camera fall inside [0, 2r + Zext]. Cull and CastsIn match design review S6 and NIT 4.
- **Vulkan.**
  - SetRenderTarget(NULL, atlas) gives a depth-only BeginRendering with LOAD. ShaderClass::Setup takes the depth target (the D3D9Util line) and sets the whole-atlas viewport. Each tile has its own scissor, and Clear is clipped to viewport ∩ scissor. EndRendering moves the atlas to SHADER_READ_ONLY, and PrepareSample covers a never-drawn atlas.
  - Depth func, slope bias, cull and decl are restored through GetState/SetState, and the pass id goes through PassStack.
  - No FX draw happens in the caster pass, so there is no feedback on CsmS.
  - The query pool is reset outside rendering and read four frames later without wait.
- **Blocks and samplers (INT-2/3).** gCsm is appended under `_CSM` at the end of PlanetPSBlock, FxParams, PbrShared and TreeBlock, before gSsr. It is 416 B on both sides, and row_major/scalar reach the struct's matrices. Sampler bindings:
  - CsmS: 28 in D3D9Client.fx (right after Planet3S), 29 in PbrMesh.fx.
  - tShadowMap 25 carries the atlas in NewPlanet.
  - tCsm is 4 in Tree.glsl (NUBOS 4).
  - VkEffect's textual parser lists gCsmMap/CsmS even when off, but per-pass samplers come from the compiled pass, so nothing changes.
- **Off path.** pShadows is not created, Get() is null and Enabled() is false. So there is no `_CSM` in BaseMacros, the planet flags or the tree flags; every new shader line is under `#if`; Probe only queries the format; the Surfmgr2/TreeMgr/VBase/PbrRender/VVessel branches are inert. The deliberate exceptions, which the user asked for: the Mesh.fx ShadowMeshTechExVS pull with the P1 depth test, and the VBase buried flag.
- **Receivers.**
  - Terrain: -camW, kt 1, min with fH; the specular uses fShadow, which closes design review NIT 7.
  - Base tiles: inside STAGE_PS, on frg.CamW.
  - Vessel, PBR_PS, FAST_PS and Metalness: min with the legacy term exactly as the legacy line; kb 3 under gShadowsEnabled.
  - PbrMesh: Ng, already flipped for double-sided.
  - Trees: posW and vL, zb 0.5 h; the distance pass returns before the lookup.
  - Buildings: through the FX.
  - Receivers are on only between BeforeMain and BeforeCockpit, on the proxy, in MAINSCENE. The FX block is "off" in custom cameras and env maps.
- **Casters.** Each caster set was checked against its main-pass counterpart:
  - The terrain walk uses TILE_VALID children with a mesh (no tile->state), the cut 0.5·span/2^lvl ≥ cut, this frame's WorldMatrix after SetRenderPrm(dmWorld) (the same dwmat as the main Render), and the loader mutex.
  - Bases: active, terrainSun > 0, not buried.
  - Buildings: no c.vis test.
  - Trees: mesh and card thresholds are complementary per tree. The card geometry projects the trunk as s·cos e plus the crown.
  - Vessels: all active ones, exterior meshes in the cockpit view, opt 1.
- **Lifetime.** VkTex's dtor calls ForgetTexture, so the FX, PbrMesh and ShaderClass buffers drop the atlas. The query pool goes through Defer, and inst is cleared.
- **Buried fix.** The pull moves each projected vertex along its view ray, so its screen position is kept. The pulled points lie on the plane raised 2 m, so triangles stay planar. The P1 after-state (test off, func unchanged) equals the legacy one.
- **Tests and rules.**
  - The 19 cases cover the review's asks (spin 600 frames, k+3 reuse, float matrices, hand-over continuity, stable index).
  - Comments are one line each, citations are in the headers, and I found no copied GPL or vendor code (the IGN constants are Jimenez's published ones, the LCG constants Knuth's).
- **Delta at `<!-- DONE -->` (17:19).** Reviewed. Since my first pass only VBase.cpp changed: the buried flag, the RenderGroundShadow state reset, and the SHD_TMP_NOBURY getenv removed. Mesh.fx P1 and every other owner file match what is reviewed above (md5 checked).
- **Not verified:** runtime. The notes say SceneShadows 1 has not been on screen in this tree, and the 17:18 reboot took the run scripts. There is no validation-layer output and no cost number.

## MUST

1. **VVessel.cpp:1068, with Scene.cpp:1757/1788 and Surfmgr2.cpp:1186: the focus vessel and its shadow-map cluster lose their terrain shadow wherever no cascade that draws them receives it.**
   - **Problem:** The chain of events, with the defaults ShadowMapMode 1 and TerrainShadowing 2 (D3D9Config.cpp:66, 86):
     - The legacy focus code runs after BeforeMain and sets bStencilShadow = false on vFocus and its cluster, because the legacy map is projected on the terrain.
     - While the cascades receive, Surfmgr2.cpp:1186 skips that projection.
     - The CSM then shows the shadow only where it lands in a cascade with CastsIn (ρ ≥ 0.5 t). For a DG (ρ ≈ 18 m) that means within 4.23 km of the camera; past the 30-42 km fade, nowhere.
   - **Example:** a DG flying 2 km AGL with the sun at 20°, seen from the chase camera.
     - The shadow falls about 5.8 km away and is read from C3, where the DG is not drawn.
     - SceneShadows 0 shows it: the legacy projection shades the whole light cylinder behind the vessel (pd clamped at 0, NewPlanet.glsl:227).
     - SceneShadows 1 shows nothing, neither stencil nor map. The same happens from the cockpit in any low flight with a long shadow.
   - **Fix** (owner, the VVessel.cpp stencil part):
     - While the cascades receive on the proxy in MAINSCENE, RenderGroundShadow should decide by coverage alone. Return only when the vessel is covered; otherwise draw the stencil, even though the legacy focus code cleared bStencilShadow.
     - For example, add `bool SceneShadows::Covered(const vObject *v) const { return recv && stencilOff.count(v); }`. Then `if (pass == MAINSCENE) { SceneShadows *s = SceneShadows::Get(); if (s && s->ReceiversOn(scn->GetCameraProxyVisual())) { if (s->Covered(this)) return; } else if (!bStencilShadow) return; }`.
     - This gives one shadow in both cases: CSM when covered, stencil when not, with the legacy projection off.
     - Check: Shd Chase Low with the vessel at 2 km and a low sun, SceneShadows 0 vs 1.

## SHOULD

1. **SceneShadow.glsl:115: the hand-over PCF radius in cascade i+1 has no upper bound.**
   - **Problem:** `CsmPcf(tC, b, max(w / (2.0 * b.t), 0.5), rot)` scales the 8-tap disk by any R. Two examples on a C0 receiver:
     - A ridge 5 km toward the sun: w = 46 m, R₁ ≈ 39 C1 texels.
     - A caster 20 km out: w = 186 m, R₁ ≈ 159 texels.
   - Either way, 8 point-like taps sample a 23-93 m disk. Without TAA, the stable hash leaves a fixed grain over the whole penumbra; with TAA, 8 frames of 8 taps still show it.
   - The design's intent (§6) was "a wide physical penumbra without a wide kernel".
   - **Fix:** clamp R to 4 in i+1 as well, and continue the hand-over while w/(2t) > 4 and the cascade is below 3 (i+2, i+3, each blended over 4-8). A coarser cascade's bilinear taps are pre-filtered. Extend the EffectiveRadius test across two hand-overs.

2. **SceneShadow.glsl:47: the receiver plane is capped at cosθ = 0.2.**
   - **Problem:** Beyond 78.5° the gradient under-predicts the slope. Take flat ground with the sun at 5°:
     - True tanθ is 11.4 against the capped 4.9.
     - A texel one t along the light is therefore 6.5 t nearer than predicted: 0.51 m in C0, 3.8 m in C1.
     - The constant bias is only 0.25 t, plus the terrain bias of 0.2 / 0.5 m.
   - Lit flat terrain, base tiles and roofs then shadow themselves in C0-C2 at sunset. The direct term is ≤ 20 % there, so this shows as speckled darkening.
   - **Fix:** when capped, add the missing part as bias: t·(tanθ − tan θcap)·(1 + 0.5) in depth units. Or lower the floor to 0.05 and fade CsmVis toward 1 between cosθ 0.1 and 0.
   - Check one sunset shot (sun about 5°) with TAA off.

3. **Notes, Tests: the combined builds and runtime are not checked.**
   - **Problem:** Gaps in the checks so far:
     - NewPlanet was compiled with `_CSM` but not with Earth's default `_WATERFX _SSR _CSM`.
     - PbrMesh.fx was compiled with `_CSM` but not with `_CSM _SSR` (bindings 29-31, the block holds both).
     - There is no INT-2 reflection dump (gCsm 416 B, gSsr after it).
     - SceneShadows 1 has not been run in this tree: no "[ERROR]" check, no validation output, no timing lines.
   - SceneShadows and SSR both default to 1, so a failure there breaks Earth terrain or every glTF draw.
   - **Fix:** run those two compiles and the reflection dump. Then make one SceneShadows 1 run (Shd KSC Low, shots plus the client log's timing lines) and shdF1 before `<!-- FIXED -->`, and put the numbers in the notes.

4. **VBase.cpp:354-358: UpdateBuried runs at every light check.**
   - **Problem:** That is every 0.1 s, and every frame while paused (`|| oapiGetPause()`). Each run makes 5 elevation lookups per structure_as on every active base, with oapiSurfaceElevation as the fallback before the tiles load.
   - An add-on base with a few hundred separate meshes pays thousands of lookups per check. This applies with SceneShadows 0 too.
   - **Fix:** run it every 1-2 s and not again while paused once it has run, or only when the planet's loaded tile set changed.

5. **Mesh.fx:332 and 491, the buried fix: partly sunk structures lose their whole stencil shadow.**
   - **Problem:** With the P1 depth test, the stencil shadow is hidden wherever the terrain is more than 2 m above the plane.
     - Example: a merged-mesh object at base level where the streamed terrain is 4 m higher. Its top 16 m stand visible, but its shadow (on the plane, 4 m under the terrain) is gone.
     - This applies everywhere with SceneShadows 0, and with SceneShadows 1 beyond what the cascades draw.
   - **Fix:** check it in the planned shdV0 run, and look at objects on raised ground. If it shows, raise the tolerance (pull 6-10 m) or say in the notes that it is the accepted cost of the fix.

## NIT

1. **SceneShadows.cpp:175-176:** SetScissor is restored before SetRenderTarget(c0, d0), which resets the scissor and viewport (VkCore.cpp:1053-1056). Swap the two lines. There is no visible effect today, because PushRenderTarget follows.
2. **SceneShadows.cpp:152-156:** the fill mode is not set. Debug wireframe (Scene.cpp:1679) leaves VK_POLYGON_MODE_LINE for the next frame's casters, so the shadows turn into lines. Set FILL in Draw and restore it after.
3. **VVessel.cpp:807:** the comment says "glass left out", but opt 1 casts legacy translucent groups that have no 0x20 flag; only PBR transmission is skipped. Fix the comment, or note in the design that canopies now cast, as in the legacy focus map.
4. **TreeMgr.cpp:516-519 and Surfmgr2.cpp:1963-1966:** these are keyed on Get() alone. With the probe failed, Get() is non-null and every draw looks up a "gCsm" that does not exist. Key them on `ss->Ok()`.
5. **PbrRender.cpp:397:** `GetParameterByName(0, "CsmS")` runs on every BeginVariant. Cache it in PbrVariant, as hShadowS is.
6. **SceneShadow.glsl:6-8:** the header is three comment lines. Fold it into one or two to keep the one-line-comment rule.
7. **SceneShadows.h:** W and H are stored but never used.
8. **Notes:** list the D3D9Client.fx/PbrMesh.fx off-path SPIR-V comparison (all passes but ShadowTech P1's VS). The integrator should record the user-approved legacy change (Mesh.fx P1, VBase buried) in the commit message, because it is a stated exception to "switch-off gives the old path".
9. **Cost.** The design review allowed for a 4-tile frame (its NIT 6) but not for these:
   - C0-C2 are cleared and walked every frame even when Camera.elev ≥ 0.94 r_i, where no receiver can use them. Skip those tiles.
   - Under time warp the re-base forces all four tiles: every frame at ×1000, about every 4 frames at ×100. Put a number on that cost in the notes.
10. **VBase.cpp:574:** Covers(c, ρ, 0.0) ignores the structure's shadow length. At a low sun, a small structure near 4.2 km loses the part of its shadow that falls in C3, which does not draw it. Pass half the bounding height as alt, as the vessels do.

Counts: MUST 1, SHOULD 5, NIT 10.

