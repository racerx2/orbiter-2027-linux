# Design: height and valley fog (HeightFog)

Repo: Orbiter-linux-custom (/home/racerx/Projects/Orbiter-linux-custom), branch Orbiter-linux-custom, HEAD dd4e087c, clean tree. Paths are under OVP/VulkanClient/ unless shown otherwise. Line numbers are from that tree. Read-only survey: nothing was built or run.

## 0. Rules
- One switch, `HeightFog` in VulkanClient.cfg, read once at start (SceneFx ctor). Off means the legacy image:
  - no IPI, no textures, no GfxPool jobs;
  - the shader is a new file (shaders/HeightFog.glsl), compiled only when on, so no other SPIR-V changes;
  - the C++ hooks multiply by 1.0f or keep the old boolean.
- Own code, from published maths:
  - Wronski 2014 (froxel volumetric fog), considered and rejected in 1.2;
  - Hillaire 2015 (energy-conserving step integral);
  - Wrenninge et al. 2013 (multiple-scattering octaves);
  - Henyey-Greenstein 1941 (phase);
  - Timonen and Westerholm 2010 (height-field shadow line sweep);
  - Koschmieder (visibility = 3.912/σ);
  - the sunrise equation cos H0 = −tan φ tan δ;
  - Jimenez 2014 (interleaved gradient noise);
  - Teschner et al. 2003 hash primes with a Murmur3 finaliser (as PostLook.glsl).
  No GPL, vendor or Khronos sample code.
- One-line comments. The 2D panel, HUD, MFDs, GDI and debug text are never touched, and no text goes over the sim screen.
- TAA-aware: the march offset changes per frame only while TAA runs. Otherwise the offset is static per pixel and blurred.

## 1. What it does, and the choice of method
### 1.1 Look
- Low ground fog and haze layers. They pool in valleys and basins up to a nearly flat top, lie over the sea and lakes, and thin with altitude. Ridges and peaks stand out of the fog.
- Strongest around dawn and through the night. Light evening haze. Deeper and more common with long nights (season and latitude). Weaker under overcast skies and in dry air.
- Lit by the sun with strong forward scattering, so the low sun glows through the fog. The existing sky shafts (PostLook) pick that glow up.
- Shadowed by the terrain (volumetric shafts from ridges into foggy valleys) and, when the volume clouds run, by the clouds.
- Applies to everything in SceneFx tDepth: terrain, bases, buildings, trees, vessels, PBR/glTF and XR. Also applies to the sky and horizon (depth 0).
- Fades out between 12 and 30 km camera altitude. Above that the Scatter.glsl tables alone shape the haze, as today.

### 1.2 Method: a per-pixel march limited to the fog shell, driven by a CPU terrain fog map
- **Froxel volume (Wronski 2014, Hillaire 2015): rejected.**
  - It needs compute or layered 3D render targets. VkCore/VkShader/IProcess have neither (grep finds no compute pipeline, dispatch or 3D attachment); ImageProcessing draws full-screen quads into up to 4 2D targets.
  - Its 64 exponential slices cannot hold both a fog bank 2 m away and a valley 150 km away.
  - Depth-0 sky pixels lie beyond the last slice.
  - The volume's one advantage here, fogging particles and exhausts, has no hook: they draw after AfterOpaque.
- **Pure analytic exponential height fog (UE-style closed form): rejected as the main model.**
  - One fixed base height cannot make a fog top that follows valley floors.
  - It has no terrain or cloud shadows (no shafts inside the fog).
  - It ignores curvature at grazing angles. A 100 m layer seen from the ground leaves the layer about 36 km out, which the flat-earth formula gets wrong at the horizon.
  - The closed form is kept for the CPU sun transmittance and the sun's column through the fog.
- **Fog in TerrainPS: rejected.**
  - Buildings, trees, bases and vessels use other shaders.
  - NewPlanet.glsl has no free sampler (map-water §1).
  - It would change the Earth shader's SPIR-V.
- **Chosen, the same form as VolMarch/VolShadow:**
  - a half-resolution full-screen IPI pass in SceneFx::AfterOpaque;
  - each pixel's ray is clipped to the fog shell and to tDepth (or to the planet, or 200 km, for depth 0), then marched in 16 asinh-spaced steps with the Hillaire step integral;
  - density comes from two camera-centred fog maps (terrain floor, valley depth, sun-shadow height, strength) baked on a GfxPool thread from the loaded elevation grids;
  - then a depth-weighted blur and a depth-aware 2×2 up-sample composite alpha-blended over the scene, exactly as VolComposite does.

## 2. Where it hooks (Custom lines)
- **SceneFx ctor (SceneFx.cpp:34-41).**
  - Line 36: `InitDepth()` also when `Config->HeightFog`.
  - New `if (Config->HeightFog && depthOK) InitFog();`. The IPI is compiled at start, not mid-flight (the code-review-vfx-taa-shadow NIT 5 lesson).
- **SceneFx dtor (127):** `FreeFog(fog)` next to FreeVolShadow (129).
- **SceneFx::CloudUpdate (175-261), at the map upload (210-218):** with fog on, compute `covMean` from `box->map`: the mean coverage within 50 km of the map centre. This is the humidity proxy, a CPU loop over 256².
- **SceneFx::AfterOpaque (263-285):**
  - `float wF; bool fogOn = FogWanted(vp, &wF); if (fogOn) FogUpdate(vp);`;
  - line 274's early return also needs `!fogOn`;
  - order after VolShadow (279-280):
    - `bool fogFirst = clouds && vp->CameraAltitude() > vp->prm.cloudalt + 500.0;` (above the slab middle: base = cloudalt − 1000, top base + 3000);
    - `if (fogOn && fogFirst) Fog(...)`, then `if (clouds) Clouds(...)`, then `if (fogOn && !fogFirst) Fog(...)`.
  - Below the clouds the fog is nearer, so it goes over them. From above, the clouds cover the fog under them.
