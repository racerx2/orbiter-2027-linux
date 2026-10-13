# Design review: SSGI (design-ssgi.md)

Repo: Orbiter-linux-custom, HEAD dd4e087c, read-only (Desktop Commander reads, git untouched). Paths under OVP/VulkanClient/ unless shown. Every item below was checked against the tree; the slice maths, the 10-bit octahedral error and the mesh-curve error were also computed numerically.

Verdict: the method (GTAO slices + visibility bitmask, CDF-uniform bits) is sound and the hook placement is right. The integration has 6 MUST items: a missed gate, an MRT lifetime hole, and four places where the radiance model does not match what the shaders actually do. The MRT question in risk 1 is not a risk (see NIT 1).

## MUST

**M1. The terrain is never in the depth pass with SSGI alone.**
- Surfmgr2.cpp:1973: `depthRec = (Config->SSAO || Config->VolClouds) && …`.
- With SSAO = 0 and VolClouds = 0, no tile is recorded (1300), and RenderDepth returns at 1837.
- Then tDepth, tAlb and tNrm hold no terrain, so there are no terrain senders or receivers. Canyon, Moon and KSC terrain all show nothing.
- §6 lists only RenderDepth (1835-1855) for Surfmgr2.
- Fix: `(Config->SSAO || Config->VolClouds || Config->TAA || Config->SSGI)`. TAA is missing today too (shadows risk 10, SSR §2). List 1973 in §6 and §14 as shared with SSR.

**M2. The MRTs leak out of FlagPass; the restore paths never clear them.**
- Nothing on the restore paths touches rtExtra:
  - VkDev::SetRenderTarget (VkCore.cpp:1047-1057);
  - D3D9Client::Push/PopRenderTargets (D3D9Client.cpp:1205-1262);
  - AfterOpaque's re-push and SetState (SceneFx.cpp:282-284).
- ImageProcessing::Execute saves whatever rtExtra holds and puts it back (IProcess.cpp:305, 413).
- §6 unbinds only in DepthPass. §2 binds sAlb/sNrm again in FlagPass after 323 and never unbinds them.
- What then happens:
  - Ssao, VolShadow, Clouds and Ssgi restore them;
  - the re-push at 282 makes the main pass attach the W×H single-sample tAlb/tNrm next to the main target in BeginRendering (VkCore.cpp:1119-1131). With MSAA that is a sample-count mismatch, a validation error and undefined behaviour;
  - without MSAA, every later main-pass draw loads and stores two extra 8 MB targets.
- Fix: in both passes, `SetRenderTargetN(2, NULL); SetRenderTargetN(1, NULL);` before PopRenderTargets, best as a small RAII guard.
- Simpler for FlagPass: do not bind at all. The rewrite is identical, and writes to locations without an attachment are dropped.
- Add a check with MSAA 4 and validation layers to §13.

**M3. The probe rule takes the terrain bounce off almost every vessel, by default.**
- §5.4 sets W[vessel][terrain] = 0 whenever probes run.
- Only Metalness.fx:266-276 and PbrMesh.fx:228-235 sample IrradS (grep: no other shader does).
- PBR_PS, FAST_PS (PBR.fx:269, 503) and AdvancedPS (Vessel.fx:96, 145) use gSun.Ambient and EarthGlow only. These are the legacy DG/XR shaders.
- The defaults are EnvMapMode = 1 and Irradiance = 1 (D3D9Config.cpp:62, 98). So the default config removes the ground bounce from legacy hulls whose ambient never contained the ground.
- Probes also exist only for vessels with apprad > 8 px (Scene.cpp:1546, 1573).
- Fix:
  - write a per-pixel shader family into tAlb.a (2 bits, free today): 0 legacy linear, 1 Metalness (`Grp[g].Shader == SHADER_METALNESS`), 2 PbrMesh (`Grp[g].bPbr`), 3 base tile. Set it in RenderShadowMap opt 2, and set the base tile mesh through a static, as SetGiClass does;
  - zero the probe-covered senders only for families 1 and 2.

