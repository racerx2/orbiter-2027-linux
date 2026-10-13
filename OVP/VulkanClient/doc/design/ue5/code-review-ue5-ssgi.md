# Code review: SSGI (UE5 batch, owner "ssgi")

Repo Orbiter-linux-custom, branch ue5-batch, uncommitted tree on 35e2949c. Paths are under OVP/VulkanClient/ unless shown otherwise. Read-only review (git diff, reads, no build, no run). Scope: SceneFxGi.cpp, SsgiMath.h/.cpp, Tests/SsgiMath.Test.cpp, shaders/SSGI.glsl, shaders/SceneDepth.glsl, NewMesh.glsl SceneDepth_PS, Mesh.h/.cpp (RenderShadowMap opt 2, GlobalInit, the tile flag), and the MRT parts of SceneFx.cpp/.h, Tree.glsl, TreeMgr.cpp:111, Surfmgr2.cpp RenderDepth. Design: design-ssgi.md Rev 2 with design-review-ssgi.md.

**Verified.**
- **GI maths** (checked by hand against the formulas, and the GLSL against SsgiMath.cpp line by line):
  - SliceCdf (SSGI.glsl:227-232): both branches integrate cos(θ − γ)|sin θ| from γ − π/2; q.w = F(0); the total is cos γ + γ sin γ (= M at :314). γ is clamped to ±π/2 (:311), so M ≥ 0 and 1/M is finite.
  - Bits (:234-243): floor(32F/M + jit) on both edges with one jitter per slice, so the expected bit count is 32ΔF/M (unbiased). n = 32 gives the full mask, and there is no shift by 32.
  - Slice plane: T is the in-plane tangent of the ray at uv0 + ω. Every tap on that screen line lies in the plane through the camera. Front angle atan(D·T, D·V), back angle with B = S + rs·T_thick. Terrain's 1e6 thickness sends the back angle to ±π, which is clamped to the hemisphere edge on the tap's side. That is the heightfield case.
  - Estimator: E = mean over slices of |n_p|·M·Σ popcount(new)·L/32, the GTAO measure with bits of mass M/32. A fully covered hemisphere with uniform L gives L. The brute-force tests (pit sin²α, tilted normals, half-space) cover it.
  - Slice angles (k + ξ)π/2 over [0, π), each slice two-sided with one occ mask. Taps run near to far, so newly covered sectors take the nearest sender.
- **Senders and composite per family.** These match the shaders:
  - terrain with air: Tw⁻¹(y) − Expo·I_raw, then ÷A and ÷(Expo/e); the forward side is Tw(u + A·s·ρ·E);
  - airless: Ta⁻¹, LightFX with L0 = r/(2 − r);
  - trees: Tw with their own s;
  - Metalness: Tm with the LightFXSq quadrature, S = 2L²/(3.24 − L²), L ≤ 1.75;
  - PbrMesh: c = S2L(Tm⁻¹(q)/e) plus ρ·S2L(E/e), back through L2S, Tm and A;
  - legacy and base tile: identity with the clamp min(ρE, eρ − q).
  - With air, TrExpo is not exposure-scaled (VPlanetAtmo.cpp:586-587), so terrain ρ = pow(tex, TrGamma)·TrExpo and s = Expo/e cancel e. Airless: ρ = tex and s = TrExpo/e.
- **Sky ambient (M4).** MeshAmb (SSGI.glsl:146-159) is GetObjectAtmoParams (VPlanetAtmo.cpp:543-552) term for term:
  - AmbientApprox(vNrm, false) gives (cAmbient, fA), so mc.g = cAmbient.g;
  - fA uses smoothstep(0, TW_Dst, −dNS) and is ×tw_bld from GetAtmoParams() (the same mode −1 call UpdateScatter makes);
  - SunLightColor(−dNS, alt) is Gauss7/RayLength·rmO, capped at max 1; the factor is exp(−alt·iH.x)·rmI.x·6e5, plus the config level (sunLight.Ambient, Scene.cpp:505);
  - in space (alt > AtmoAlt/2) or without air it is the config level only.
  - TerrAmb is NewPlanet.glsl:955-985 with fM = 0.5 and cSF capped.
