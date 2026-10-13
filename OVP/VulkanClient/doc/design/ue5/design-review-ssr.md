# Design review: screen-space reflections (SSR)

Checked design-ssr.md against /home/racerx/Projects/Orbiter-linux-custom (branch visual-fx, HEAD dd4e087c, clean). Paths are under OVP/VulkanClient/ unless shown otherwise. I also cross-read design-scene-shadows.md, design-ssgi.md, design-height-fog.md and design-volumetric-plumes.md. Read-only: nothing was built or run.

Most code claims are correct:
- the hooks at Scene.cpp:1595-1596, 1954-1962, 2016 and 3419;
- the SceneFx.cpp:37/273 gates and the Surfmgr2.cpp:1956/1973 lines;
- the sampler numbering in VkShader.cpp:547-596. Only D3D9Client.fx has sampler_states, 24 of them, so 4-27;
- PbrMesh.fx 4-28;
- the IsAttachment rule in VkShader.cpp:321;
- PrepareSample in VkCore.cpp:1568-1574 and StretchRect between levels;
- the jitter sign in Scene.cpp:3518-3521;
- the curve and decode chains in PbrMesh.fx:261-283 and 538-542, Metalness.fx:386-397, PBR.fx:375 and 533, and Mesh.fx:225;
- the Earth flags in VPlanetAtmo.cpp:159-175, which have no `_MICROTEX`.

The off build should give byte-identical SPIR-V: glslang runs without debug info (VkShader.cpp:181-197), so the extra lines add no OpLine.

## MUST

1. **NewPlanet.glsl UBO binding 3 is claimed twice.**
   - §4 puts `SsrBlock` at binding 3 in TerrainPS.
   - design-scene-shadows §7.1 puts `CsmBlock` at binding 3 in the same shader, through SceneShadow.glsl, which is included after Scatter.glsl:8.
   - NUBOS is 4 (VkCore.h:302), so there is no binding 4. VkConstBuffer keys its data by binding (VkShader.cpp:302-314). With both switches on, the Earth shader either fails to compile or each block overwrites the other's bytes.
   - Fix: SSR adds no UBO in NewPlanet. Append `#if defined(_SSR) SsrPrm gSsr; #endif` at the end of `PlanetPSBlock` (binding 2, NewPlanet.glsl:126-130), the same way SSR appends to FxParams and PbrShared.
     - Earlier offsets do not move.
     - Binding 2 is already set on every draw.
     - Keep writing the zero gSsr on every Surfmgr2 Render, because ShaderClass keeps the last data between passes.
     - Correct §4, R5 and §11, which say binding 3 is free and that SSR takes it.

2. **The Hi-Z cell index reads outside the mip levels.**
   - VkTex and VkSurf mips are floor-sized (VkCore.cpp:475-476), and level k is sampled at `⌊(X + jit)/2^k⌋`. Examples at 1920×1080:
     - level 4 is 120×67, but y = 1079 maps to row 67;
     - level 8 is 7×4, so x ≥ 1792 and y ≥ 1024 are out of range;
     - X = W at the clip edge, or X + jit < 0, is out of range at every level.
   - No robustness feature is enabled (grep finds none), so texelFetch out of range is undefined. The right and bottom of the screen get garbage hits or skipped occluders.
   - The odd-size remainder (row 134 of level 3) is also not covered by level 4, so coarse cells are no longer conservative.
   - Fix:
     - Allocate tHiZ level 0 padded to a multiple of 2^(levels−1), which is 2048×1280 for levels 0-8, and clear the padding to 0 (sky). Every reduction is then an exact 2×2, and the CPU test's footprint rule holds by construction (about 14 MB).
     - Clamp the cell index to [0, size_k − 1] in the walk anyway.
     - Size tHiZTmp to match.

3. **The Hi-Z walk skips cells for rays that move toward the camera.**
   - §5 "Else if lvl > 0: t = clamp((Sc − s0)/(s1 − s0), t, tx)" is only valid when s1 < s0, that is, when the ray moves away from the camera (Uludag advances to the depth plane only then).
   - With s1 > s0, the else branch means s(t) ≤ Sc, so t* ≥ t:
     - the ray jumps to where it reaches the closest plane;
     - if s(tx) ≤ Sc as well, it jumps over the whole cell;
     - either way it drops a level past the stretch where it was behind something, which is exactly where its hits are.
   - Vessel surfaces facing the camera send rays back toward it, and thin near parts are then missed (the reflection shows through them).
   - Fix: `if (s1 < s0) t = clamp(t*, t, tx);` and otherwise keep t, then `lvl -= 1`. Add rays toward the camera (s1 > s0) to the SsrMath walk-vs-march test explicitly.