**M4. The mesh and tree ambient in the double-count rule is the wrong number.**
- §5.3 uses v_amb = e·scn->GetSun()->Ambient. That is only the config level (Scene.cpp:496).
- In air, vessels, bases, buildings and trees are lit with GetObjectAtmoParams' sky ambient: `unit(mc.rgb + cSunAmb·2)·mc.a·mc.g·exp(−alt·iH.x)·rmI.x·6e5 + cfg` (VPlanetAtmo.cpp:541-552).
  - Set at VVessel.cpp:1514, VBase.cpp:354, BldgMgr.cpp:522 and TreeMgr.cpp:540-544.
  - It reaches the shaders as gSun.Ambient (Metalness.fx:282, PBR.fx:503, Vessel.fx:96).
- So max(L_s − L_amb, 0) subtracts almost nothing on mesh receivers in daylight. The sky ambient is double counted in every covered sector.
- Fix: in PSGiMarch, compute v_amb for mesh and tree receivers from the same formula. Port 541-552 using Const, AmbientApprox(vPlN, false) and GetSunColor at the receiver, plus the config level, times e. Keep the config level alone without air. Add a SsgiMath test that matches GetObjectAtmoParams.

**M5. The inverses can return Inf or NaN, and the composite clamps nothing.**
- The failing inputs:
  - Tm⁻¹(q) = q(1 − q⁴)^−¼ for q ≥ 1;
  - Tw⁻¹(q) = −ln(1 − q) for q ≥ 1;
  - the filmic root for y > 1.03;
  - negative q.
- These inputs happen. For trees and meshes, A and I are GetLandView stand-ins (§11.8), while vessels nearer than 1 km really use A = 1, I = 0 (VPlanetAtmo.cpp:554). Add 8-bit quantisation and the MSAA resolve.
- §4.2 clamps y ≤ 0.98 in the prep only. The receiver table has no clamp.
- A NaN added through ONE/ONE poisons the target, then the TAA history and the exposure measure.
- Fix, for every family in both prep and composite:
  - q = clamp((y − I)/max(A, 0.05), 0, 0.98);
  - terrain: max(Tw⁻¹(y) − Tw⁻¹(I), 0);
  - y′ from q, and output max(y′ − (q·A + I), 0).
- Tests: I > y, A = 0.01, y = 1, and the filmic y = 1.

**M6. Metallic surfaces get a full diffuse bounce.**
- The lit shaders kill the diffuse part on metals:
  - Metalness: fA = (1 − fFrs)(1 − fMetal) and cAmbient·(1 − fMetal) (Metalness.fx:286, 349, 354);
  - PbrMesh: (1 − metallic)·(1 − Max3 Fd)·base (PbrMesh.fx:303, 354), with metallic = factor·mr.b (402-408).
- §4.1 writes ρ = tex·colour only, so chrome and gold-foil parts glow with ground colour.
- The factor alone is not enough. The glTF default metallic is 1 (PbrMaterial.h:47), with the metallicRoughness map's B channel doing the work.
- Fix, under `_SSGI`:
  - PSGi.vMat gets the metallic factor and a map flag;
  - SceneDepth_PS declares a second sampler (binding 5) for the MR map, and ρ ·= 1 − factor·mr.b;
  - legacy Metalness groups use D3D9MatExt::Metalness (D3D9Util.h:309);
  - add a PBR bench check: metallic = 1 gets no added light.

## SHOULD

**S1. Most meshes have no Tm curve.**
- §5.1 applies Tm to all meshes. Only Metalness (Metalness.fx:393-394) and PbrMesh (538-542) curve.
- FAST_PS and PBR_PS show y = e·x·A + I (PBR.fx:533-535). AdvancedPS is the same (Vessel.fx:258). BaseTilePS shows (x·A + I)·e (Mesh.fx:225). These are the DG, the XR series, runways and pads.
- Tm⁻¹(y)/y is 1.035 at 0.6, 1.14 at 0.8, 1.31 at 0.9, 1.52 at 0.95 and 1.89 at 0.98. The "< 2 % for y < 0.6" claim holds only below about 0.53. So a lit white hull is sent up to 1.9× too bright, and bright receivers get too little.
- Fix: the family code of M3 picks the inverse/forward pair in prep and composite. Mirror it in SsgiMath with round-trip tests per family.

