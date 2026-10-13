# Design review: volumetric plumes, smoke puffs, re-entry plasma

Checked design-volumetric-plumes.md against Orbiter-linux-custom HEAD dd4e087c, the Orbiter SDK headers, the stock Atlantis/DeltaGlider sources, ~/Projects/XRVessels-linux and ~/plasma-reentry, and against design-scene-shadows.md, design-ssr.md, design-ssgi.md and design-height-fog.md. Paths are under OVP/VulkanClient/ unless shown otherwise. Read-only: nothing was built or run.

The physics numbers hold up. A_e = F0·pfac follows from Vessel.cpp:1338 and Vessel.h:1331-1334. SSME D_e is 2.328 m, the SRB D_e is 4.259 m, SSME p_e is 25.5 kPa, M_e 3.936, M_j 3.203, r_j/r_e 0.589, L_s/D_e 2.34, θ∞ 121.5°, and the cos¹⁰ profile gives 0.46 at 30° and 0.034 at 60°. The plasma colour table matches model/README.md exactly. Most file:line references are right; the exceptions are listed under NIT.

## MUST

1. **RENDERPASS_FLOWMAP collides with the shadows feature and overruns the pass-label arrays.**
   - Both designs take 0x000A "after Scene.h:57" (design-scene-shadows §1: RENDERPASS_SCENESHADOW 0x000A).
   - RenderFlowMap is modelled on RenderShadowMap, which pushes with its pass code (Scene.cpp:2645). D3D9Client::PushRenderTarget and PopRenderTargets index `labels[data.code]` in arrays of 10 entries (D3D9Client.cpp:1207, 1224, 1241, 1261).
   - LogDbg is a function (Log.h:58), so the argument is evaluated on every push. Pushing a flow map therefore reads past the array on every frame.
   - Fix: the integrator assigns SCENESHADOW 0x000A and FLOWMAP 0x000B. Extend both label arrays to 12 entries ("SCENESHADOW", "FLOWMAP"). Add D3D9Client.cpp to §18 as a shared file.

2. **The legacy re-entry glow stays on under the plasma.**
   - vVessel::Render(VkDev*) calls RenderReentry in every external view (VVessel.cpp:653). That draws the Reentry.dds billboard pair whenever ρV³ > 1e8, with full strength at 6e8 (VVessel.cpp:1759-1792).
   - The design never mentions it. §7.6 hides only the default ReentryStream.
   - At lunar-return speed (11 km/s) the billboard starts at about 70 km, where the plasma intensity I is already 1. At 7.6 km/s it starts at about 62 km. In both cases the result is a double glow, and the billboard is drawn in the opaque loop, so it also lands in the TAA history.
   - Fix: `if (!oapiCameraInternal() && !(scn->pPlume && scn->pPlume->PlasmaActive(this))) RenderReentry(dev);` at VVessel.cpp:653, or scale its `ints` by (1 − I_plasma). Add the line to §10 and §18.

3. **DIFFUSE re-entry streams are claimed as smoke.**
   - §6.1 claims "every D3D9ParticleStream with diffuse set". §7.6 says custom re-entry streams stay legacy.
   - Stock Atlantis adds its own DIFFUSE re-entry stream (Src/Vessel/Atlantis/Atlantis/Atlantis.cpp:289-293: DIFFUSE, ATM_PLIN 6e7..12e7) on top of the default one (Vessel.cpp:780).
   - As specified, Atlantis' entry trail becomes sun-lit grey spheres beside the glowing sheath, and nearly black at night. That fails the §16 check "Atlantis reentry 1 … custom streams kept".
   - Fix: puffs claim only non-ReentryStream streams. Add a `bReentry` flag in the ReentryStream ctor (Particle.cpp:765), next to bDefault, and test it in Claims().

4. **Clustering merges the Shuttle's three SSMEs into one plume.**
   - The SSME refs are 3.10, 3.44 and 3.44 m apart (Atlantis.h:154-156). §5.1 gives r_e = 1.164 m, so 3·max r_e = 3.49 m.
   - All three share THRUSTGIMBAL_LAUNCH (|a_i·a_j| = 1) and the hydrolox class, so §5.6 makes them one plume at the centroid with the cluster radius.
   - The shock cells then scale with the cluster radius, about 2.7× too long. That contradicts the §16 headline check "SSMEs see-through with diamonds and a Mach disk".
   - Fix: cluster only when needed:
     - over the 32-plume budget;
     - or when every member is below the volume LOD (r_px < 3);
     - or when the refs are closer than 1.2·(r_i + r_j).
   - When members do cluster, take r_j, L_s and x_md from a member and only the outer envelope from the cluster.