4. **The dry base-surface model darkens or washes out every runway and apron (default config).**
   - §6 kind 1 uses F = Schlick(F0 = 0.04, N·V) with α = 0.30 and lit = mix(lit, mix(Lsky, L, conf), F).
   - Problems:
     - Raw Schlick ignores roughness. At N·V = 0.05 it gives 0.78. The split-sum directional albedo for α = 0.3 is about 0.12 (Karis 2014 EnvBRDFApprox: 0.04·0.60 + 0.097), so a rough runway at grazing angles is replaced by up to 78 % of the fallback colour.
     - The fallback Lsky = insca/max(1 − atten, 0.05) (respectively Inscatter/max(1 − Transmission, 0.05)) is a ratio of two small numbers.
       - Near the camera it is about 20·insca, close to black.
       - A few km out it can exceed 1 (BaseTilePS insca is already × (diffuse + ambi), Mesh.fx:185-192).
       - The conf for α = 0.3 is at most 0.35, so the fallback dominates.
     - F ≥ F0 = 0.04 > 0.03, so the "F < 0.03" early-out never fires. Every dry base pixel traces, and the cockpit KSC target of 0.2 ms is not reachable.
   - The design's own check, "dry: a faint grazing sheen only", fails.
   - Fix:
     - F = EnvBRDFApprox(F0, α, N·V) (Karis 2014, cite it);
     - lit = mix(lit, L, F·conf), with no Lsky: where SSR misses, the legacy pixel stays;
     - keep the early-out on F < 0.03. It then skips normal incidence when dry (0.029 at N·V = 1).
   - Use the same weight in FAST_PS and PBR_PS base meshes.

## SHOULD

1. **Water applies the camera-to-water haze to hits that already carry it.**
   - TerrainPS adds cRfl·0.75 and then applies sct.atn and the inscatter of the camera-to-water path (NewPlanet.glsl:874, 888-889).
   - The hit pixel's HDRInv value already contains the camera-to-hit haze. For far water in front of a far hit, the stretch from the camera to the water is counted twice, so mountains across a bay mirror too pale.
   - The mesh hosts strip their own haze ((y − I)/T, §6), and water does not.
   - Fix: keep the trace at 652/660 (h in a local). Do the mix after sct (827), before 874:
     - finite hits (Hi-Z hit): L = (HDRInv(y) − I_w)/max(atn_w, 1e-3), with I_w the 888-889 inscatter term;
     - sky lookups: HDRInv(y) unstripped, which matches GetAmbient.
   - Write the 0.75 into §16's water identity.

2. **The filmic HDRInv has no clamp.**
   - Filmic() clamps to 1 (Scatter.glsl:200-204), but the capture can hold y > 1:
     - under PostProcess = 1, meshes write HDR values into the RGBA16F target;
     - the mip averages of those values do too.
   - a = 2.43y − 2.51 reaches 0 at y = 1.033, and the root is NaN above it, which shows as black water.
   - Fix: min(y, 0.985) before both branches, as R4 already states for the exp branch. Add y ∈ {0.985, 1, 2} to the SsrMath test.

3. **The sky lookup uses a clipped end point.**
   - When c1.w < near, c1 is clipped (§5 Setup), and X1 is then the near-plane point, not the vanishing pixel of R. Rays that turn back toward the camera then show an arbitrary on-screen pixel as "sky".
   - Fix: allow the sky lookup only when c1 was not clipped. Take the vanishing pixel from (R, 0)·mSky with w > 0, which is what mSky is for.

4. **SSGI shares nothing with SSR.**
   - design-ssgi §1 and §9 say it needs no Hi-Z and makes its own tScn before the volume clouds, and leaves Scene.cpp unchanged.
   - So §11's "share tHiZ and tSsrCol; with SSGI on and SSR off, Scene makes SceneSsr in data-only mode" is dead code and 36 MB of memory.
   - Fix: drop the data-only mode and `HiZ()`/`Color()` "also for SSGI". SSGI should likewise drop `GiNormals()`/`GiAlbedo()` "for SSR".

5. **Height fog is not per shader.**
   - §11 says fog "rides along through the decode", but design-height-fog is a SceneFx post pass composited in AfterOpaque before the TAA resolve.
   - So the capture holds fog in front of each hit, and the fog pass then fogs the water pixel again: fog banks mirror themselves, and reflections are doubly fogged.
   - Fix: correct §11 and list it under risks. One option is to capture the colour before the fog composite. Then the fog would need to be its own TAA input, so for v1 it is probably accepted.

