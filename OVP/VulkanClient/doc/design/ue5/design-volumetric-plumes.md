# Design: volumetric engine plumes, launch smoke and steam, re-entry plasma (UE5 batch)

Repo: /home/racerx/Projects/Orbiter-linux-custom, branch Orbiter-linux-custom, HEAD dd4e087c (clean). Paths are under OVP/VulkanClient/ unless shown otherwise. All line numbers are from this tree. The design builds on design-visual-batch.md (Rev 2/3 as built), map-postprocess.md and vfx-changes.md.
The user's ~/plasma-reentry (their own project) was read read-only. Only their computed data and the published relations they cite are used, tagged [U]. Nothing is copied from cfdtools (NASA Open Source Agreement 1.3) or from their scripts.

## 0. Rules
- **One switch:** `VolPlumes` (default 1), read once at start. It covers the plumes, the smoke puffs and the plasma.
- **Switch off:**
  - PostPlume is never created.
  - Plume.glsl is a new file and is never compiled. No existing shader is edited, so there is no SPIR-V change.
  - Every new branch in Scene, VVessel, Particle and SceneFx is behind `pPlume` or a flow-map pass that never runs.
  - No buffers are made, and DepthPass is exactly as today.
- **Provenance tag on every constant:**
  - [P] published (see Sources);
  - [U] from ~/plasma-reentry;
  - [D] derived here (tested in PlumeMath);
  - [V] visual choice, kept in one `PlumeMath::Tune` table and not in the cfg (one switch).
- **Code style:** own code, one-line comments, `// not upstream` in upstream files. No GPL, Khronos or vendor sample code.
- **Main pass only.** The custom camera, env map, irradiance and shadow passes keep the legacy exhaust and particle drawing. The 2D panel, HUD, MFDs and GDI are never touched, and nothing is drawn as text on the sim screen.

## 1. What it does and why
- **Today:**
  - Exhausts are a flame quad plus a flare quad (D3D9Effect.cpp:752-808), shaded by ExhaustTechPS with alpha blending (D3D9Client.fx:644-647, 784-797).
  - The flame has a constant length `lsize`, so it does not grow with level or thin with altitude.
  - The flicker is a per-frame `rand()` (D3D9Effect.cpp:759), and the flame cuts the ground with a hard line.
  - Diffuse particles are unlit: ParticleDiffuseVS sets `light = 1` (Particle.fx:24), so smoke glows white at night.
  - The re-entry stream is emissive billboards with a fixed (1, 0.7, 0.5) colour (Particle.cpp:773-779) and no shape.
- **(a) Plumes.**
  - Every active EXHAUSTSPEC becomes an analytic emissive and absorbing volume about its axis.
  - Its shape comes from ambient pressure against an estimated exit pressure:
    - at sea level, a necked or bulged jet with shock diamonds and a Mach disk;
    - in vacuum, a wide faint cone (Simons).
  - Its colour comes from a propellant class, set by an Isp heuristic or a per-class cfg. An unclassified engine keeps its add-on texture's colours, mapped onto the volume.
  - It is ray-marched at half resolution, stops at tDepth (so the soft intersections come for free), and is upsampled bilaterally.
- **(b) Smoke and steam.**
  - Every DIFFUSE particle stream (smoke, vapour, contrails, from any add-on) is drawn as lit spherical-billboard volumes. The lighting is the sun with a column shadow, the sky ambient, and the plumes' own light.
  - A client-side ground cloud is added where a plume hits the ground in air.
- **(c) Plasma.**
  - It applies to vessels that own a re-entry stream and have a high energy flux.
  - There is a glow in the shock layer just upstream of the windward hull, taken from a flow-aligned depth map of the real mesh.
  - Around that there is a soft halo, a base wake and a faint trail.
  - The colour depends on velocity, from the user's CEA-based table, and the flicker is smooth.

## 2. Switch, config, dialog, docs
- **Config:**
  - D3D9Config.h: `int VolPlumes;` after HeatHaze (:122).
  - D3D9Config.cpp: the default at :122, the ReadParams clamp 0..1 after :246, and WriteParams after :368.
- **Dialog:**
  - resource.h: `IDC_FX_PLUMES` 3056. The integrator fixes the order of the five UE5 ids; suggested: shadows 3054, SSR 3055, plumes 3056, SSGI 3057, fog 3058.
  - D3D9Client.rc: "Volumetric plumes" on the Effects row 2 (y 428, x 290, w 75). Row 2 is free from x 290. If all five UE5 boxes land, the group and dialog grow by 12, and OK moves down.
  - VideoTab.cpp: setChecked after :936 and save after :1033.
- **Per-class cfg** (`Config/GC/Plume/<ClassName>.cfg`):
  - Optional; one file per vessel class, so add-ons can ship their own. Config/ is copied wholesale (CMakeLists.txt:117).
  - The class name is parsed as MaterialMgr does it (parse_vessel_classname, MaterialMgr.cpp:174-200).
  - Lines (later lines win):
    - `EXHAUST <i|i-j|*|main|retro|hover|att> <hydrolox|methalox|kerolox|hypergolic|solid|texture> [pe=<kPa>]`
    - `SMOKE <0..2>`
    - `GROUND <steam|dust|none>`
  - The group names are matched by enumerating that group's thrusters (GetThrusterGroupHandle/GetGroupThruster) against `es.th`.
  - Stock files to ship:
    - Atlantis: exhausts 0-2 hydrolox (the SSMEs), OMS and RCS hypergolic;
    - the SRB class: solid;
    - DeltaGlider: texture.
- **Docs:**
  - VulkanClient.html: an Effects `<li>` after :681, plus a short "Engine plume classes" paragraph on the cfg format.
  - README.md:225 (the key list), NEW.tex:48, LAUNCHPAD.tex:555 (the Effects list).

## 3. Where it hooks (Scene::RenderMainScene, Custom lines)

| Step | Where | What |
|---|---|---|
| U | Scene::Update, after the stream loop (Scene.cpp:797-803) | `if (pPlume && !oapiGetPause()) pPlume->Update();` steps the ground clouds in sim time |
| C | before `pFx->AfterOpaque` (Scene.cpp:1954) | `float plumeFar = pPlume ? pPlume->Collect() : 0;` (CPU, details below) |
| A | Scene.cpp:1954 | `pFx->AfterOpaque(RenderMarkers, pBackBuffer, plumeFar)`: SceneFx runs DepthPass if plumeFar > 0 (§10) |
| — | 1955-1962 (unchanged) | TAA resolve, jitter off (1956), late labels, `pLook->Haze` |
| R | right after Haze (Scene.cpp:1962), before the markers (1964) | `if (pPlume) pPlume->Render(pBackBuffer);` (GPU passes, §4) |
| X | exhaust loop (1988-1996) | vVessel::RenderExhaust skips claimed exhausts, or crossfades them (§11) |
| P | particle loop (2014) | `if (!(pPlume && pPlume->Claims(pstream[n]))) pstream[n]->Render(pDevice);` |
| E | after 2014 | `if (pPlume) pPlume->EndFrame();` ends the claims (secondary scenes are unaffected) |

- **Collect (C)** runs on the CPU:
  - it gathers the exhausts, the plasma vessels and the puffs;
  - it sorts the puffs and builds their opacity maps;
  - it uploads tPuffData;
  - it returns the far distance of everything visible, or 0 if nothing is.
- **Why after the resolve, as the legacy flames are:**
  - The plume is not in tDepth, so TAA would reproject plume pixels with the background's motion. A chased rocket's plume would smear.
  - The pass is unjittered and gets no history.
  - Its noise is static per pixel and its motion is smooth in time (§8). This meets "stable without TAA".
  - An own vessel-frame history is an option (§15).
- **Order details:**
  - Running after Haze means the haze bends the background, not the flame (same as design-visual-batch §4).
  - Running before the markers keeps the vessel labels on top, which the legacy flames did not.
  - Running before RenderPbrDeferred (2016) means PBR glass draws over the plumes, as it does over the legacy flames.
  - Running before the VC (2042-2079) means the cockpit covers them.
  - Measure (2084) sees the plumes, as it saw the legacy flames.