- **Metalness (M6).**
  - PBR: metal = factor·mr.b (vMtl.z = 1), the base colour decoded when NeedsDecode, ρ·(1 − metal).
  - Metalness groups: map.g, or D3D9MatExt::Metalness (Metalness.fx:172-173).
  - Fully lit groups (UsrFlag 0x4): ρ = 0.
  - Families come from IsPbrGroup / SHADER_METALNESS / bGiTile. Only VBase's tile mesh uses the (groups, MESHGROUPEX**, SURFHANDLE*) constructor (RingMgr uses another one).
- **Temporal.**
  - Rigid and the unjittered VP are built as PostTaa (PostTaa.cpp:125-145): row-major `v * M` and F16 row order.
  - The distance test reads the stored |P| in .a. The history is rescaled by e/ePrev.
  - It resets on a debugger camera or wireframe, a proxy/focus/target/internal change, a frame gap, or a failed pass.
  - The history texture is never read on a reset frame. PrepareSample moves a never-written tHist out of UNDEFINED (VkShader.cpp:327).
- **Composite.**
  - 2×2 depth/normal-aware upsample of the history; fade 15-25 km; sky and d ≥ 25 km give 0; ρ ≈ 0 gives 0.
  - Output dy ≥ 0, alpha 0, ONE/ONE through Execute(2, …). The blend function is restored by SetState: State holds blendEq (VkCore.h:268).
- **NaN and Inf.**
  - Fin() is applied on SenderV, the prep output, the march output, the temporal output (with the history read), the composite E and CompositeDy.
  - E is clamped to [0, 4·uMax]. TwI clamps to 0.98, and the filmic root has a positive leading coefficient there. TmI uses max(1 − y⁴, 1e-4). TaI/S2L/L2S/LFXSq clamp their bases.
  - normalize(0) in MeshAmb/TerrAmb (cAmbient and the sun both 0) is caught by the Fin at the end.
- **MRT lifetime (M2).**
  - Bound only between GiBind(0) (after the push, before the Clear, so VkCore.cpp:1180 clears tAlb and tNrm with tDepth) and GiBind(2) (before PopRenderTargets). FlagPass binds nothing.
  - SetRenderTargetN ends rendering on each change.
  - Every draw inside the bound region writes locations 1-2:
    - terrain: SceneDepth `_SSGI`;
    - bases and buildings (RenderShadowMap opt 2, VBase.cpp:443-450, BldgMgr.cpp:480-500);
    - trees (Tree.glsl distance branch);
    - vessels (VVessel.cpp:806 is the only SCENE_DEPTH draw).
  - So no attachment is left with undefined values.
  - giCls is −1 outside the region, so FlagPass, CSM casters (opt 1) and every other pass skip the ps_gi uploads. Writes to locations without an attachment are dropped.
- **IPI use.**
  - One VkConstBuffer serves all entries (IProcess.cpp:40-42, 120-128), and SetFloat/SetTextureNative persist across Activate. So the GiPS values and the samplers set under PSGiPrep reach the other four entries.
  - Every SetTextureNative name is live in the entry that is active when it is set, so there is no log spam.
  - Outputs 1-3 are NULL. No entry samples its own output.
- **Resource lifetime.**
  - FreeGi deletes the IPI first, then each VkSurf before its VkTex. ~SceneFx calls FreeGi before the depth targets.
  - Created only in InitGi (Config->SSGI && depthOK).