6. **bLabelsLate must be merged at Scene.cpp:1596.**
   - SSR wants `TaaActive() || SsrActive()`; height fog wants `TaaActive() || (pFx && pFx->FogLikely())`.
   - The integrated line must OR all three, and PostLook.cpp:248 must take TaaActive() (fog does not mention it).
   - Put both in §17 as one integrator item.

7. **The plumes far plane breaks the Hi-Z.**
   - design-volumetric-plumes §10 cuts the DepthPass far plane (SceneFx.cpp:289) to plumeFar "unless SSAO, clouds or TAA run".
   - With SSR on and those three off, tDepth would be short. SceneFx::Depth() then returns NULL, which leaves SSR off, or the terrain is missing from the Hi-Z.
   - Fix: add SsrActive() to that full-range condition (plumes owner), and note it in §2.

8. **The ownership list is wrong.**
   - §17 lists these files as "SSR-only edits", but design-scene-shadows also edits them:
     - VVessel.cpp (Render 664, 683, 794-799), VBase.cpp, PbrRender.h/.cpp (SharedNames, BeginVariant);
     - NewPlanet.glsl and D3D9Client.fx (samplers after Planet3S);
     - PbrMesh.fx (after IridThickS), Mesh.fx BaseTilePS, PBR.fx, Vessel.fx and Metalness.fx.
   - design-volumetric-plumes also edits VVessel.cpp.
   - §11's "Shadows: nothing shared" is likewise wrong.
   - Move these files to "shared" and agree the append order of the sampler_states with the shadows owner.

## NIT

1. **Sampler numbers.** Shadows' CsmS also goes after Planet3S and IridThickS. Because VkEffect numbers by text order, SSR gets 29/30 in D3D9Client.fx and 30/31 in PbrMesh.fx. PbrMesh.fx is then full (MAXBINDINGS 32, VkCore.h:303), so update R5. Handles are by name, so the code is unaffected.
2. **Includes and the parser.** ExpandIncludes expands every #include textually (VkShader.cpp:38-75), and the VkEffect parser scans past `#if` (VkShader.cpp:527-528). Put the SsrPrm.glsl and Ssr.glsl includes inside `#if defined(_SSR)`, and keep Ssr.glsl free of `uniform`, `texture` and `sampler_state` declarations.
3. **Layout after the capture.** End Capture with `dev->PrepareSample(tHiZ)` as well. The last blit leaves tHiZ in TRANSFER_DST, so the first SSR draw would otherwise call EndRendering inside the next main pass (VkCore.cpp:1576-1577).
4. **Sky weight per host.** §6 defines vMode.w = 1 − smoothstep(3 km, 12 km, alt) under water only. The LEO check (§15) needs vessels to use 1, and every host needs 0 while the planetarium is on. Say so in §8 VesselParams/BaseParams.
5. **Water gate.** Also require that last frame's proxy terrain was in tDepth (depthRec with no spherical mode) before PlanetParams sets valid. Otherwise ridges mirror as sky.
6. **AdvancedPS LOD.** AdvancedPS uses fLOD = (1 − fRghn)·10 (Vessel.fx:230), not ·8 (that is PBR.fx:327).
7. **Water early-out.** "0.75·fFS.y·fMask" counts fMask twice, since fFS already includes it (NewPlanet.glsl:637).
8. **Water roughness.** Under `_WATERFX`, fRgh is at least 0.08 (NewPlanet.glsl:638-642), so water is never on the "α < 0.05 deterministic" path with TAA. Its glint roughness also gives a cone of about mip 4.6 at 300 px, which blurs tower reflections a lot. Tune a separate α_ssr (for example 0.5·fRgh) in the Coast 300 check.
9. **Extra key.** SSRWetness is a second cfg key, against the one-switch rule (the plumes design keeps its tunables out of the cfg). Decide O1 before code; with the MUST 4 fix the dry model no longer needs a key.
10. **Dialog slot.** Shadows, SSR, plumes and SSGI all claim Effects row 2 at x 290 (D3D9Client.rc:277-286; the group is 535 wide). The integrator lays out a third row.
11. **Pass id (for the integrator).** design-scene-shadows and design-volumetric-plumes both take RENDERPASS id 0x000A.

Counts: MUST 4, SHOULD 8, NIT 11.