**S2. The PBR albedo is linear, not "as stored".**
- baseColor maps are loaded as _SRGB images (PbrRender.cpp:193-202, 248-256, 309 `PbrSlotSRGB`), so tDiff sampling in SceneDepth_PS returns linear values. The legacy-texture fallback (UNORM) stays encoded. §10's "written as stored (sRGB-encoded)" is wrong.
- PbrMesh lights in linear, decodes legacy light (Decode, PbrMesh.fx:213, 233), then shows Tm(LinearToSrgb(x)·e).
- Fix: for family 2, composite in linear: c = SrgbToLinear(Tm⁻¹(q)/e), c′ = c + ρ_lin·SrgbToLinear(E/e), y′ = Tm(LinearToSrgb(c′)·e)·A + I. Decode the fallback texture in the depth pass. This replaces the "about 1.5×, tuned" note.

**S3. Two families compress the irradiance before the albedo, so "no extra gain" (§5.1) is wrong for them.**
- Airless terrain is ρ·LightFX(E), with LightFX(x) = 2x/(1+x) (NewPlanet.glsl:787, Scatter.glsl:216-220). Its slope at 0 is 2, so the Moon shadow bounce is half the shader's own response; in sunlight it is too large.
- Metalness adds lights in quadrature, then compresses: LightFXSq(Sq(sun) + … + Sq(amb)) (Metalness.fx:91-96, 354).
- Fix:
  - airless receivers: Δx = ρ·(LightFX(E0 + ΔE) − LightFX(E0)), with E0 = r/(2 − r) and r = min(x/ρ, 1.9);
  - Metalness: at least the low-light gain 1.8/√2, and state the model in §5.1;
  - test each against the shader formula.

**S4. With TAA off, labels are GI senders and receivers.**
- Planet labels, scene art and surface labels draw before AfterOpaque when TaaActive() is false (Scene.cpp:1596, 1822, 1830, 1848). So they are in tScn, and the composite adds to their pixels.
- Fix: `bLabelsLate = TaaActive() || (pFx && pFx->GiLikely())`, the same pattern SSR (SsrActive) and fog (FogLikely) use. One merged line for the integrator. So "Scene.cpp: no change" no longer holds.

**S5. Main-pass content that is not in the depth pass is taken as the geometry behind it.**
- Examples: the 2D cloud layer seen from above, horizon haze, cNgt2, light sprites and glints.
- This gives bright fake senders, up to uMax = 3.
- Fix: a plausibility cap in PSGiPrep: v_s ≤ 1.5·ρ_s·(2·|sun colour| + v_amb/e)·e, using tAlb and the sun at P. It also bounds labels and glints.
- Add Alps High above the 2D deck, VolClouds = 0, to §13.

**S6. The IPI keeps its MRT outputs.**
- pRtg[] persists across Execute; SceneFx::Clouds clears output 1 by hand (SceneFx.cpp:460).
- After the 3-output PSGiPrep, the W2 march, blur and temporal passes would bind tAtn/tIns again. They have the same size, so there is no error, but the attachments are not written and become undefined.
- PSGiComposite (full res) would then fail the size check (IProcess.cpp:186), and no GI would appear.
- Fix: `SetOutputNative(1, NULL); SetOutputNative(2, NULL);` right after the prep.

**S7. Clamp γ for unflipped normals.**
- Only meshes are flipped toward the camera (§4.1). Interpolated terrain and tree normals can face away at silhouettes.
- Then |γ| > π/2: F's domain breaks, and M(γ) = cos γ + γ sin γ < 0 near π.
- Fix: in the march, γ = clamp(γ, −π/2, π/2) for every class, or flip N when N·V < 0.

**S8. Failure path: never clear Config->SSGI at run time.**
- When things are compiled:
  - SceneDepth_PS is compiled with `_SSGI` in D3D9Mesh::GlobalInit (D3D9Client.cpp:677), before Scene and SceneFx (680);
  - Trees per planet (VPlanet.cpp:416);
  - Surfmgr2 picks the prm size from Config->SSGI.
- "SSGI off" on an InitGi or MRT failure must mean gi = NULL or !ok only.
- Key the larger DepthPrm and the tDiff bind on a SceneFx flag recording how pTerrain was compiled, not on a mutable flag.

**S9. The octahedral test bound cannot pass.**
- 10-bit UNORM oct (round-to-nearest) gives a 0.23° max and 0.08° mean over 10⁶ random normals (computed). "Within 0.15°" fails.
- Fix: bound 0.25°, or the precise encoder (Cigolle et al. 2014, best of the 4 neighbouring codes).