- **Scene.cpp:1596:** `bLabelsLate = TaaActive() || (pFx && pFx->FogLikely());`. With fog, the planet, surface and art labels draw after AfterOpaque (the 1957-1961 block) and are not fogged. FogLikely() is false with the switch off, so the line is unchanged then.
- **Scene.cpp:3976 (RenderGlares, after TerrainSunVisibility):** `glare *= pFx ? pFx->FogSunT() : 1.0f;`. The sun glare fades in thick fog.
- **PostLook.cpp:234 (flare CPU visibility):** `× FogSunT()`. No PostLook shader change.
- **Order this frame:**
  1. DepthPass;
  2. Flag;
  3. SSAO;
  4. VolShadow;
  5. [Fog | Clouds | Fog];
  6. TAA resolve (1955);
  7. late labels;
  8. heat haze (1962);
  9. markers, exhausts, particles, glass, VC;
  10. Measure (2084);
  11. LightBlur;
  12. PostLook Final (2177);
  13. glares;
  14. 2D.

## 3. Passes and buffers
| Item | Format, size | Use |
|---|---|---|
| tMap[c][k], c = near/far cascade, k = current/previous | RGBA16F 256×256, SAMPLED + TRANSFER_DST | fog map (§6), uploaded from the job |
| tFH[2] | RGBA16F W/2 × H/2, RTUsage | march, blur ping-pong; premultiplied display colour + alpha |
| reads tDepth (R32F), tCov (R8 256², volume clouds only), scatter tables SUN_COLOR, RAY_LAND, MIE_LAND, ATN_LAND, SKY_AMBIENT | — | — |
- **Passes:**
  - PSFogMarch (half res) → tFH0;
  - PSFogBlur (half res) → tFH1;
  - PSFogComposite (full res) → target, `Execute(1, true)` (SRC_ALPHA/INV_SRC_ALPHA, as VolComposite);
  - then the main target, viewport and state are restored the way AfterOpaque does (281-284).
- **Memory:** 2 MB of maps plus 8.3 MB half-res buffers at 1080p. CPU copies of the current maps (2 × 1 MB float4) are kept for FogSunT.

## 4. Density model (valley pooling)
- **Altitudes are above sea level (the body radius).**
  - Const.PlanetRad is size + minelev (VPlanetAtmo.cpp:653).
  - The shader uses vShell.z = size and vShell.w = the camera altitude, taken in double from vp->CameraAltitude().
  - Sample altitude, stable in float: `h(t) = hc + (2 t Rc μ + t²)/(sqrt(Rc² + 2 t Rc μ + t²) + Rc)`, with Rc = size + hc and μ = dot(d, normalize(CamPos)).
- **Map fields per texel (baked, §6):**
  - F: valley floor (m);
  - V: valley depth = local mean ground G − F (m);
  - Hs: sun-shadow height (m) for the bake's sun;
  - m: strength multiplier = day noise × (1 + 1.5·water), clamped 0-3.
- **Valley fog (the pool):**
  - lid depth `D = (D0 + kv·min(V, 1200 m))·sqrt(m)`;
  - lid `L = F + D + 0.25·D·(2n − 1)`, where n is drift noise, below;
  - softness `Δ = 0.15·D + 8 m`;
  - `ρf = σf·m·(1 − smoothstep(L − Δ, L + Δ, h))·(0.6 + 0.8 n)`.
  - The floor (a smoothed minimum, §6) is flat across a valley, so the top is flat, follows the valley's fall, and stops at the slopes. Its crisp edge on the hillside comes from each sample's true altitude, not from the map's resolution.
- **Haze layer (an inversion-capped exponential above the local mean ground G = F + V):** `ρh = σh·exp(−max(h − G, 0)/Hh)·(1 − smoothstep(Hinv − 150, Hinv + 150, h − G))`, with Hh = 350 m.
- **Drift noise n:**
  - 3D value noise on planet-local position (vPl rows), with 700 m cells;
  - integer hash with mod 64 on x, plus a CPU offset `fmod(simt·0.6 m/s, 64·700 m)` along local x, so the pattern is continuous when the offset wraps;
  - world-anchored, so it does not jump when a map is rebuilt.
- **Edge fade:** the far map's edge, density × (1 − smoothstep(0.85, 1.0, r/half)). The near map blends into the far one over 0.80-0.95 of its half side.
- **Per-frame strengths (CPU, FogMath::Frame), from the camera's planet-local lat φ and lng:**
  - Subsolar point from the planet-local sun vector: δ = asin(s.y), λs = atan2(s.z, s.x).
  - Local solar time τ = 12 + (λ − λs)·12/π, wrapped to 0-24 h. Retrograde bodies flip the sign (oapiGetPlanetPeriod < 0; check in 13).
  - Day length from cos H0 = −tan φ tan δ, clamped: polar day or night.
  - Hours after sunrise a and after sunset b.
  - **f_tod (continuous and 24 h periodic):**
    - morning: 1 − 0.9·smoothstep(0.5, 3.5, a) while the sun is up;
    - day: 0.1 until sunset;
    - night: 0.1 + 0.9·smoothstep(0, nightLen, b)^0.7, reaching 1 at sunrise;
    - polar day: 0.15; polar night: 0.8.
  - f_season = mix(0.55, 1.15, smoothstep(9 h, 15 h, nightLen)): long nights mean cold ground.
  - **Humidity proxy:**
    - with a coverage map (VolClouds): hum = 0.35 + 0.65·smoothstep(0.02, 0.3, covMean), and f_clear = 1 − 0.6·smoothstep(0.5, 0.9, covMean), since overcast nights make little radiation fog;
    - without it: hum = 0.7, f_clear = 1.
  - body = 1 for a planet shader with _WATER (PlanetShader::bWater), else 0.25 (dust haze; no water term).
  - σf = 0.008·f_tod·f_season·f_clear·hum·body (1/m). At m = 1 that is a visibility of 490 m. Over water at dawn, m up to 2.5, about 200 m.
  - D0 = 20 + 160·f_tod·f_season·hum (m); kv = 0.25·(0.4 + 0.6·f_tod).
  - σh = 6e-5·hum·body·(0.6 + 0.4·max(f_tod, 1 − smoothstep(0, 3, b))) (1/m).
  - Hinv = 400 + 1100·smoothstep(1, 8, a) in daytime, falling back to 400 over 2 h after sunset (continuous).
  - wF = (1 − smoothstep(12 km, 30 km, hc − Ḡfar)) × the map ramp (0 → 1 over 2 s after the first far-map upload). Ḡfar is the far map's mean ground.
  - f_tod is taken at the camera for the whole map (±0.4 h of solar time across ±150 km at 45°). Lighting still uses each sample's own sun angle.