- **Off path (SSGI = 0).**
  - gi is NULL, so there is no GiBind, no pass, no buffers, and `more` is not set by GI.
  - pTerrain gets "" (as before). Mesh GlobalInit gets NULL. Tree flags are unchanged. Surfmgr2 gets hDiff = NULL (GetConstantByName returns NULL without logging) and uploads the old 128 bytes.
  - Each `#else` keeps the original line verbatim, so the SPIR-V is unchanged (the owner's off.sh hashes agree).
  - With SSR on and SSGI off nothing GI is made, and SSR needs nothing from SSGI: ue5-ssr-changes.md says "Not used: SceneFx::Normal()", and there is no caller in the tree.
- **Uniform layout.**
  - DepthPrm (mW, mVP, vTexOff, vAlb) = the 160-byte C++ struct. BuildTable reflects all block members (EShReflectionAllBlockVariables, VkShader.cpp:193), so the VS-side "Prm" handle covers vAlb, which the PS reads from the shared binding-0 buffer.
  - PSGi has 3 vec4 on both sides.
- **Cost.** Owner-measured 0.48-0.56 ms for Gi and 0.047-0.070 ms extra in the DepthPass (KSC Mid, 1080p): within §8. The timing code is removed (grep: no GiStamp/TEMPORARY left in SceneFxGi.cpp).
- **Rules.**
  - Comments are one-line, with Jimenez 2016, Therrien 2023, Ritschel 2009, Cigolle 2014 and Jimenez 2014 credited.
  - No vendor or GPL code was found. The Tree.glsl albedo lines under `_SSGI` repeat the project's own lines 156-159.
  - `// not upstream` marks the shared-file edits.
- **Delta at `<!-- DONE -->` (17:20).**
  - All 14 owner files have the same md5 as at the first read (17:11).
  - The only change is the notes: the timing code was removed, and the finish runs were lost to the device reboot at about 17:17. /tmp/earth is gone, including build.sh, run.sh and every log the notes cite. The tree is intact.
- **Not verified.** Nothing was run, by me or (after the reboot) by the owner:
  - the three KSC Low finish runs (on, off, strength 2);
  - design §13 Canyon/Moon/Hangar/Night;
  - the p99 stability and motion checks;
  - MSAA 4 with validation layers (design-review M2);
  - Alps High above the 2D deck with VolClouds = 0 (S5).
  - The off-path SPIR-V identity is taken from the reading above and from the owner's pre-reboot hashes.

## MUST
None.

## SHOULD

1. **SceneFxGi.cpp:144: `gi->bound = gi->run || Config->SSR` binds the MRT every frame for a reader that does not exist.**
   - **Problem:**
     - SSR never calls Normal() or Albedo(). ue5-ssr-changes.md says "Not used: SceneFx::Normal() (ssgi)". A grep of the tree finds no caller of Normal()/Albedo() outside SceneFxGi.cpp.
     - With the defaults (SSR 1, SSGI 1), every depth-pass frame where GI's own gate is off still pays for the MRT. That covers orbit, flight above 20 km, and any frame with no vessel within 2 km:
       - the Clear and the writes of two W×H 32-bit targets;
       - the per-tile tDiff bind and descriptor push in Surfmgr2 RenderDepth;
       - base/MR map sampling in every mesh group;
       - the ps_gi uploads.
     - That is about the measured 0.05-0.07 ms on the GPU, plus CPU per terrain tile, for nothing.
     - The owner's notes ("Requests for other owners: ssr ...") promise a contract nobody consumes.
   - **Fix:** `gi->bound = gi->run;`. Drop the "for ssr" text in that comment and in the notes. Keep Normal()/Albedo() returning NULL unless written. If SSR ever needs them, it can ask for a flag then.

## NIT

1. **SceneFx.cpp:281 (integrator line): `more = depthOK && (gi || …)` is true whenever gi was allocated.**
   - **Problem:** That includes after a failed InitGi (gi->ok false) and frames where GI's gate is off.
     - With SSGI the only depth reader (SSR, VolPlumes, TAA, SSAO, clouds and fog off), DepthPass runs every main frame in orbit or deep space for no reader.
     - After an init failure it runs every frame forever.
     - The defaults hide it, because SSR and VolPlumes already set `more`.
   - **Fix:** at least `(gi && GiOk())`. Better, a const gate shared by AfterOpaque and GiBind(0): camera under 20 km or a vessel within 2 km. AfterOpaque has the vessel list.
2. **SceneFxGi.cpp:130-133: on an InitGi failure only the IPI is freed.**
   - **Problem:** The ~55 MB of targets stay allocated for the session.
   - **Fix:** delete the targets too (keep gi with ok = false, so DepthPass/GiBind stay off).
3. **Mesh.cpp:3137: legacy groups with UsrFlag 0x10 (no texture colour) take the texture into ρ.**
   - **Problem:** The PBR branch honours 0x10 (Mesh.cpp:3115), and the legacy shaders ignore the texture colour for those groups (Mesh.cpp:1949, 2296, 2644 bNoC). Such a group (texture used for alpha only) gets ρ = tex·Diffuse, a darker or tinted bounce and cap, instead of Diffuse.
   - **Fix:** `vMat.w = (hTex && !(Grp[g].UsrFlag & 0x10)) ? 1 : 0`.