**S10. Conflicts with plumes and fog.**
- Plumes §10 (DepthPass far plane = plumeFar unless SSAO, clouds or TAA run) must add SSGI. Otherwise tDepth, tAlb and tNrm are cut in SSGI-only frames.
- Fog §2 and §9 require anything that changes surface radiance to run before Fog. With fogFirst, Fog runs before Clouds. So Ssgi, with its tScn copy, must come before the first Fog call, not just "before 281".
- AfterOpaque's 273-274 predicate is edited by SSGI, SSR, fog and plumes: one merged expression.

## NIT

1. **Risk 1 is not a risk.**
   - VkDev uses VK_EXT_shader_object (VkShader.cpp:201, 641-642) and dynamic rendering (VkCore.cpp:1107-1131). There is no pipeline key.
   - Blend enable, equation and write mask are set for 4 attachments (VkCore.cpp:1272-1278, 1343-1386).
   - ShaderClass::Setup(…, 0) sets blend off and writeMask 0xF for all of them (D3D9Util.cpp:1815-1871). So MRT through MeshShader, Tree and SceneDepth works as VolMarch does.
   - Replace risk 1 with M2.
2. Give M(γ) = cos γ + γ sin γ and the bit index ⌊32·F(θ)/M⌋. Numerically, the CDF matches ∫μ to 1e-8, and (1/π)∫|n_p|M dφ = 1 for normal tilts of 0-85°.
3. `GetLandView(|P + CamPos|, normal of P)`: the second argument must be the geocentric up, normalize(P + CamPos), as vPlN in NewPlanet.glsl:592 and 827. It is not the surface normal.
4. §12: E/L = sin²α holds only for an untilted normal. The 30° tilt cases need the brute-force reference.
5. The additivity test fails at the 0.98 clamp. Restrict it to y′ < 0.95.
6. Is "tap spacing < 2 px" in W2 or full-res pixels? Say which.
7. Decode the class as round(a·3). The 2-bit alpha holds 0, 1/3, 2/3, 1 exactly.
8. SSR §11 offers SceneSsr "data-only" HiZ/colour for SSGI. SSGI cannot use them: SSR captures after the resolve, which is after the GI composite, the clouds and the late labels. Drop that note in SSR. SSGI's GiNormals()/GiAlbedo() have no SSR consumer, so drop them too.
9. Shadows: the CSM casters call RenderShadowMap opt 2 per cascade. Gate SSGI's extra binds and ps_gi uploads on `scn->GetRenderPass() == RENDERPASS_SCENE_DEPTH`.
10. Dialog: SSGI, SSR and shadows all place their box at (290, 428) after heat haze (D3D9Client.rc:286). The integrator lays out the row. Plumes suggests IDC_FX_SSGI 3057.
11. §8: when SSGI is the only DepthPass consumer (SSAO, clouds and TAA off), the whole DepthPass and FlagPass are SSGI's cost. Time them separately.
12. SSGIStrength is a second cfg key beside the one switch. That matches the SSAOStrength precedent, but it is the owner's call.

## Checked and correct
- Hooks and lines:
  - SceneFx.cpp:37, 53-60, 263-285, 287-301, 304-333;
  - Scene.cpp:1954-1962, 2051-2079, 2084;
  - Mesh.cpp:3764;
  - TreeMgr.cpp:110;
  - Tree.glsl:118, 120-123;
  - NewMesh.glsl:228-235;
  - NewPlanet.glsl:440-444, 787-788, 822, 855, 866, 889, 894;
  - Surfmgr2.cpp:1835-1855, 1155.
- The Clear clears every bound MRT (VkCore.cpp:1180).
- Additive blend via SetBlendFunc plus Execute(2, …) (IProcess.cpp:282, 287).
- StretchRect resolves MSAA (VkCore.cpp:1625).
- Unset samplers get the default texture (VkShader.cpp:326).
- Bindings 13-24 are free next to Scatter.glsl's 4-12 (MAXBINDINGS 32).
- A2B10G10R10 is a mandatory colour-attachment format.
- Inversion order:
  - terrain with air (cTex·A + insc inside HDR) matches (Tw⁻¹(y) − Tw⁻¹(I))/A;
  - trees: Tw(Expo·c)·A + I;
  - Metalness and PbrMesh: e → Tm → ·A + I;
  - the filmic inverse root is right.
- Off: VkCompileGLSL emits no debug info (VkShader.cpp:197), so code inside inactive #if leaves the SPIR-V byte-identical.