## 4. Passes and buffers (W×H = view; h = half res W/2×H/2)

| Pass | Kind | Target | Inputs |
|---|---|---|---|
| Atlas bake | IPI PSAtlasBake, once per new texture | tAtlas 512×64 RGBA8: 16 slots of 32×64 (flame strip 16×64, flare 16×16) | exhaust or particle SURFHANDLE |
| Flow maps | vessel meshes, RENDERPASS_FLOWMAP | tFlowRaw[k][front, back] 128² R32F, DS 128² D32 shared | §7.3 |
| Flow prep | IPI PSFlowPrep, PSFlowReduce, PSFlowBlur | tFlowP[k] 128² RGBA16F; tFlowB[k] 64² RG16F; tFlowR[k] 8² then 1² RGBA32F | raw maps |
| Puffs back, front | ShaderClass PuffVS/PuffPS, premultiplied over | tPuff[0], tPuff[1]: h RGBA16F | tPuffData, tDepth, tNoise |
| March | IPI PSPlumeMarch (MRT) | tVolP h RGBA16F premult; tVolPD h R32F transmittance-weighted distance | tDepth, tNoise, tAtlas, tFlowP/B/R, uniforms |
| Blur | IPI PSPlumeBlur, 3×3 depth-weighted | tVolP2 h RGBA16F | tVolP, tVolPD |
| Composite | IPI PSPlumeComposite, full res, blend ONE / ONE_MINUS_SRC_ALPHA into the target | pBackBuffer (MSAA or the offscreen RGBA16F) | tVolP2, tVolPD, tPuff[0..1], tDepth, VolumeTex, VolumeDist |

- **Other textures:**
  - tNoise: 32³ RGBA8 from `VolCloud::ShapeNoise(32, …)` (VolCloudMap.cpp:154), our own tiling Perlin-Worley. It is built on a GfxPool NOISE job, and until it arrives there are no claims (legacy drawing).
  - tPuffData: 256 × ceil(4n/256) RGBA32F, uploaded each frame (VkTex::Upload, VkCore.h:97), at most 8192 puffs.
- **Memory at 1080p:** about 15 MB (four h RGBA16F, two h R32F) plus about 0.6 MB of small textures.
- **State:**
  - Each pass saves VkDev::State. The main target is re-pushed and popped at the end, as SceneFx::AfterOpaque and PostLook::Haze do (PostLook.cpp:184-186).
  - Premultiplied blend: IPI `Execute(2, true)` after `dev->SetBlendFunc(ONE, ONE_MINUS_SRC_ALPHA)`. IProcess.cpp:282-289 keeps the device's blend function for blendop ≠ 1.
  - ShaderClass `Setup(…, false, 2)` (D3D9Util.cpp:1847-1851). Check that its alpha factors give A' = As + Ad(1 − As).
- **Resolution:**
  - The march and the puffs run at half resolution.
  - The half-res depth of a texel is the nearest of its 4 full-res tDepth texels, so a plume never leaks behind a thin foreground edge.
  - The composite does a joint bilateral 2×2 upsample (Kopf 2007), weighting each tap by full-res tDepth against the tap's representative depth.

## 5. Plume model (per exhaust; CPU in PlumeMath, field in Plume.glsl)

### 5.1 Inputs and estimates (cached per vessel and exhaust index; refreshed when `es.th` or the class changes)
- **From the API:**
  - EXHAUSTSPEC: lpos, ldir, lsize, wsize, lofs, level, modulate, tex, th (OrbiterAPI.h:593-605).
  - Ambient pressure `p_a` = VESSEL::GetAtmPressure().
- **Exit area [D].** Orbiter's thrust is F(p) = F0·(1 − pfac·p) with pfac = (Isp0 − Isp_ref)/(p_ref·Isp0) (Vessel.cpp:1338, 8259). The pressure loss is therefore A_e·p, so:
  - `A_e = F0·φ/p_SL`, with φ = 1 − GetThrusterIsp(th, 101325)/GetThrusterIsp0(th);
  - Atlantis SSME (Atlantis.h:42, 52-53) gives D_e 2.33 m (real 2.30 m);
  - the SRB (Atlantis.h:125-129) gives 4.26 m (real 3.80 m).
- **Nozzle radius:** `r_e = clamp(sqrt(A_e/π), 0.3·wsize, 1.5·wsize)` when φ > 0.02, else 0.6·wsize [V]. wsize is the legacy flame half-width.
- **Exit pressure:**
  - `p_e = clamp(0.05·p_SL/φ, 3, 120) kPa` [D]. Ideal nozzles put 3-7 % of vacuum thrust in the pressure term p_e·A_e; the estimate is good to about ±50 %.
  - Without φ: 40 kPa if F0 > 300 kN, else 8 kPa [V].
  - A cfg `pe=` value wins.
- **Mach numbers:**
  - Chamber pressure p_c = 7 MPa [V] and γ = 1.2 [P, typical combustion products].
  - M_e is found from p_c/p_e, and the fully expanded jet Mach M_j from p_c/p_a, by the isentropic relation (Anderson 2003).
  - SSME at sea level (p_e 25.5 kPa): M_e 3.94, M_j 3.20.

### 5.2 Shape (plume frame)
- **Frame:** origin `ref = lpos − ldir·lofs`, axis `a = −ldir`, x along a, r radial. Both are taken to camera-relative space through vVessel::MWorld, as PostLook::Exhausts does (PostLook.cpp:93-156).
- **Regime:** n = p_e/p_a (∞ in vacuum) and `w_atm = smoothstep(0.03, 0.3, p_a/p_e)` [V]. 1 is a jet in air; 0 is a vacuum plume.
- **Jet in air:**
  - `r_j = r_e·sqrt(A(M_j)/A(M_e))`, from the isentropic area ratio [P]. It is below 1 when over-expanded (neck) and above 1 when under-expanded (bulge). SSME at sea level: r_j/r_e = 0.59.
  - Shock-cell length `L_s = 1.306·sqrt(M_j² − 1)·2r_j` [P Pack 1950]. SSME at sea level: 2.3 D_e.
  - Radius: `R(x) = r_j + (r_e − r_j)·exp(−x/(0.5 L_s)) + 0.06 r_j·sin(2πx/L_s)·exp(−x/(4L_s)) + 0.094·max(0, x − L_c)`.
    - The 0.094 is the round-jet spreading rate [P Pope 2000].
    - L_c = 4·L_s [V] (visible cells).
- **Vacuum (Simons 1972):**
  - `ρ_v = (r_e/(r_e + d))²·cos^(2/(γ−1))(min(πθ/(2θ_∞), π/2))`, with d the distance from the exit centre and θ the angle off the axis.
  - `θ_∞ = ν_max − ν(M_e)` (Prandtl-Meyer). For γ = 1.2 and M_e = 3.94 this is 121°. The cos¹⁰ profile gives 0.46 at 30° and 0.03 at 60°: a wide faint cone.
- **Blend:** ρ = mix(ρ_v, ρ_jet, w_atm), with ρ_jet = exp(−(r/R(x))⁴).
- **Visible length:** `L = lsize·(0.4 + 0.6·level)·(1 + 1.5·(1 − w_atm))` [V]. The authored lsize keeps its scale, the plume grows with level and grows longer in vacuum.