## 5. Lighting
- **Sun colour per step:** `Cs = GetSunColor(dot(n̂p, s), h − Const.MinAlt)·Const.cSun·Const.Clouds·vLight.y`.
  - These are the scatter tables' transmittance (Scatter.glsl:324), so the reddening and the dark at low or negative sun angles follow the atmosphere.
  - Const.Clouds matches VolMarch's sunC (VolCloud.glsl), so fog tops and cloud tops share a scale.
- **Terrain shadow:** `Vt = smoothstep(Hs − 30, Hs + 30, h)`.
  - A point above the texel is sunlit exactly when it is above the shadow height. The per-tile horizon maps (tHorizon) only give the horizon at ground level, are not reachable in screen space, and say nothing for a point 200 m up.
  - At ground level Hs agrees with Horizon::Compute (test 15.4).
- **Cloud shadow (volume clouds valid this frame, vCld.y = wV):**
  - q = p + s·t, where the sun ray meets the slab base sphere (SphereHit);
  - `Tc = mix(1, exp(−2.5·Coverage(q)), vCld.y)`. One tCov tap; Coverage is as VolCloud.glsl:70-76 with the cloud map frame from SceneFx::VolParams.
  - With the switch VolCloudShadows off, the fog is still cloud-shadowed. Without volume clouds there is no cloud shadow on the fog.
- **Sun column through the fog (flat layer, closed form):** `ODs = (0.8·σf·m·max(L + Δ − h, 0) + σh·Hh·exp(−max(h − G, 0)/Hh))/max(μs, 0.08)`.
- **Phase and multiple scattering (Wrenninge 2013, 3 octaves):**
  - `MS = Σi 0.5^i·P(cosθ, 0.5^i)·exp(−0.5^i·ODs)`;
  - `P(c, k) = 0.75·HG(c, 0.7k) + 0.25·HG(c, −0.2k)`, with HG 4π-normalised as VolCloud.glsl:114.
  - The forward lobe makes the glow around a low sun; deep inside the fog the octaves keep it grey-white, not black.
- **Ambient:** `A = GetAmbient(n̂mid)·vLight.z·(0.35 + 0.65·exp(−0.5·σf·m·max(L − h, 0)))`, once per pixel.
  - GetAmbient is the tAmbient sky radiance (Scatter.glsl:693), valid in the atmosphere, which is the only place the fog runs.
  - vLight.z = 2 (hemisphere over zenith), to be calibrated.
- **Source and step (Hillaire 2015, albedo 1):**
  - `S = Cs·Vt·Tc·MS + A`;
  - `a = 1 − exp(−(ρf + ρh)·dt)`; `L += T·a·S`; `tw += T·a·t`; `ts += T·a`; `T *= 1 − a`.
  - This is VolMarch's own step form.
- **Haze in front and tone:**
  - α = 1 − T; col = L/α; pm = o + d·tw/ts;
  - `sct = GetLandView(|pm|, n̂pm)`;
  - `col = col·sct.atn + sct.ray·RayPhase(cosθ) + sct.mie·MiePhase(cosθ)`, which is TerrainPS's haze form (NewPlanet.glsl:888-889, with cosθ = dot(d, s));
  - `HDR(col)` (Filmic under _FILMIC), then premultiplied: (HDR·α·wF, α·wF).
  - The over-blend in display space is the convention the volume clouds already use (VolCloud.glsl:201-204).
- **Night:** Cs → 0 through the tables, so the fog takes only the twilight or night sky ambient. City lights do not glow in it (13).

## 6. Fog maps (CPU bake on GfxPool)
- **Two cascades.** Each is a gnomonic tangent plane in the planet frame: VolCloud::Frame with the planet longitude, not CloudLng. Row 0 is north.
  - near: ±25 km (195 m texels), from internal level 9 grids (39 km tiles, 153 m nodes);
  - far: ±150 km (1.17 km texels), from level 7 (156 km tiles, 610 m nodes).
- **Grids:**
  - New `TileManager2<SurfTile>::ElevGrids(lat, lng, halfLat, halfLng, lvl, out)` in Surfmgr2.cpp, next to HorizonAt (2441-2458), declared at Tilemgr2.h:533-539.
  - Under the loader mutex, it collects the ready `elev` grids (elevReady + elev, as HorizonSubmit 1731-1797) at lvl over the box, or the finest ready ancestor. It copies them as Horizon::Grid.
  - At most 25 grids; above that, one level coarser (high latitudes need more longitude tiles).
  - Margins: 16 km for near, 30 km for far, for the shadow sweep.
- **Bake (FogMath::Bake, pure C++, one job per cascade, new GfxPool kind FOGMAP = 4 at GfxPool.h:19, NKIND 5).** Per texel:
  - four sub-samples via Grid::Covers/At (finest covering grid first), giving their mean and max;
  - F = box_r1.5km(min_r2km(mean)); G = box_r3km(mean); V = max(G − F, 0). The same metric radii in both cascades, so the two agree in their blend band;
  - water (wet bodies only):
    - a sub-sample is sea where h ≤ 0.5 m (sea clamped at 0, map-water §3);
    - it is a lake where the 3×3 source-node range is < 1 m (DEM lakes are flattened);
    - w = box_r1km(fraction). The tMask water bit is GPU-only and tile-local, so it is not reachable here;
  - **Hs, the shadow line sweep (Timonen-Westerholm 2010):**
    - heights relative to the map centre's tangent plane, hp = max − r²/2R, so the sun ray is a straight line of slope tan e0 (e0 = the sun elevation at the centre);
    - `Hp(x) = max(hp(x), Hp(x + δû) − δ·tan e0)`, swept row by row from the sun side, with linear interpolation on the previous row;
    - then Hs = Hp + r²/2R;
    - the upstream boundary comes from the far map's Hs (near cascade) or the terrain itself (far);
    - e0 < −2°: Hs = 1e4, so everything is shadowed;
  - m = DayNoise(lat, lng, mjd)·(1 + 1.5w), with DayNoise in 0.25-1.35: value noise with 0.05 rad (about 320 km) and 1 day cells, smooth in both;
  - stats: Fmin, max(L) at full strength, max(G + Hinv), mean G (for the shell bounds and wF);
  - output: float4 (CPU copy) and half4 (FogMath::ToHalf) for the upload.