5. **The short-range DepthPass rule breaks the other batch features.**
   - §10 cuts the DepthPass far plane to plumeFar unless "SSAO, clouds or TAA run". Three other features also need a full-range DepthPass:
     - SSR forces the pass on SsrActive (design-ssr, SceneFx.cpp:273 hook);
     - height fog forces it below 30 km (design-height-fog: line 274 gets `!fogOn`);
     - SSGI writes MRTs in it (design-ssgi §2).
   - With TAA off and any of those on, they get terrain cut at plumeFar. Fog over cut terrain renders as sky, and SSR misses distant terrain. Depth() also returns NULL then (depthFull false), so they may silently skip.
   - Fix: give SceneFx one `bool full = ssao || clouds || taa || ssr || fog || ssgi` (a WantFullDepth() each feature ORs into). Cut the far plane only when PostPlume is the sole consumer. Also merge line 274's early exit as one condition.

## SHOULD

1. **δ0 uses the wrong ε for its [U] source.**
   - The user's envelope uses ε = EPS_RG = 1/16, the real-gas density ratio (solver/geometry.py:30, solve.py:177-183; the ledger "density_ratio", PUBLISHED).
   - γ 1.2351 is the effective γ for the Rayleigh–Pitot pressure only (solve.py:201-204).
   - (γ−1)/(γ+1) = 0.105 makes δ0 1.68× thicker than the source.
   - Fix: ε = 1/16 [U geometry.py:30]. Cite γ to solver/solve.py, not model/README.md (Sources list).

2. **The windward band is lost at the hull.**
   - The ray stops at D_scene, the nearest of 4 jittered tDepth texels, while zs is bilinear in a 128² map. A texel is 2.3·R_b/128 ≈ 0.4-0.7 m for the Shuttle, against λ ≈ 0.2-0.3 m.
   - On convex or grazing windward surfaces the zs sign change often lies beyond D_scene, so no crossing is found. The band comes out patchy exactly where the glow should be strongest.
   - Fix: when the ray ends at a finite D_scene and |zs − z_end| < 1.5·texel·sqrt(1 + |∇zs|²), add the band integral at the end point. Also clamp λ_min to at least half a flow-map texel.

3. **The vacuum case produces NaN.**
   - §17 expects "in vacuum M_j → ∞", so r_j and L_s become ∞ in vPl.
   - In the shader, R(x) = r_j + (r_e − r_j)·e^… is ∞ − ∞ = NaN. GLSL mix(ρ_v, NaN, 0) is still NaN.
   - The result is NaN plume pixels, and LightBlur spreads them under _LIGHTGLOW.
   - Fix: JetState clamps p_a ≥ 0.01·p_e, so M_j stays finite. Alternatively, zero the jet terms on the CPU when w_atm < 1e-3 and select rather than mix in the shader. Change the test to "finite, w_atm = 0".

4. **The fog option in §13 is not feasible.**
   - "Fog skips pixels with tVolPD" cannot work: the fog composites inside AfterOpaque (design-height-fog §2, order step 5), before PostPlume::Render exists.
   - tFH is fog along the whole path to the surface, so it is unusable at plume distance.
   - Today the plumes, puffs and ground cloud composite unfogged over valley fog, which is visible on a foggy pad.
   - Fix: agree on a CPU `SceneFx::FogAlong(origin, dir, dist, &T, &In)` built from the CPU maps the fog already keeps for FogSunT. PostPlume folds it into the per-plume and per-puff-group Tr/In it already uploads (vPl "haze Tr … In").

5. **Add-on DIFFUSE streams lose their look.**
   - Every claimed stream keeps only the atlas mean colour (§6.2). Dust, splash, debris or other textured streams become noisy spheres, and §2 offers no opt-out (SMOKE scales only the plume tail, §5.3).
   - Fix: add `PUFFS 0|1` to the class cfg. By default, claim ExhaustStream plus generic streams that use the default texture (deftex, Particle.cpp:98, 129). A non-default texture would need `PUFFS 1`.

6. **The §12 puff cost case does not test what it claims.**
   - Stock srb_contrail (Src/Vessel/Atlantis/Common.cpp:71-75: rate 3, life 12 s) with the interval rule at Particle.cpp:669 gives about 20 puffs per SRB at T+5 s and a few hundred at most. The case never comes near 6000 or the 8192 path.
   - The real fill cost at launch is the §6.6 ground cloud:
     - 384 puffs per vessel;
     - three vessels (two SRBs and Atlantis);
     - radius up to 0.5·r_j + 90 m.
   - Seen from 300-500 m, that is tens of screens of half-res RGBA16F blending.
   - Fix: base the case on the pad ground cloud from a 500 m camera. Add a stress scenario with a scripted vessel carrying 4 × 3000-particle streams. Add a fill guard: cap the summed projected half-res area per frame and drop or merge the smallest or farthest puffs first.