### 5.3 Emission, absorption and classes
- **Terms at a sample:**
  - Core: `e_c = I·ρ·exp(−x/(k_L·L))`.
  - Diamonds (jet only): `e_d = I·A_d·w_atm·exp(−x/L_c)·smoothstep(0.9, 0.4, |u| + r/R)`, with u = 2·fract(x/L_s − 0.5) − 1 (L1 diamonds centred at (k + ½)L_s) [V]. The amplitude is A_d = smoothstep(0.1, 0.7, |ln n|)·k_d [V].
  - Mach disk, when n < 0.4 or n > 4: a thin disc at x_md = 0.55·L_s, `exp(−((x − x_md)/0.06r_j)²)·smoothstep(0.6, 0.3, r/r_j)` [V].
  - Nozzle glow, seen from behind: `(−dir·a)⁴`-weighted disc at x ≈ 0 [V]. For the texture class it is the atlas flare quadrant (the legacy flare quad's uv, D3D9Effect.cpp:186-189).
  - Absorption and scattering: σ_a (soot) and σ_s (alumina, smoke) per class.
  - Smoke tail, in air only: a column beyond L of radius R(L) + 0.094(x − L), density `exp(−(x − L)/L)` out to 3L. It is scaled by SMOKE, and by 0.3 when the vessel has its own diffuse stream [V]. It is lit by the sun (HG g 0.5, with an analytic self-shadow through the tail's cylinder) plus the ambient.
- **Integration:** front to back with Hillaire's energy-conserving step. Soot and smoke hide the core behind them; hydrolox stays see-through.
- **Classes**, decided in this order (first match wins): cfg → th NULL → Isp0 > 5000 or < 2300 m/s gives texture → F0 < 50 kN and Isp0 < 3500 gives hypergolic → 4000-5000 hydrolox → 3450-4000 methalox → 2900-3450 kerolox → 2300-2900 solid.
  - Check: SSME 4442 → hydrolox; OMS 3099 at 26.7 kN → hypergolic; SRB 2639 → solid; F-1 2982 → kerolox; RL10 4560 → hydrolox; DG Isp 4e4 (DeltaGlider.h:62) → texture.

| Class | Core (linear RGB × gain) | Diamonds / disk | σ_a, σ_s | k_L | Tail |
|---|---|---|---|---|---|
| hydrolox | (0.30, 0.40, 1.00)×0.06 | (1.0, 0.85, 0.65)×0.8 / ×1.5 | 0, 0 | 0.4 | none |
| methalox | (0.35, 0.45, 1.00)×0.15 | (1.0, 0.55, 0.75)×0.7 | 0, 0 | 0.5 | none |
| kerolox | Planck 2100 K ×2.5 | (1.0, 0.80, 0.50)×1.0 | soot high at the edge and the tail | 0.8 | grey (0.55, 0.52, 0.50) |
| hypergolic | (1.0, 0.45, 0.25)×0.3 | ×0.3 | low | 0.6 | brown (0.6, 0.4, 0.25), thin |
| solid | Planck 2600 K ×4 | ×0.3 | σ_s high, albedo 0.9 | 1.0 | dense white |
| texture | atlas strip: u from r/R, v from x/L (the legacy uv: u 0.01-0.24, v 0 at the nozzle, D3D9Effect.cpp:182-185); colour = rgb·a | none | 0 | 1.0 | none |

- **Gains and colours:** all gains are [V]. Planck colours are [D], from PlumeMath::Blackbody (Planck's law with the Wyman 2013 CIE fit).
- **Intensity:** `I = level'·k_class·w_lod`. Kerolox at full level lands at about 1.0 on display after the curve, like the legacy flame.

### 5.4 Ground impingement
- **CPU:**
  - The vessel-frame up vector is up = tmul(R_vessel, unit(gpos − planet)).
  - The nozzle height is `h = GetAltitude(ALTMODE_GROUND) + dot(ref, up)`.
  - With c = dot(a, −up): if c > 0.2, x_g = h/c. If x_g < 1.5 L, the plume impinges, and the ground plane (normal and offset, camera-relative) goes into the uniforms.
- **Shader:**
  - The axial density fades over 0.3 r_j past x_g.
  - A wall jet on the plane takes over: `ρ_w = ρ_axis(x_g)·exp(−z/h_w)·exp(−s/R_w)`, with h_w = r_j·(0.5 + 0.073·s/r_j) [P: wall-jet half-thickness growth of about 0.073, Launder & Rodi 1981] and R_w = 0.8(L − x_g) + 2r_j [V].
- **Effect:** a hovering DG or a pad launch spreads its flame along the ground and never cuts it.

### 5.5 Animation (TAA-friendly)
- **Clock:** the wall clock, as the haze uses it: `oapiGetSysTime() mod 3600`, with whole repeats.
- **Turbulence:**
  - Noise coordinates `(x − U t, r cos φ, r sin φ)/s(x)`, with s(x) = 0.8r_e + 0.25x and U = 25 r_e/s [V].
  - Two octaves of tNoise erode the density by `saturate((n − (1 − ρ)·0.6)/0.4)`, weighted toward the edge and downstream of 0.2L [V].
- **Flicker:** `level' = level·(1 − es.modulate·m(t))`, where m is smooth value noise at 12 Hz with a per-plume seed. The add-on's modulate is honoured with no per-frame white noise.

### 5.6 Level of detail and limits
- **Crossfade by the projected nozzle radius** r_px = r_e·P._22·H/(2d), at full resolution [V]:
  - below 1 px, legacy only;
  - 1-3 px, a crossfade: the legacy level ×(1 − w) and the volume ×w;
  - above 3 px, the volume only.
- **Count:** at most 32 plumes, the nearest by r_px·level.
- **Clustering** (Starship-like stacks): exhausts of one vessel cluster when |a_i·a_j| > 0.98, their refs are less than 3·max r_e apart, and they share a class. A cluster becomes one plume at the level-weighted centroid with r_e = the cluster radius [V]. What is still left over is drawn legacy.
- **Culling:** a capsule bound (ref, ref + a·3L, R_max, plus the wall-jet disc) against the frustum planes, from Camera.mView and the aperture (no jitter). Vessels that are not visible are included when their bound is in view.

## 6. Smoke and steam puffs

### 6.1 Sources
- **Streams:** every D3D9ParticleStream with `diffuse` set and particles alive. That is ExhaustStream and any add-on stream (Particle.h:130).
- **Ground clouds:** PostPlume's own (§6.6).
- **Not puffs:** EMISSIVE streams (DG exhaust_main, Atlantis srb_exhaust) stay legacy billboards, drawn over the composite with the hardware Z test.

### 6.2 Per-puff data (CPU)
- **Geometry:** the centre is `pos − Camera.pos` and the radius is p->size.
- **Opacity:** `a = max(0.1, alpha0·(1 − age·ipht2))·0.35`. This is the legacy formula (Particle.cpp:462, 520) times a density scale [V].
- **Variation:** seed = texidx, plus age.
- **Albedo:** the atlas mean of the stream texture, normalised to luminance 0.85, so an add-on's tint carries over. Ground clouds use their class colour.
- **Layer:** each puff is back or front of the nearest big plume's midpoint, so the composite can put the plume between the two layers (§8).

### 6.3 Lighting (CPU, per puff into the data)
- **Sun and ambient:**
  - Sun colour Cs and ambient Ca come from the owner vessel's sunLight (VVessel.cpp:1514-1517: GetObjectAtmoParams × terrainSun), through a new `vObject::GetSunLight()`.
  - Ground clouds use the proxy planet's GetObjectAtmoParams at the cloud centroid.
- **Column shadow:**
  - T_col comes from a per-group deep opacity map on the CPU (Kim & Neumann 2001): 48×48 cells in sun space.
  - Puffs are sorted toward the sun. Each one reads the accumulated τ, then splats its own (4/3)·σ0·a·R over its disc.
  - The cost is O(n log n), well under 0.3 ms for 6000 puffs.
- **Plume light:** `E_p = Σ over the 4 brightest plumes of col_p·I_p·L_p·r_e,p/(d² + (0.5L_p)²)` [V]. At night the smoke is lit orange from below.
- **Haze:** Tr and In come from the same D3D9Sun (aerial perspective).

### 6.4 PuffVS
- It fetches 4 texels by `gl_VertexIndex/4` (vertex decl pVector4Decl).
- It builds a camera-facing quad of half-size `R/sqrt(1 − (R/D)²)`, which bounds the sphere's silhouette.
- If the camera is inside (D < 1.05R) it emits a clip-space full-screen quad, at most 16 of them per frame [V].

### 6.5 PuffPS (half resolution)
- **Soft intersection:** the ray-sphere chord, clipped to [near, D_scene], where D_scene is the nearest of the 2×2 tDepth texels.
- **Density:** the parabolic profile 1 − r²/R², integrated exactly over [u0, u1]: `(u1 − u0)(1 − d²/R²) − (u1³ − u0³)/(3R²)` [D].
- **Opacity:** τ = σ0·a·I·(0.6 + 0.8n), where n is one tNoise tap at the chord midpoint in puff space, rotating slowly with age [V]. Then α = 1 − e^(−τ).
- **Colour:** `C = albedo·(Cs·T_col·T_self·HG(cosθ, 0.35) + Ca·(0.75 + 0.25·μ_up)) + E_p` (Umenhoffer 2006 spherical billboards; Henyey & Greenstein 1941).
  - T_self = exp(−0.5·σ0·a·chord to the sun from the midpoint).
  - The lit terms × e (fExposure), as the meshes take gExposure.
- **Haze:** C·Tr + In.
- **Volume clouds:** if VolumeTex exists and the puff lies beyond the cloud distance (tVolD, or the cloud-shell distance), α × (1 − α_cloud).
- **Tone curve:** x(1 + x⁴)^−¼, unless _LIGHTGLOW.
- **Output:** premultiplied, back to front (CPU sort), in batches of at most 4096 quads (16-bit indices).

### 6.6 Ground-cloud emitter (PostPlume::Update, sim time)
- **When:** for each impinging plume with p_a > 10 kPa and the vessel less than 300 m above the ground.
- **Spawning** [V]:
  - rate 6/s × sqrt(F0·level/1 MN);
  - at most 384 live puffs per vessel;
  - placed at the impingement point plus a random radial offset.
- **Motion** [V]:
  - radial velocity along the ground 0.15·Isp0, at most 60 m/s;
  - buoyancy 3 m/s;
  - drag exp(−dt/4 s) toward the wind (oapiGetWindVector, as Particle.cpp:597-602).
- **Size and life** [V]: size from 0.5·r_j, growth 3 m/s, life 30 s.
- **Colour** [V]: steam (0.95, 0.95, 0.95), solid (0.92, 0.90, 0.88), kerolox (0.85, 0.82, 0.78), dust (0.62, 0.55, 0.45). The GROUND cfg key picks steam, dust or none.
- **Time jumps:** dt > 1 s or a focus change clears the clouds.

## 7. Re-entry plasma

### 7.1 Which vessels and how bright
- **Candidates:** owners of a ReentryStream in Scene's pstream list (Particle.h:145).
  - This follows Orbiter's particle setting (Vessel.cpp:2335) and add-ons that removed the stream.
  - The vessel must also have GetAtmDensity() > 0.
- **Energy flux:** `Φ = GetDynPressure()·GetAirspeed()`, the free-stream kinetic energy flux ½ρV³.
- **Intensity:** `I = smoothstep(6.0, 7.0, log10 Φ)·smoothstep(3, 6, V km/s)` [V]. Calibration:
  - Onset is about 85 km at 7.6 km/s. That is where RAM-C II crossed the S-band threshold (84 km, TN D-6062) [U model/README], and where Orbiter's default stream starts (ATM_PLOG 2e8 on 0.5ρ^0.6V³, Vessel.cpp:2351-2360).
  - I ≈ 1 by 75 km.
  - The glow fades out below about 3.5 km/s, near 55 km.
- **Limits:** at most 4 vessels. The 2 largest on screen (at least 48 px) get flow maps; the others get the ellipsoid (§7.5).
- **Docked and attached stacks:** the rigid set, as in SceneFx::FlagPass (SceneFx.cpp:301-318), is one primitive. The members are drawn together into its maps, and the other members get no plasma of their own.

### 7.2 Flow frame
- `GetAirspeedVector(FRAME_LOCAL, v)` gives f̂ = −unit(v), the downstream direction in vessel axes. It is taken to render space through mWorld's rotation.
- The Mach number from GetMachNumber() gives the Mach angle μ = asin(1/M) [P].

### 7.3 Flow maps (camera target every frame, the second vessel every 2nd frame)
- **Projection:** ortho over the bounding sphere (camera-relative centre c, radius 1.15·R_b), near 1, far 2R_b + 10.
  - Front map: the eye at c − f·(R_b + 5), looking along +f.
  - Back map: looking along −f.
- **New `Scene::RenderFlowMap(vVessel*, const D3DXMATRIX &vp, VkSurf *rt, VkSurf *ds)`**, modelled on RenderShadowMap (Scene.cpp:2575-2667):
  - set mFlowVP;
  - BeginPass(RENDERPASS_FLOWMAP);
  - PushRenderTarget, clear the target to 0 and the depth to 1;
  - each member's Render(dev, false);
  - pop.
  - smap and smapSerial are not touched, so deferred PBR shadows stay valid.
- **vVessel::Render in the FLOWMAP pass:**
  - exterior meshes only (as SHADOWMAP at VVessel.cpp:683);
  - `mesh->RenderShadowMap(pWT, scn->GetFlowVP(), 0)` (beside :794); ShdMapPS writes 1 − z (NewMesh.glsl:124-127).
  - The terrainSun early-out (:664) tests SHADOWMAP only, so night entries keep their map.
- **PSFlowPrep** writes zs and zb in metres (value 0 means no surface), the coverage, and `w = (n·f)² = 1/(1 + |∇zs|²)`. This is modified Newtonian Cp/Cp_max [P Lees 1955]; it is 0 at the sides and the lee.
- **PSFlowReduce** (128² → 8² → 1²) finds:
  - z_min, the stagnation depth;
  - zb_max, the base;
  - the frontal area A_f, from the coverage;
  - the stagnation uv.
- **PSFlowBlur:** a separable 9-tap blur of (cov, zs·cov) into the halo envelope.

### 7.4 Field (sample to map coordinates u, v, z)
- **Shock-layer band (analytic).**
  - Along the march, d = zs(u,v) − z changes sign where the ray crosses the windward surface. This works from outside (+ to −, then tDepth stops the ray) and from inside the cockpit (− to +).
  - At each crossing, add `E_b = c·I·w·S(ℓ)·λ/|f·dir|`, capped at 8λ [D: the integral of e^(−d/λ) across a layer crossed at that angle].
  - λ = max(δ, λ_min), with δ = δ0 + 0.05ℓ [V] and δ0 = 0.78·ε·R_n [U envelope.py].
  - ε = (γ−1)/(γ+1) = 0.105, with γ 1.2351 [U: chosen there to reproduce Scott's stagnation pressure, NTRS 19840002087].
  - R_n = clamp(0.25·sqrt(A_f/π), 0.3, 3) m [V], and λ_min = max(0.01·R_b, 1.5 px at that distance) [V].
  - Running length ℓ = zs − z_min + |uv − uv_stag|·E. The heating shape is `S(ℓ) = (max(ℓ, ℓ_f)/ℓ_f)^−0.5`, with ℓ_f = 0.1R_n [U heat.py: Cp^0.5·s^−b, laminar b ≈ 0.5].
  - Front: × smoothstep(ℓ_front + 0.1L_b, ℓ_front − 0.05L_b, ℓ), with ℓ_front = L_b(0.15 + 0.85I) + 0.1L_b·wob(t). The ionisation front walks aft as heating rises, and the front itself fluctuates [U patch_unsteady.py; the amplitude is not measured there, so it is V].
- **Halo:** `A_h·I·cov_b·w_b·exp(−|z − zs_b|/λ_h)`, with λ_h = 0.08R_b and A_h = 0.3 [V]. It wraps a soft glow past the silhouette edges and shows through the VC windows.
- **Wake:** for z > zb_max, in the base outline (blurred coverage of the texels with zb > zb_max − 0.1L_b), widening with tan μ:
  - near wake: `A_n·exp(−(z − zb_max)/D_b)`, with D_b the base diameter [U patch_wake.py: TN D-4118 and D-6617 support the base region only];
  - far trail: `A_t·exp(−(z − zb_max)/(V·0.05 s))` with A_t = 0.15 [V]. It is not evidenced, so it has its own low gain.
- **Lee side:** no term. w is 0 where the surface faces downstream [U newtonian/README: lee Cp 0, lee heating 10⁻³-10⁻² of stagnation, vortex cores dark].
- **Colour:**
  - `c(V)` is piecewise-linear over the user's table [U model/README, computed with NASA CEA, NIST ASD and LIFBASE-derived bands], clamped outside it:

    | V (km/s) | linear RGB |
    |---|---|
    | 5.00 | (1, 0.19, 0) |
    | 7.00 | (1, 0.26, 0) |
    | 7.62 | (1, 0.28, 0) |
    | 9.00 | (1, 0.36, 0.06) |
    | 10.0 | (1, 0.47, 0.55) |
    | 11.0 | (0.75, 0.39, 1) |
    | 12.0 | (0.44, 0.25, 1) |
  - The hot core whitens: mix(c, 1, 0.25w⁴) [V].
  - In a non-air atmosphere (ATMCONST R more than 15 % from 287) the colour is (0.85, 0.6, 1.0) [V] (§15).
- **Intensity and flicker:** a gain of 3 [V], emission only (optically thin, so it adds light), and a brightness flicker of 1 ± 0.2 from smooth 3-6 Hz noise on the wall clock [V].

### 7.5 Ellipsoid fallback and small sizes
- **Fallback:** the hull box as an ellipsoid in the flow frame. The hull box is a new `vVessel::HullBox`, copied in UpdateBoundingBox before the exhaust points are added (before VVessel.cpp:1866). zs, zb and w come from the ellipsoid analytically, through the same field code.
- **Below 2 half-res px:** λ and λ_h are inflated to 1.5 px, and the intensity is scaled by (true/inflated)². A ground observer sees a glowing point and a trail [V].

### 7.6 Legacy re-entry stream
- **Default stream:** ReentryStream's constructor marks Orbiter's default by its fixed fields: rate 15, v0 0, spread 0.01, life 0.5, growth 3, slowdown 3, EMISSIVE, LVL_FLAT 1 1, ATM_PLOG 2e8 1e11, tex NULL (Vessel.cpp:2351-2360). Claims() hides it while that vessel's plasma primitive is active.
- **Custom streams:** these stay legacy, and they switch off our far trail. The sheath and the near wake are kept.

## 8. Shaders (shaders/Plume.glsl, new, compiled only with VolPlumes = 1)
- **IPI entries:** PSPlumeMarch, PSPlumeBlur, PSPlumeComposite, PSFlowPrep, PSFlowReduce, PSFlowBlur, PSAtlasBake.
- **ShaderClass:** PuffVS / PuffPS.
- **ppf macros:**
  - `_LIGHTGLOW` when pOffscreenTarget exists: no curve, and the HDR plume blooms in LightBlur;
  - `_PLMCLOUD` when VolClouds is on: the cloud occlusion.
- **Uniform block `PlumePS`, binding 1** (IPI's VS block is 0, as in PostLook.glsl:9):
  - vCamX, vCamY, vCamZ: SceneFx::CameraRays (SceneFx.cpp:384), unjittered here;
  - vTex: 1/W, 1/H, time s, plume count;
  - vFrame: near, e, flags, plasma count;
  - vSun;
  - vPal[24]: 6 classes × 4;
  - vPl[32·7]: origin and r_e; axis and L; r_j/r_e, θ_∞, L_s, x_md; class and atlas slot, I, w_atm, A_d; ground plane; haze Tr and smoke; In and seed;
  - vPz[4·8]: flow axes, centre, extent, map index, colour and I, δ0, λ, λ_h, D_b, trail, tan μ, ℓ_front, ellipsoid axes;
  - vLight[8].
  - About 4.7 KB in total.
- **Samplers:**

  | Binding | Sampler |
  |---|---|
  | 4 | tDepthP |
  | 5 | tNoiseP (3D) |
  | 6 | tAtlasP |
  | 7, 8 | tFlowP0, tFlowP1 |
  | 9, 10 | tFlowB0, tFlowB1 |
  | 11, 12 | tFlowR0, tFlowR1 |
  | 13 | tVolP |
  | 14 | tVolPD |
  | 15 | tPuffBk |
  | 16 | tPuffFr |
  | 17 | tCloudV |
  | 18 | tCloudD |
  | 19 | tPuffData (VS) |
  | 20, 21 | tSrcP, tSrc2P (bake and prep inputs) |
- **PSPlumeMarch:**
  - The ray is VolCloud's PixelRay convention (VolCloud.glsl:136-139), in our own copy. D_scene is the min of the 2×2 tDepth texels, with 0 meaning ∞.
  - Each plume bound and plasma box is intersected, and the 4 nearest intervals are kept.
  - Steps per interval: clamp(length/(0.6r_e or λ_h), 6, 24), at most 48 per pixel.
  - The start offset is IGN (Jimenez 2014) per pixel with **no frame term**.
  - Hillaire 2015 step, early exit at T < 0.01. The plasma band uses the analytic crossings.
  - Aerial perspective of the nearest primitive: `C = C·Tr + (1 − T)·In`.
  - The curve unless _LIGHTGLOW; emission × 1, lit smoke × e.
  - Out: (C, 1 − T), plus the transmittance-weighted mean distance.
- **PSPlumeComposite:**
  - bilateral 2×2 upsample of V, Pf and Pb;
  - volume-cloud occlusion of V when the cloud distance (tVolD, else the cloud shell along the ray) is nearer than tVolPD;
  - `C = Pf + (1 − αf)·(V + (1 − αv)·Pb)`, with α the product rule.
- **No change** to D3D9Client.fx, Particle.fx, NewMesh.glsl or any other existing shader.

## 9. Data and where it comes from

| Item | Source |
|---|---|
| exhaust geometry, level, modulate, tex, th | VESSEL::GetExhaustCount/Spec/Level (OrbiterAPI.h:593-605) |
| F0, Isp0, Isp at sea level | GetThrusterMax0, GetThrusterIsp0, GetThrusterIsp(th, 101325) (VesselAPI.h:2538, 2600, 2636) |
| p_a, ρ, q, M, V, airspeed vector | GetAtmPressure, GetAtmDensity, GetDynPressure, GetMachNumber, GetAirspeed, GetAirspeedVector(FRAME_LOCAL) (VesselAPI.h:1252-1356) |
| ground height, wind | GetAltitude(ALTMODE_GROUND), GetSurfaceElevation, oapiGetWindVector |
| sun, ambient, haze at a vessel | vObject::sunLight (VObject.h:254, filled at VVessel.cpp:1514-1517) through a new GetSunLight() |
| air or not, γ | oapiGetPlanetAtmConstants(hPlanet): R, gamma |
| world matrix, hull box, bounding sphere | vVessel::MWorld (VObject.h:106), HullBox (new), GetBoundingSphere* (VObject.h:135-137) |
| particles | D3D9ParticleStream accessors (new): Diffuse(), First(), Alpha(p, t), Texture(), Owner(), IsDefaultReentry() |
| scene depth | SceneFx tDepth (camera distance, 0 none) through the new DepthAny() (§10) |
| volume clouds | SceneFx::VolumeTex(), VolumeDist() |
| exposure, camera | Scene::fExposure, Camera (friend), SceneFx::CameraRays |

## 10. C++ changes per file
- **PostPlume.h/.cpp (new):**
  - ctor: targets, IPIs, ShaderClass, a GfxPool noise job, and Config/GC/Plume reading. Each failure is logged and turns the feature off (no claims, so legacy drawing).
  - It logs `PostPlume: march 1, puffs 1, flow 1` once.
  - Methods: Collect, Update, Render, Claims, LegacyWeight, EndFrame, and `Segments()` for PostLook.
  - Private parts: the atlas LRU of 16 slots by SURFHANDLE, flow maps, clusters, the deep opacity map and the ground clouds.
- **PlumeMath.h/.cpp (new, pure):** §17.
- **Scene.h:**
  - `friend class PostPlume;` after :78.
  - `#define RENDERPASS_FLOWMAP 0x000A` after :57.
  - Members `class PostPlume *pPlume = nullptr; D3DXMATRIX mFlowVP;` near :529.
  - `const D3DXMATRIX *GetFlowVP() const`, `void RenderFlowMap(…)`, `float PlumeLegacyWeight(vVessel*, DWORD) const`.
- **Scene.cpp:**
  - Create after :348 when Config->VolPlumes, and delete beside :381.
  - The hooks U, C, A, R, P, E of §3, plus RenderFlowMap beside :2575.
  - RenderSecondaryScene (2739-2820) is unchanged.
- **SceneFx.h/.cpp:**
  - `AfterOpaque(…, float plumeFar = 0)` (:29, :263).
  - InitDepth also when Config->VolPlumes (:37).
  - At :274, the early exit also requires `plumeFar <= 0`.
  - DepthPass far plane (:289): 1e6 when SSAO, clouds or TAA run; otherwise min(1e6, plumeFar). From orbit this cuts the terrain beyond the plumes.
  - It records `depthFull`. `Depth()` keeps returning the full-range depth only, so PostLook's sky tests (shafts, flare) never see a short-range tDepth. The new `DepthAny()` serves PostPlume.
- **VVessel.h/.cpp:**
  - RenderExhaust (:964-985): `w = scn->PlumeLegacyWeight(this, i)`. At 0 it skips the exhaust, but still updates ExhaustLength. Between 0 and 1 it passes a local level × w through es.level.
  - Render: FLOWMAP added to the no-cockpit condition (:683) and a FLOWMAP branch beside :794. Check the tail of Render for main-pass-only work (DeferPbr is PHASE_OPAQUE only).
  - UpdateBoundingBox: `HullBox = BBox` just before the exhaust points (:1866). HullBox is a public member.
- **VObject.h:** `const D3D9Sun *GetSunLight() const { return &sunLight; }` in the public section.
- **Particle.h/.cpp:**
  - Accessors: Diffuse, First, Alpha, Texture, Owner.
  - ReentryStream gets `bool bDefault`, set in its constructor (Particle.cpp:765).
  - The legacy Render, Update and RenderGroundShadow are unchanged.
- **PostLook.h/.cpp (the look owner):** Exhausts() (:93-156) uses `scn->pPlume->Segments()` when it exists. Each segment carries ref, end and the plume radius at both ends, so the haze matches the visible plume. Otherwise it keeps its own walk.
- **Config and dialog:** D3D9Config.h/.cpp, resource.h, D3D9Client.rc, VideoTab.cpp (§2).
- **CMake:**
  - OVP/VulkanClient/CMakeLists.txt after :80: PostPlume.cpp, PlumeMath.cpp.
  - Tests/CMakeLists.txt after :225: `add_unit_test(PlumeMath.Test …/PlumeMath.cpp)` with the include directory.

## 11. Add-on compatibility and the legacy path
- **Exhausts:**
  - Every vessel type (legacy, PBR/glTF, XR, Lua) goes through the same EXHAUSTSPEC API.
  - lsize, wsize, level and modulate drive the volume.
  - An add-on texture is used as-is for the texture class (the atlas strip and flare uv), and it tints the puffs.
- **Particle streams:**
  - DIFFUSE streams keep their spec: size, growth, life, level and atmosphere mapping (the alpha0 path), and their texture as a tint. Only the drawing changes.
  - EMISSIVE streams and custom re-entry streams are unchanged.
  - Particle ground shadows (Scene.cpp:3149) are unchanged.
- **Legacy kept for:**
  - tiny or far exhausts (§5.6) and plumes beyond the 32 limit;
  - secondary scenes (custom camera MFDs and env maps);
  - any IPI failure, and the time until the noise texture arrives.
- **Switch off:** byte-identical behaviour and SPIR-V (§0).

## 12. Cost (target: 1080p, RTX 3060-class desktop NVIDIA)

| Case | Target GPU | Notes |
|---|---|---|
| nothing active | 0 ms | CPU Collect < 0.05 ms |
| DG main engines at KSC, close (2 plumes, ~25 % of the screen) | ≤ 0.6 ms | march 0.35, blur 0.05, composite 0.12 |
| Atlantis launch at T+5 s (5 plumes, 2 SRB contrails of up to 6000 puffs, ground cloud) | ≤ 1.8 ms | puffs ≤ 0.8; CPU sort, opacity map and upload ≤ 0.5 ms |
| re-entry chase (1 vessel with maps) | ≤ 0.6 ms | flow maps 0.1 (two 128² mesh draws), prep 0.02 |
| plumes only, TAA/SSAO/clouds off, in LEO | + forced DepthPass ≤ 0.3 ms | the far plane is cut to plumeFar |

- **How to measure:**
  - Vulkan timestamp pairs (vkCmdWriteTimestamp) around the flow maps, puffs, march, blur and composite, under a temporary `PLUME_TIMING` define, logged as a 120-frame mean. The vshd agent did the same and then removed it.
  - std::chrono around Collect and Update.
  - The mean frame interval as a cross-check.

## 13. Interplay with the existing effects
- **TAA:**
  - The passes run after the resolve on the unjittered projection, so the plume is never in the history.
  - tDepth was drawn jittered, at most 0.5 px off at hull edges, which the bilateral weights absorb.
  - The noise is static per pixel and the motion is smooth in time.
- **SSAO, VolShadow:** unaffected; they run before and only see tDepth.
- **Volume clouds:** composited earlier. The plume and the puffs are occluded by VolumeTex when the cloud is nearer (§8). The 2D cloud layer is not in tDepth, so the plumes go over it as the legacy flames do.
- **Heat haze:** shares the exhaust list and uses the plume radius. It runs before our pass, so it bends the background and not the flame.
- **Shafts and flare:** they read Depth(), which stays full-range or NULL (§10). A bright plume against the sky can feed the shaft mask, but only near the sun direction (§15).
- **Water:** no plume reflection, as legacy.
- **Filmic:** emission uses the Metalness curve x(1 + x⁴)^−¼ (M6/M7 scope), not HDR().
- **Exposure:** the lit smoke takes e; the emission does not, like the legacy flames. Measure (Scene.cpp:2084) sees the plumes.
- **LightBlur (PostProcess = 1):** no curve, so the HDR plume and plasma bloom.
- **UE5 batch:**
  - Height fog: either runs before our composite and exposes `FogAt(origin, dir, dist)` for the march under a macro, or skips pixels with tVolPD. One of the two must be agreed.
  - SSR and SSGI: plumes are not reflected or bounced unless their passes run after ours, the same as legacy.
  - Shadows: plumes and smoke cast none. Shadows and this feature both touch vVessel::Render (§18).

## 14. PBR, glTF and XR
- **Opaque PBR and glTF groups** are in tDepth (Mesh.cpp opt 2), so they hide the plumes. BLEND and transmission groups are not, so the plume shows through glass, and RenderPbrDeferred (Scene.cpp:2016) draws the glass over it afterwards.
- **Flow maps** use Mesh::RenderShadowMap opt 0, which already handles PBR groups by alpha mode and skips transmission groups (Mesh.cpp:3045-3105). glTF and XR hulls give real sheaths.
- **XR vessels:** Isp 4e4-class values classify as texture, so their custom exhaust textures are honoured. Scramjet exhausts (if exposed as EXHAUSTSPEC) are treated the same way.
- **Phases:** PbrRender is in PHASE_ALL when R runs (Scene.cpp:1953).

## 15. Risks and open questions
- **Isp heuristic:** fictional or overlapping Isp values misclassify, for example Raptor-SL at about 3400 m/s comes out kerolox. The cfg fixes it, and stock files ship.
- **Puff fill:** big near-camera puffs cost fill. Mitigations: half resolution, the inside-cap (16 per frame), and soft depth.
- **Forced DepthPass in orbit:** costs ≤ 0.3 ms with the far plane cut. It is skipped when nothing is in view.
- **Shared vVessel::Render:** the FLOWMAP branch touches vVessel::Render, which the shadows feature may also change. The owners must coordinate.
- **Light shafts:** a plume against the sky (tDepth 0) can add rays near the sun. Optionally, PostPlume could write its α to a mask the shaft pass reads (open).
- **Wall clock:** turbulence and flicker run in real time under time warp and while paused, as the legacy rand() flicker did. Particle motion stays in sim time.
- **Default re-entry detection:** it relies on Orbiter's fixed default spec. If upstream changes those numbers, the legacy stream shows again (harmless).
- **Open questions:**
  - Own temporal accumulation in the vessel frame (tVolPD and rigid motion, PSTaa-style clamp) when TAA runs, to halve the steps?
  - Should Orbiter's EnableReentryFlames (Config.cpp:651, unused by the core) also gate the plasma?
  - The plasma colour for CO2 atmospheres (Mars, Venus: CN violet and CO bands)? Today it is a [V] constant.
  - Should big plumes add a scene local light, so the pad and vessel turn orange? Many add-ons already add a LightEmitter; it is left out to avoid doubling.
  - Is `Config/GC/Plume/<Class>.cfg` the right place, or one table `Modules/VulkanClient/Plumes.cfg`?
  - Should the far re-entry trail exist at all? The user's own research (patch_wake.py) keeps only the base region. It is kept at gain 0.15 here.

## 16. Checks (scenarios; build-tree Tests/Plume*.scn may be added)
- **Switch off:** identical to the dd4e087c build in:
  - Look DG Engines;
  - Space Shuttle Atlantis/Launch into Sunrise;
  - Demo/Atlantis Ascent AP at T+10 s;
  - Atlantis reentry 1;
  - Delta-glider/DG Mk4 in orbit with the main engine at 50 %.

  Pass criteria:
  - no PostPlume log line;
  - no Plume.glsl compile;
  - pixel-identical outside the exhausts.

  Legacy rand() flicker differs from run to run, so compare with modulate 0 vessels or mask the flames.
- **Plumes on:**
  - Look DG Engines (sea level, texture class): tight plume in the DG colours, no hard ground line.
  - Atlantis on the pad, T+3 s:
    - SSMEs see-through with diamonds and a Mach disk;
    - SRBs bright, with white smoke;
    - a ground cloud spreading;
    - the dawn smoke lit orange from below.
  - DG Mk4 in orbit: a wide faint cone with no diamonds.
  - DG hovering at KSC (hover 60 %, 10 m): the flame spreads along the ground.
  - XR2 "Ready for Takeoff": custom textures honoured.

  Pass criteria:
  - the plume is hidden behind the hull from the front;
  - two frames 1 s apart with modulate 0 change by less than 3/255 in the plume region;
  - no NaN or black pixels.
- **Plasma:**
  - Atlantis reentry 1, chase view: windward glow, a dark lee, a front that walks aft, flicker without strobing.
  - The same scenario in the VC cockpit: orange glow in the windows.
  - DG at 80 km seen from a ground camera: a point plus a trail.
  - Venus descent: the CO2 colour, no errors.

  Pass criteria:
  - onset within ±5 km of 85 km at 7.6 km/s;
  - the default stream hidden, and custom streams kept.
- **Robustness:**
  - TAA 0 and 1;
  - PostProcess = 1 (bloom);
  - MSAA 0 and 4;
  - the 2D panel and the VC;
  - a custom camera MFD (legacy there);
  - Gfx Clouds scenarios with a vessel climbing through the volume clouds (plume hidden behind the cloud);
  - a script vessel with 33 engines (clustering).

  Pass criteria: no validation-layer errors and no shader [ERROR] lines.
- **Timing:** each case in §12 within its target. ctest passes.

## 17. Pure maths (PlumeMath.h/.cpp, Tests/PlumeMath.Test.cpp)
- **PrandtlMeyer(M, γ):**
  - ν(1) = 0;
  - ν(2, 1.4) = 26.38°;
  - ν(∞) = (π/2)(sqrt((γ+1)/(γ−1)) − 1), which is 3.639 rad at γ 1.2.
- **AreaRatio(M, γ):** A/A*(1) = 1 and A/A*(2, 1.4) = 1.6875. **MachFromPressureRatio** is the inverse of p0/p = (1 + (γ−1)/2·M²)^(γ/(γ−1)); M = 2 at γ 1.4 gives 7.824.
- **ExitArea(F0, Isp0, IspRef, pRef):**
  - SSME numbers give D_e within 2 % of 2.30 m;
  - the SRB within 15 % of 3.80 m;
  - pfac 0 gives no area.
- **ExitPressure:** clamped and monotonic in φ.
- **JetState (SSME at sea level):** M_e 3.94 ± 0.02, M_j 3.20 ± 0.02, r_j/r_e 0.59 ± 0.02, L_s/D_e 2.3 ± 0.1. In vacuum M_j → ∞ and w_atm → 0.
- **PlumeRadius(x):** continuous; equal to r_e at x = 0; necks when over-expanded and bulges when under-expanded.
- **Simons(d, θ):** 1 on the axis at d = 0, 0 at θ_∞, and monotonic.
- **Classify:** SSME hydrolox, OMS hypergolic, SRB solid, F-1 kerolox, RL10 hydrolox, DG texture, NULL th texture.
- **Blackbody(T) → normalised linear sRGB:** 6504 K white within 3 %; 2100 K gives r > g > b.
- **PlasmaColour(V):** the table nodes are exact, and it clamps outside them. **PlasmaIntensity:** about 85 km onset at 7.6 km/s with US76 densities, and 0 below 3 km/s.
- **ChordIntegral (parabolic sphere):** matches a 4096-step numeric integral within 1e-4, including clipped chords.
- **DeepOpacity:** a puff behind another toward the sun gets τ > 0; puffs side by side get 0; and the result does not depend on the input order.
- **HG phase:** integrates to 1 over the sphere, within 1e-3.
- **FlowOrtho:** an upstream point maps to depth less than a downstream one; the VP is right-handed as RenderShadowMap's.
- **BandIntegral:** λ/|cos| against numeric integration, and the cap.
- **Cluster:** 33 parallel engines become 1 plume, and two opposite plumes stay 2.
- **ProjectCapsule culling:** behind-camera and outside-frustum rejections.

## 18. Files touched (for splitting ownership with shadows, SSR, SSGI, height fog)
- **Mine (new):**
  - OVP/VulkanClient/PostPlume.h, PostPlume.cpp, PlumeMath.h, PlumeMath.cpp;
  - OVP/VulkanClient/shaders/Plume.glsl;
  - Tests/PlumeMath.Test.cpp;
  - Config/GC/Plume/Atlantis.cfg, SRB.cfg, DeltaGlider.cfg (stock class names to be confirmed);
  - build-tree scenarios Scenarios/Tests/Plume*.scn.
- **Mine (existing):** OVP/VulkanClient/Particle.h, Particle.cpp.
- **Shared, small edits, coordinate with the shadows owner:** OVP/VulkanClient/VVessel.h, VVessel.cpp (RenderExhaust, the FLOWMAP branch in Render, HullBox) and VObject.h (GetSunLight).
- **Integrator:**
  - Scene.h, Scene.cpp (hooks, RenderFlowMap, RENDERPASS_FLOWMAP);
  - SceneFx.h, SceneFx.cpp (plumeFar, depthFull, DepthAny);
  - D3D9Config.h, D3D9Config.cpp, resource.h, D3D9Client.rc, VideoTab.cpp;
  - OVP/VulkanClient/CMakeLists.txt, Tests/CMakeLists.txt;
  - OVP/VulkanClient/doc/VulkanClient.html, README.md, Doc/Orbiter User Manual Linux/NEW.tex, LAUNCHPAD.tex.
- **Look owner:** OVP/VulkanClient/PostLook.h, PostLook.cpp (Exhausts from Segments()).
- **Not touched:** D3D9Effect.cpp, every existing shader file, Mesh.cpp, PbrRender, NewPlanet.glsl and the core (Src/Orbiter).

## Sources (one line each)
- Anderson, Modern Compressible Flow, 3rd ed. (2003): isentropic relations, Prandtl-Meyer function.
- Pack, Q. J. Mech. Appl. Math. 3:173 (1950): shock-cell length 1.306·sqrt(M²−1)·D.
- Simons, AIAA J. 10(11):1534 (1972): vacuum plume density model.
- Pope, Turbulent Flows (2000): round-jet spreading rate about 0.094.
- Launder & Rodi, Prog. Aerospace Sci. 19 (1981): wall-jet growth rate about 0.073.
- Lees, "Hypersonic flow" (1955): modified Newtonian Cp ∝ sin² of the inclination.
- Hillaire, SIGGRAPH 2015 course "Physically Based and Unified Volumetric Rendering in Frostbite": energy-conserving integration step.
- Umenhoffer, Szirmay-Kalos & Szijártó, WSCG 2006: spherical billboards.
- Kim & Neumann, EGWR 2001: opacity shadow maps.
- Henyey & Greenstein, ApJ 93 (1941): phase function.
- Wyman, Sloan & Shirley, JCGT 2(2) (2013): CIE 1931 fits for the Planck colours.
- Jimenez, SIGGRAPH 2014: interleaved gradient noise.
- Kopf et al., SIGGRAPH 2007: joint bilateral upsampling.
- [U] ~/plasma-reentry:
  - model/README.md: velocity colour table, RAM-C II onset, γ 1.2351;
  - newtonian/README.md: dark lee side;
  - solver/heat.py: Cp^0.5·s^−b heating shape;
  - solver/envelope.py: δ0 = 0.78·ε·R;
  - newtonian/patch_unsteady.py: moving ionisation front;
  - newtonian/patch_wake.py: base-region wake, TN D-4118 and D-6617.

  Only data and published relations are used; no code was copied.

## Rev 2 (implementation)
Rev 2 overrides the body where they differ. Branch ue5-batch (HEAD 35e2949c).

Review MUSTs:
- **M1 pass id:** RENDERPASS_FLOWMAP is 0x000B and the label arrays have 12 entries (INT-1, done by the integrator).
- **M2 legacy re-entry billboard:** vVessel::RenderReentry multiplies `ints` by PostPlume::LegacyReentry(this). That is 0 for every member of a plasma primitive drawn by the last Collect (the opaque pass runs before Collect), and 1 otherwise and in every pass other than the main one. The billboard is never drawn twice.
- **M3 DIFFUSE re-entry streams:** ReentryStream::IsReentry() is true. Re-entry streams are never claimed as puffs, so Atlantis' DIFFUSE entry trail and Orbiter's default stream stay legacy.
- **M4 clustering:** PlumeMath::Cluster joins nozzles only when they belong to the same vessel and class, have axis dot > 0.98, and one of these holds:
  - the refs are closer than 1.2(r_i + r_j);
  - both are below 3 px and within 3 radii;
  - the plume count is over the 32 budget (nearest pairs join first).

  A cluster keeps its biggest member's r_e, r_j, L_s and x_md. The envelope offset (P5.z) only widens the outer radius. The SSMEs stay three plumes, and 33 packed engines become one (tests).
- **M5 depth:** INT-4 applies. DepthPass is full range for every reader, and SceneFx::DepthWanted() includes VolPlumes. There is no plumeFar, no DepthAny, and no AfterOpaque argument; PostPlume reads SceneFx::Depth().

Review SHOULDs:
- **S1:** δ0 = 0.78·ε·R_n with ε = 1/16 [U geometry.py]. γ is cited to solve.py.
- **S2:** when the ray ends on the hull, the band is added at the end point if |zs − z| < 1.5·texel/√w plus the pixel footprint. λ_min is at least half a flow texel. zs is a bilinear blend of covered texels only, and coverage must be > 0.5.
- **S3:** JetState clamps p_a ≥ 0.01·p_e. When w_atm < 1e-3 the jet terms are switched off (jet = false, L_s = 0), and the shader selects by L_s > 0. The test checks "finite, w_atm = 0".
- **S4 fog (INT-5 contract):** the CPU calls SceneFx::FogAlongRay(cam, dir, dist) for each plume, and for each puff light sample, to get the transmittance Tf. The fog colour per pixel is FogTex().rgb/a at half res. With no fog, FogAlongRay returns 1 and FogTex returns nullptr, so the fog flag stays off. This is coded against the stub signatures in SceneFxFog.cpp.
- **S5:** the class cfg has `PUFFS 0|1`. By default only diffuse streams with the default texture are claimed. Streams with their own texture need `PUFFS 1`, and then the atlas mean of their texture tints the light.
- **S6 puff cost:** the cap is 32 half-res screens. Over the cap, a hashed subset is kept and drawn denser (σ × 1/√k). At most 16 puffs around the camera and 8192 puffs in all. The pad ground cloud is the measured case.
- **S7 light along the column:** GetObjectAtmoParams is sampled every 32 puffs or 1 km, at most 96 samples per frame, times the owner vessel's terrainSun. A ground cloud is lit at its centroid.
- **S8:** the stability check compares consecutive frames, not frames 1 s apart.

NITs taken:
- **N2:** the Blackbody test checks chromaticity at 6504 K, (0.3135, 0.3236) ± 0.002.
- **N3:** PlasmaIntensity is tested in [0.05, 0.25] at 84-85 km.
- **N5:** the SRB cfg is Atlantis_SRB.cfg.
- **N7:** HullBox is updated by D9UpdateAABB.
- **N8:** the nozzle cache key includes F0, Isp0 and wsize.
- **N9:** the atlas key is handle + VkTex + VkImage.
- **N10:** a tDepth value of 0 counts as far.
- **N11:** the band uses √w.
- **N12:** the far trail gain A_t = 0, and the legacy default stream stays as the far trail (no bDefault flag).
- **N15:** with Filmic the lit smoke uses the planet's ACES fit; otherwise it uses the vessels' x(1+x⁴)^−¼.

Integration decisions that change the design:
- **Hooks:** the integrator calls Collect before AfterOpaque and Render right after PostLook::Haze (INT-5). The ground clouds step inside Collect in sim time, so there is no Scene::Update hook.
- **Claims without Scene.cpp edits:**
  - D3D9ParticleStream::Render skips a stream that PostPlume claimed for this frame, in the main pass.
  - vVessel::RenderExhaust scales es.level by PostPlume::LegacyWeight: 0 skips the exhaust, 1 leaves it legacy.
  - Render revokes all claims when tDepth is missing.
  - EndFrame and Claims() are not needed.
- **Flow maps:** they are rendered inside PostPlume (a friend of Scene: BeginPass, PushRenderTarget). vVessel's FLOWMAP branch takes its view-projection from the static PostPlume::FlowVP(). Scene.h gets no new members, and vVessel declares `friend class PostPlume` (for HullBox and terrainSun).
- **Bindings:** Plume.glsl is a new IPI file. It uses block PlumePS at binding 1 (IPI.glsl's VS block is 0), and the ShaderClass blocks PuffVSB 0 and PuffPSB 1. Samplers are 4-22 in that file. No existing block or sampler changes (INT-2 and INT-3 do not apply).
- **Haze:** PostLook::Exhausts calls PostPlume::HazeShape(vv, i, &r0, &r1, &len) instead of Segments(), so the haze keeps legacy exhausts and matches volume plumes.
- **Composite:** there is no hardware Z test; occlusion comes only from tDepth, which includes bases, buildings and trees. Volume-cloud occlusion is applied in the march and puff shaders at half res, not in the composite.
- **Formats:** tVolPD is RG32F (plume distance, plus the half-res scene distance used by the bilateral composite). tFlowP is RGBA32F for metre precision.
- **Texture class [V]:**
  - r_e = 0.8·wsize, and R(x) = r_e·(1 + 0.15x/L);
  - the emission is the add-on's flame strip × alpha × 2;
  - extinction is 0.6·alpha;
  - the slot is baked lazily, and until then the palette colour is used.