- **Rebuild (FogUpdate, render thread, main pass):**
  - far first; near only once a far map is valid;
  - far rebuilds when: none is valid, another planet, the camera is more than 20 km from the centre, the sun has turned more than 1.0° (planet-local) since the bake, the bake is older than 120 s, or the grid signature changed (at most every 5 s);
  - near: 3 km, 0.5°, 120 s, 2 s;
  - one pending job per cascade; dropped jobs are asked again (as CloudUpdate's mapDropped).
- **Upload and cross-fade:**
  - the result goes from a mutex'd box into the free texture of the pair;
  - the pair swaps, and the previous map and its frame stay bound with weight 1 → 0 over 1 s of system time (vXF);
  - a result that arrives during a fade waits in the box until the fade ends;
  - the shader samples the previous map only while its weight > 0.
- **Frames per draw:** E, N, U = R_planet(now) · the frame's local axes, as SceneFx::VolParams (401-416) builds the cloud frame (without cloudrot).
- **FogSunT (CPU, per frame):**
  - 24 asinh steps from the camera toward the sun over 30 km through FogMath::Density (the shader's density without the noise) on the CPU copies;
  - T = exp(−∫σ); 1 when off or not wanted.

## 7. Shader shaders/HeightFog.glsl
- **Header:** `#include "Scatter.glsl"`; Const at binding 1 (`SetStruct("Const", vp->GetScatterConst())`, as Clouds 434-480).
- **Own block `FogPS` at binding 2**, 28 vec4, filled by SceneFxFog.cpp:
  - vCamX/Y/Z (SceneFx::CameraRays 384-399, TAA jitter folded in);
  - vMapE[4], vMapN[4], vMapU[4] (near, far, near-previous, far-previous; E.w = 1/half, N.w = weight);
  - vCldE/N/U (cloud map; w 1/half) and vCld (base radius, wV, 2.5, 0);
  - vPlA/B/C (planet-local rows; w: drift offset, 0, 0);
  - vFog (σf, σh, D0, kv); vFog2 (Hinv, Hh, 0, wF); vLight (g 0.7, sun gain 1, ambient 2, 0);
  - vShell (r_in, r_out, size, hc); vMarch (steps 16, frame or −1, max distance 200 km, 0); vTex (1/W, 1/H, 1/w, 1/h).
- **Samplers:**
  - Scatter.glsl's tSun 4, tLndRay 6, tLndMie 7, tLndAtn 8, tAmbient 10;
  - own: tDepthF 13, tFogN 14, tFogF 15, tFogNp 16, tFogFp 17, tCovF 18, tFH 19.
  - Bind exactly what each entry uses (the code-review-vfx-look-water-core MUST 1 lesson):
    - March: 13-18 + the five tables. Without clouds, tCovF gets tFogF with vCld.y = 0, and the previous maps get the current ones with weight 0;
    - Blur/Composite: 13 + 19.
- **No macros of its own.** The ppf is `_PERFORMANCE` (when !bAtmoQuality, so the table sizes match), `_FILMIC`, `_TAA`, as InitClouds (SceneFx.cpp:89).
- **PSFogMarch(x, y):**
  - The pixel's depth is the full-res texel it stands for (VolShadow.glsl:19 FullUV); the ray is PixelRay from vCamX/Y/Z.
  - Shell [r_in, r_out] = [size + Fmin − 20, size + max(Lmax, Gmax + Hinv) + 200], from the bake stats.
  - The first stretch of the ray inside it is [t0, t1] ∩ [0.5 m, depth or (planet-sphere hit at size + Fmin, 200 km)]. An empty stretch returns 0: most sky pixels, and everything seen from above the layer that the layer does not cover.
  - 16 steps, even in asinh(t/100 m) (VolMarch's spacing).
  - Start offset j: with TAA running, `fract(IGN(px) + 0.618034·(frame % 64))`; otherwise IGN(px), static.
  - Per step: one map tap (two in the near band, double while a fade runs), one tSun tap, one tCov tap (clouds only), three exp in MS, the noise hash (8 lattice hashes).
  - Early exit at T < 0.01.
  - Then §5.
- **PSFogBlur:** 3×3, centre weight 2, DepthW (VolCloud.glsl:128-134) on full-res depth at FullUV, as PSVolShdBlur.
- **PSFogComposite:** VolComposite's depth-aware 2×2 up-sample (VolCloud.glsl:223-240); returns (rgb/α, α), with α < 1e-3 → 0.
- **Depth 0 (sky, 2D cloud layer, terrain beyond 1e6 m):**
  - integrated to the shell exit, the planet hit or 200 km;
  - inside a 100 m layer a horizontal ray leaves it about 36 km out, so the horizon goes white;
  - from a peak above a fog sea, rays toward the horizon cross the layer near their lowest point, which gives the fog band at the horizon;
  - camera above the 2D cloud layer: α × (1 − 0.8·Coverage(view ray at cloudalt)) when the cloud map is valid (the 2D layer is drawn before the fog and is not in tDepth; see 13).

## 8. C++ changes per file
- **SceneFxFog.cpp (new, like SceneFxShadow.cpp):**
  - `struct FogState`: IPI, tMap[2][2] and the CPU maps (shared_ptr<const FogMath::Map>), frames, weights, tFH/sFH[2], job box, poolId, bookkeeping.
  - `InitFog` (on failure: LogErr once, fog = nullptr, nothing else changes).
  - `FreeFog`.
  - `FogLikely`: switch, state, proxy with atmosphere, not PLT_GIANT, CameraAltitude < 30 km.
  - `FogWanted(vp, &wF)`: FogLikely, a far map of this planet, wF > 0.
  - `FogUpdate(vp)`: grids, jobs, uploads, fades, per-frame FogMath::Frame, FogSunT.
  - `Fog(target, vp, wF, cloudsOn)`: the uniforms, VolParams for the cloud frame when cloudsOn, then the three passes.
- **SceneFx.h (23-107):**
  - public `bool FogLikely() const; float FogSunT() const;`;
  - private `struct FogState; FogState *fog = nullptr; void InitFog(); static void FreeFog(FogState*); bool FogWanted(vPlanet*, float*); void FogUpdate(vPlanet*); void Fog(VkSurf*, vPlanet*, float, bool); float covMean = -1.0f, fogSunT = 1.0f;`.
- **SceneFx.cpp:** lines 36, ctor, 129, 210-218, 263-285 (§2).
- **FogMath.h/.cpp (new, pure, no Vulkan or oapi; includes HorizonMap.h and VolCloudMap.h):**
  - `Frame`: in camera lat/lng, sun local vector, MJD, covMean, wet, hc, Ḡ; out the §4 params;
  - `Diurnal`, `Sunrise`, `DayNoise`, `Bake`, `Floor`, `ShadowSweep`, `WaterAt`, `Density`, `SunTransmittance`, `ToHalf`, `NeedRebuild`.
- **Surfmgr2.cpp + Tilemgr2.h:** ElevGrids (§6).
- **GfxPool.h:19:** FOGMAP = 4, NKIND = 5. The queue picks the lowest kind first (GfxPool.cpp:133), so fog jobs never delay atlas, map, noise or horizon jobs.
- **Scene.cpp:** 1596 and 3976 (§2). **PostLook.cpp:** 234.
- **D3D9Config.h:122+:** `int HeightFog; // not upstream: valley fog and haze layers (0,1)`.
- **D3D9Config.cpp:** default after 122 (`HeightFog = 1`), read after 246 (clamp 0-1), write after 368.
- **resource.h:** `IDC_FX_FOG` after 196 (3054 unless another of the five takes it; the integrator assigns).
- **D3D9Client.rc:** after 286, `AUTOCHECKBOX "Height fog", IDC_FX_FOG, 380, 416, 50, 8`. Row 1 is free from x 375.
- **VideoTab.cpp:** set after 936, save after 1033.
- **CMakeLists.txt:75-82:** SceneFxFog.cpp, FogMath.cpp.
- **Tests/CMakeLists.txt:222-225:** `add_unit_test(FogMath.Test FogMath.cpp HorizonMap.cpp VolCloudMap.cpp)`, include dir, -O2.

## 9. Config, dialog, docs
- Key `HeightFog` (default 1; see 13), read once. Effects group checkbox "Height fog".
- **VulkanClient.html Effects list (631-682):** a new `<li>Height fog (<code>HeightFog</code> 1)</li>` with:
  - "Ground fog and haze that collect in valleys and over water, most around dawn and on long nights, lit and shadowed by the sun. Fades out above about 20 km. Labels stay clear."
- **README.md:221-226:** add `HeightFog` (1, valley fog and haze layers).
- **NEW.tex:48:** add "height and valley fog".
- **LAUNCHPAD.tex:555:** add "height fog" to the Effects list.

## 10. Cost
- **GPU target at 1920×1080 on an RTX 3060-class NVIDIA desktop GPU: ≤ 0.6 ms in total.**
  - march ≤ 0.40 ms (518k pixels × 16 steps, 2-4 taps each);
  - blur ≤ 0.05 ms;
  - composite ≤ 0.10 ms;
  - uploads (512 KB, at most once per 2 s) negligible.
  - With TAA on by default (D3D9Config.cpp:116), DepthPass already runs every frame. With TAA, SSAO and clouds all off, the fog forces it below 30 km; record that cost separately.
- **CPU:**
  - per frame ≤ 0.05 ms (Frame + FogSunT);
  - a rebuild frame ≤ 0.5 ms on the render thread (grid copies ≤ 25 × 268 KB, under the loader mutex);
  - the job ≤ 30 ms per cascade on one pool thread.
- **If over budget:** 12 steps, or the far half of the march at quarter rate (every other half-res pixel, then the blur fills in).
- **How to measure:**
  - temporary Vulkan timestamp queries (vkCmdWriteTimestamp, BOTTOM_OF_PIPE) around the three Execute calls on VkDev's command buffer, read back 2 frames later × timestampPeriod, with the 600-frame mean logged and then removed (as agent vshd did);
  - the mean frame interval with HeightFog 0/1 in each scenario of 14;
  - the job time with std::chrono in the job, logged at LogDbg level.

## 11. Interplay with the existing effects
- **TAA (PostTaa):**
  - the fog is in the opaque image before the resolve, so its history reprojects with the surface behind (static ground: exact) or, at depth 0, with mSky or the shell;
  - the fog has no high frequencies, so the 3×3 clip absorbs the parallax error. No fog-distance target in v1, and PostTaa is unchanged;
  - the per-frame start offset is averaged; with TAA off the image is static (fixed IGN offset, blur).
- **SSAO, VolShadow:** they run first and darken the surface. The fog in front then attenuates them, which is correct.
- **Volume clouds:** the order follows the camera side (§2). The fog is cloud-shadowed from the same tCov (it ignores the clump noise, so the in-fog shadow is softer than VolShadow's ground shadow).
- **Light shafts:** the fog's forward glow raises the sky's luminance near the sun on depth-0 pixels, which the mask (PostLook.glsl:96-106, luma knee 0.6-0.95) turns into shafts, occluded by terrain and vessels. The fog's own Hs shadows give true volumetric shafts in valleys. Check that the sunset sky does not clip (14).
- **Lens flare and sun glare:** × FogSunT (CPU), so a sun hidden in thick fog gives neither ghosts nor glare, but the glow remains.
- **Water:** the glint and foam are under the fog and attenuated by it. The fog over the sea comes from m (water), not from tMask.
- **Filmic:** HDR() in the fog follows _FILMIC.
- **Exposure:** Measure runs later, so the controller sees the fog. In a fog bank the image is bright and flat, and e drops, as it would for a real camera.
- **LightBlur (PostProcess=1):** blooms the bright fog glow. With HDR vessels (> 1) in the offscreen target, the display-space over-blend has the same limits as the volume clouds.
- **Heat haze:** refracts the fogged image (it runs after). Its depth test is unchanged.
- **Trees:** they are in tDepth (the distance pass), so they are fogged per pixel at their own dithered depth.
- **Labels:** late, so they are not fogged. **VC cockpit, 2D panel, HUD:** drawn after, so they are not fogged.
- **Exhausts, particles, beacons, glass (RenderPbrDeferred):** drawn after AfterOpaque, so they are not attenuated (see 13). The volumetric-plume feature of this batch should multiply its plume by FogSunT or sample tFH if it wants fogged plumes.
- **Other features of the batch (shadows, SSR, SSGI):** anything that changes surface radiance must run before Fog in AfterOpaque. The fog is the last thing in front of the surface.

## 12. PBR/glTF/XR
- glTF opaque groups are in tDepth (Mesh.cpp opt 2, vfx-changes TAA notes), so they are fogged like legacy meshes.
- BLEND and transmission groups draw later in RenderPbrDeferred. The fog behind them is at the surface depth, and the glass itself is unfogged (a glass canopy far off in thick fog keeps its highlights; minor).
- PbrMesh.fx's per-vessel gSun.Transmission/Inscatter (the atmosphere's haze) is unchanged. The fog adds on top.
- XR vessels draw through vVessel::Render in DepthPass (SceneFx.cpp:296), so they are fogged.
- The cockpit of a glTF/XR vessel draws after, so it is unfogged.

## 13. Risks and open questions
1. **Display-space over-blend.** An exact form exists for the 1 − exp curve: out = 1 − (1 − y)^T·exp(−E·Lf). It needs a scene copy and breaks on the HDR offscreen target, so the volume clouds' convention is kept. Thin fog over bright snow is slightly too light.
2. **Humidity proxy.** With volume clouds off it is a constant, so deserts get dawn fog (×0.7). A climate humidity texture would be better but needs a data source.
3. **Water from elevation.**
   - Polders and salt flats can read as lakes.
   - Rivers narrower than about 400 m are missed (valley pooling covers them).
   - Sea fog over pure-ocean tiles needs their elevation grids (zeros) to be ready.
4. **The 2D cloud layer with volume clouds off and the camera above it:** the fog under it shows through (not in tDepth, no coverage map). Mitigation with the cloud map only.
5. **Not fogged:** exhausts, particles, beacons, glass, the custom camera MFD and env maps (no AfterOpaque in secondary turns, as SSAO and clouds). City lights do not glow in fog.
6. **High time warp:** the strengths follow sim time; rebuilds are throttled in real time, so at ×1000 Hs lags. Proposal: Vt = 1 (no terrain shadow) when the warp is > 100.
7. **Retrograde rotators:** check the sign of oapiGetPlanetPeriod for Venus before using it in τ.
8. **Default 1 vs 0.** The look changes a lot at dawn. The integrator may ship 0 until it is art-tuned.
9. **Calibration constants:** σf 0.008, D0, kv, σh 6e-5, Hinv, the ambient gain 2, the phase 0.75/0.25 and g 0.7. Tune against the Alps Dawn and KSC Dawn shots.
10. **The second UBO path:** none. FogPS is binding 2, as VolPS. Seven own samplers plus five tables is 12 of the 28 sampler slots.
11. **Loader mutex time in ElevGrids** with 25 grid copies: about 0.3 ms. Spread near and far over different frames.

## 14. Checks (NVIDIA, 1920×1080 render; validation layers on)
- **HeightFog = 0:** pixel-identical to dd4e087c in Orbit Alps, Alps High, Carpathians Under and KSC DG. Log: no FogMath/HeightFog lines. SPIR-V of all other shaders unchanged (glslangValidator compare).
- **New build-tree scenarios** (Scenarios/Tests, not in git):
  - Gfx Fog Alps Dawn: a peak about 2.5 km above a valley, sun 3° up, January;
  - Gfx Fog Alps Noon: same place, noon;
  - Gfx Fog KSC Dawn: ground camera 2 m up, DG at 300 m, sun behind the DG;
  - Gfx Fog Coast Dawn: 300 m over the Florida coast;
  - Gfx Fog Valley Inside: camera 30 m above a valley floor.
- **Alps Dawn:**
  - valleys filled, ridges out;
  - the fog top's spread under 50 m over 5 km of one valley (debug: the lid altitude in red);
  - sunlit fog tops with terrain-shadow shafts across them;
  - no map, cascade or tile seams at ×3 contrast;
  - no halo at ridge silhouettes wider than 2 px.
- **Alps Noon:** mean fog α ≤ 15 % of dawn.
- **KSC Dawn:**
  - the DG's contrast vs the sky within ±10 % of exp(−σ·300 m) from the logged σ;
  - horizon white;
  - glow around the sun;
  - flare and glare scaled by the logged FogSunT;
  - labels on: not fogged, also with TAA = 0.
- **Coast Dawn:** a fog layer over the sea, thinner over land.
- **Alps High (10 km):** luminance step across the far map's edge < 2/255; wF falls 12-30 km. **Orbit Alps:** no fog passes (log), image identical to off.
- **Volume clouds:**
  - Carpathians Under: clouds behind the fog;
  - Alps High: fog visible in gaps, not drawn over clouds;
  - Clouds Inside: no artefacts at the slab.
- **Look KSC Sunset with fog:** shafts present; < 5 % of the screen above 250/255 around the sun.
- **Stability:**
  - static camera, TAA = 0: frame-to-frame mean change ≤ 1/255 (only the 0.6 m/s drift);
  - TAA = 1: p99 ≤ 4/255 (as today);
  - Gfx TAA Chase in fog: no trail at the vessel edge.
- **Warp ×100 from Alps Dawn for 2 h:** the fog thins smoothly; max per-frame mean change < 2/255 at every rebuild (logged).
- **Also run, no errors:**
  - PostProcess = 1;
  - Filmic = 1;
  - fullscreen without MSAA;
  - VC cockpit in fog (cockpit clear);
  - DG glTF at KSC in fog;
  - Mars, Valles Marineris at dawn: faint only, no water term;
  - night KSC: dark fog, no glow.
- **Cost:** timestamps per scenario, ≤ 0.6 ms (§10). ctest passes.

## 15. Pure maths unit FogMath and Tests/FogMath.Test.cpp (Catch2, as LookMath.Test)
1. **Sunrise and time:**
   - equator at equinox: day 12 h ± 1 min;
   - 50° N at the June solstice: 16.3 h ± 0.1;
   - polar day and night flags;
   - local solar time from (λ − λs): noon at the subsolar longitude, 18 h at +90°.
2. **Diurnal:** 24 h periodic; continuous (max step over 1 min < 0.01) at 0°, 45° and 65° on 4 dates; 1.0 at sunrise; ≤ 0.12 at sunrise + 4 h; Hinv continuous.
3. **Floor and valley depth:**
   - synthetic V-valley (walls at 0.3 slope): F within 5 m of the bottom on the axis, and F ≤ the terrain everywhere;
   - flat plain: F = G = terrain, V = 0;
   - near and far cascades within 30 m of each other on the same terrain, in the band.
4. **Shadow sweep:**
   - a single 1000 m ridge, sun at 10° from the east: Hs = 1000 − x·tan 10° west of it, within 5 % (the interpolation spread);
   - random fBm terrain: RMS error ≤ 10 m vs a brute-force ray test with curvature;
   - at ground level, sunlit or shadowed agrees with Horizon::Compute on the same grid for ≥ 97 % of nodes.
5. **Water:** flat 0 m → 1; a flat 400 m 5×5-node plateau → 1; a 0.5 m-step slope → 0; a dry body → 0.
6. **Grids and frames:** coverage of the ±25/±150 km boxes at lat 0°, 60° and 85° and across ±180° longitude; the finest-first choice; the ancestor fallback.
7. **Step integral:** 16 asinh steps through a constant slab match exp(−σℓ) within 1%; through an exponential layer (slant, horizontal with curvature) within 2% of the closed form.
8. **SunTransmittance:** in a 200 m fog, sun at 10°: within 2 % of exp(−σ·(L − h)/sin e).
9. **DayNoise:** in [0.25, 1.35]; continuous in time and space; fixed values for 3 seeds, so all platforms agree.
10. **ToHalf:** exact for 0, ±1, 65504, 2^-14 and 2^-24; round-to-nearest-even at a halfway case; no NaN from finite input.
11. **NeedRebuild:** thresholds (distance, sun angle, age) as §6.

## 16. Files the implementation touches (for the ownership split)
- **New (fog owner):**
  - OVP/VulkanClient/SceneFxFog.cpp;
  - OVP/VulkanClient/FogMath.h, FogMath.cpp;
  - OVP/VulkanClient/shaders/HeightFog.glsl;
  - Tests/FogMath.Test.cpp;
  - build-tree scenarios Scenarios/Tests/Gfx Fog *.scn.
- **Fog owner, small additions in shared files:**
  - OVP/VulkanClient/Surfmgr2.cpp (ElevGrids next to HorizonAt);
  - OVP/VulkanClient/Tilemgr2.h (its declaration).
- **Integrator / shared:**
  - OVP/VulkanClient/SceneFx.h, SceneFx.cpp (ctor, dtor, CloudUpdate coverage mean, AfterOpaque order; other features also hook AfterOpaque);
  - OVP/VulkanClient/Scene.cpp (1596, 3976);
  - OVP/VulkanClient/GfxPool.h (FOGMAP);
  - D3D9Config.h/.cpp, resource.h, D3D9Client.rc, VideoTab.cpp;
  - OVP/VulkanClient/CMakeLists.txt, Tests/CMakeLists.txt;
  - doc/VulkanClient.html, README.md, Doc/Orbiter User Manual Linux/NEW.tex, LAUNCHPAD.tex.
- **PostLook owner:** OVP/VulkanClient/PostLook.cpp:234 (one factor).
- **Not touched:** NewPlanet.glsl, Scatter.glsl, VolCloud.glsl, VolShadow.glsl, PostLook.glsl, PostTaa.*, HazeMgr.cpp, all mesh/PBR shaders.

## Rev 2 (implementation)
Overrides the body where they differ. Branch ue5-batch, HEAD 35e2949c.
- **INT-5 order:** `SceneFx::FogAndClouds` (SceneFxFog.cpp) decides the order and calls `Clouds`: camera above `cloudalt + 500` with clouds on: fog, then clouds; else clouds, then fog. With `HeightFog = 0`, `fog` is NULL and it only does `if (clouds) Clouds(...)`, as before.
- **INT-4 depth:** done by the integrator (`DepthWanted()` has HeightFog, Surfmgr2.cpp:1974). The fog reads tDepth only when `DepthValid()`.
- **GfxPool::FOGMAP:** one far job and one near job at most, own pool id. The far job also builds the cloud coverage map.
- **Helpers:** the header is the integrator's, so the helpers are member functions of `SceneFx::FogState` (a nested struct, so it reads SceneFx's members). No FogLikely/FogWanted/FogUpdate/Fog members in SceneFx.h.
- **MUST 1 (scan):** each pixel first scans its ray in 16 intervals, spaced even in asinh(t/SA) with SA = max(30 m, 0.1·length). One map tap per point, no lighting or noise. Each interval gets a density bound at its lowest altitude (lid L + 2Δ at n = 1, the haze, the sea fog). The 16 lit steps are then placed by the weights `(1 − e^−τk)·e^−0.5Στ<k + 0.02·len/total`. The steps tile [t0, t1], so the Hillaire step stays exact. The same code is `FogMath::ScanPoints/Steps/Transmittance` on the CPU, with the analytic tests of review MUST 1.
- **MUST 2:** done by the integrator (see INT-4).
- **MUST 3 (occurrence and moisture):**
  - fog days `m_day = 1.35·smoothstep(1 − p, 1.15 − p, u)`. u is variance-normalised 4D value noise (3D on the unit sphere with 0.05 rad cells × 1-day cells), mapped through Φ so it is near uniform at every point;
  - `p = mix(0.12, 0.4, moist)·mix(0.75, 1.15, smoothstep(9, 15, nightLen))`;
  - moisture `moist = smoothstep(0, 0.15, Wfar)`, `hum = mix(0.3, 1, moist)`. Wfar is the far map's water fraction (sea and lakes). Coverage only sets f_clear;
  - sea texels: no radiation fog (m = 0). Lakes need a 5×5 range ≤ 0.5 m, `mean − min_r2km < 0.75 m` and ≥ 4 flagged texels in the 3×3; `w = box_r1km`;
  - polar night 0.4·moist; night ramp `^2`;
  - σh lowered to `2.5e-5·hum·body·(0.4 + 0.6·max(f_tod, evening))` (review SHOULD 9: about 0.18 α at 50 km at noon).
- **Sea fog (added; the coast check needs it, so advection fog is not left to v2):** a flat lid at Ds = 60-260 m over F < 0.5-4 m (sea, and low coast within the 2 km floor filter). Its own fog-day noise (seed 2, 2-day cells, p = 0.12·moist), not diurnal (×(0.55 + 0.45·f_tod)), wet bodies only. No extra map channel: sea-ness comes from F.
- **MUST 4 (plume contract):**
  - PSFogMarch writes a second target tFD (half-res R32F) with the transmittance-weighted fog distance tw/ts (0 means no fog);
  - `SceneFx::FogTex()` is tFH[1], half-res, premultiplied display colour and alpha (wF and the deck mask included); new `SceneFx::FogDist()` is tFD. One line in SceneFx.h, listed. Both are valid in the frame the fog drew;
  - plume composite (plumes owner): `w = smoothstep(0.75·tf, 1.25·tf, t_plume)`, `rgb' = P.rgb·(1 − αf·w) + FogTex.rgb·w·P.a`;
  - CPU `FogAlongRay(from, dir, dist)`: the same scan and step placement on the CPU copies of the maps, drift noise included (NIT 7). Returns `1 − wF·(1 − T)`, or 1 when the fog did not draw this frame or the last. Signature unchanged.
- **No double fog with SSR:** SSR owns it. For SSR: the fog in front of a hit pixel is FogTex at the hit uv, so `hit_clear = (hit − F.rgb)/max(1 − F.a, 0.05)` strips it.
- **SHOULD 1:** done by the integrator (TaaActive in PostLook; Scene.cpp:1605 has HeightFog).
- **SHOULD 2:** wet = `vp->HasRipples()`.
- **SHOULD 3 (2D deck from above):**
  - an own coverage map tCld (R8 256², ±100 km, cloud frame, `VolCloud::Fill` on the cloud tiles' densities) in the far job, rebuilt when the camera moves 20 km in the cloud frame or every 60 s;
  - Cloudmgr2.cpp:349 also makes densities when HeightFog is on (one token, listed);
  - with the camera above the deck: `α × (1 − 0.8·cov(crossing)·(1 − wV·volume fade))` when the fog lies beyond the crossing. Without a map, wF fades over cloudalt to cloudalt + 500 m;
  - the same map gives f_clear (mean within 50 km) and the fog's cloud shadow Tc, so neither needs VolClouds. The volume clouds' tCov is not used.
- **SHOULD 4:** the edge fade cuts ρf (and the sea fog) only; beyond the far map the haze uses Ḡfar.
- **SHOULD 5:** FogTowardSun/FogTex/FogDist return 1/NULL unless the fog drew this frame (FogAlongRay: this frame or the last).
- **SHOULD 6:** the fog needs GetScatterConst() and the five tables, else it is skipped.
- **SHOULD 7:** stated above. SSR decides.
- **SHOULD 8:** done (depth always full range).
- **SHOULD 9:** default 0 (integrator).
- **SHOULD 10:** sun threshold `min(base, max(0.1°, 0.1·e0))` for −3° < e0 < 10°.
- **NITs:**
  - 5: polar constants blended over the last hour;
  - 6: rotation sense from the sign of dλs/dt;
  - 7: Drift hash in FogMath;
  - 8: uncovered texels get m = 0 and Hs lit; a map needs ≥ 90 % coverage to be used;
  - 10: grid cache by tile horGen, only new tiles copied;
  - 12: the cloud shadow fades at the map edge;
  - 13: the three octave phases once per pixel.
- **ElevGrids:** `int TileManager2<SurfTile>::ElevGrids(lat0, lat1, lng0, lng1, lvl, maxn, have, out, keys)`:
  - keys are horGen; grids in `have` come back without heights;
  - returns −1 above maxn = 25, and the caller goes one level coarser;
  - the tile index range is `FogMath::TileRange` (tested);
  - its specialisation is declared in SceneFxFog.cpp, so Surfmgr2.h is not touched.
- **Unit test limits:** FogMath.Test links FogMath.cpp only (integrator's CMake). So FogMath has its own grid sampler on Horizon::Grid (header only), and test 15.4's Horizon::Compute comparison becomes a brute-force ray test at ground level.
- **Glare:** Scene.cpp RenderGlares `glare *= pFx->FogTowardSun()` (one line in an integrator file, listed). Flare: a request to the PostLook owner (plumes): × `FogTowardSun()` at PostLook.cpp:234.
- **High warp:** the terrain shadow fades out between ×30 and ×100.