7. **Puff light comes from the owner vessel only.**
   - Cs, Ca and the haze Tr/In come from the owner's sunLight (§6.3, §6.5). A contrail spans kilometres of altitude, so at a twilight launch the upper column should be sunlit and the lower part in shadow. This is the signature look, and it comes out uniform.
   - Fix: on the CPU, sample the proxy's GetObjectAtmoParams and TerrainSunVisibility every 16 puffs or 2 km along each stream (as already done for ground clouds) and interpolate per puff.

8. **One §16 check contradicts §5.5.**
   - "Two frames 1 s apart with modulate 0 change by < 3/255" cannot pass: the turbulence advects at U = 25·r_e/s on the wall clock, even while paused (§15).
   - Fix: test consecutive frames for a per-pixel change bound, or freeze the clock under a test define.

## NIT

1. **Small reference errors:**
   - Flame-strip uv is D3D9Effect.cpp:183-186 and flare uv is 187-190 (not 182-185 and 186-189).
   - FlagPass is SceneFx.cpp:304-330.
   - GetExhaustCount/Spec/Level are in VesselAPI.h; OrbiterAPI.h:593-605 is only the struct.
   - Particle.fx `light = 1` is line 23.
2. **The Blackbody test fails as written.** A 6504 K Planckian is not D65. With the Wyman 2013 fit it gives linear sRGB (1, 0.945, 0.994), so "white within 3 %" fails. Test the chromaticity (0.3135, 0.3236) ± 0.002 instead.
3. **PlasmaIntensity numbers to state in the test.** With US76 at 7.6 km/s, I is 0 at 88 km, 0.09 at 85 km, 0.18 at 84 km, 0.66 at 80 km and 0.99 at 75 km. Write the test as I ∈ [0.05, 0.25] at 84-85 km.
4. **Memory.** Four half-res RGBA16F plus two half-res R32F at 960×540 is 16.6 + 4.1 = 20.7 MB, not about 15 MB.
5. **Stock cfg name.** The SRB class name is "Atlantis_SRB" (Atlantis_Tank.cpp:148), so the file is Config/GC/Plume/Atlantis_SRB.cfg.
6. **XR Isp.** The XR main Isp options start at 13,943 m/s (XR2Globals.cpp:693-697), not "4e4-class". They still classify as texture.
7. **HullBox.** Also call D9UpdateAABB(&HullBox) (VVessel.cpp:1887 does it only for BBox), or its centre and radius are stale.
8. **Thrust changes at run time.** XR changes thrust at run time (SetThrusterMax0), so put F0 and Isp0 into the §5.1 cache key, not only es.th and the class.
9. **Atlas key.** The atlas LRU is keyed by SURFHANDLE. A released and reloaded add-on texture can reuse the address, so add the VkTex image handle to the key.
10. **Half-res depth.** Map 0 → +∞ before the min of the 4 tDepth texels (§8 reads as min(0, …) = 0, which means sky).
11. **Heating shape.** heat.py uses Cp^0.5·s^−b, but S(ℓ) is multiplied by w ∝ Cp. Use sqrt(w), or tag that factor [V].
12. **Far trail.** The user's patch_wake.py concludes "the base recirculation region, and NOTHING BEYOND IT". Default A_t to 0 and let the legacy default stream stay for the far trail, which also covers the "DG at 80 km from the ground" check.
13. **cfdtools hygiene.** The lee-side "Cp identically zero" in newtonian/README.md is a HYPER_AERO (cfdtools, NOSA) result. Cite the published lee heating (1e-3 to 1e-2 of stagnation) or Lees' Newtonian shadow instead, so nothing cfdtools-derived is cited.
14. **Umenhoffer citation.** Umenhoffer, Szirmay-Kalos and Szijártó 2006 is Graphics Interface 2006, not WSCG.
15. **Filmic.** With Filmic = 1, the lit smoke should use the same curve as the lit meshes and terrain, not the M6/M7 emission curve, or white smoke will not match white hulls.
16. **Dialog layout.** Shadows, SSR, SSGI and plumes all claim (290, 428). Give the integrator a concrete layout: row 2 holds shadows, plumes and SSGI; row 3 holds SSR; the group grows to 50 and OK moves down 12.

Counts: MUST 5, SHOULD 8, NIT 16.
