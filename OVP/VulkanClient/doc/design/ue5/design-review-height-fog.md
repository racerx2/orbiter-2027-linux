# Design review: height and valley fog (design-height-fog.md)

Repo: Orbiter-linux-custom, HEAD dd4e087c, read-only (Desktop Commander: read_file, grep, sed). Paths are under OVP/VulkanClient/ unless shown. Nothing was built or run.

**Verified, no change needed:**
- Line references: SceneFx.cpp 34-42, 89, 127/129, 175-261 (upload 210), 263-285 (early exit 274), 296, 384-399, 401-416, 434-480; SceneFx.h 23-107; Scene.cpp 1596, 1954-1962, 2084, 2177, 3976; PostLook.cpp 234; GfxPool.h:19, GfxPool.cpp:133 (lowest kind first); Surfmgr2.cpp 1731-1797, 2441-2458; Tilemgr2.h 533-539; Scatter.glsl 324, 693; VolCloud.glsl 70-76, 114, 128-133, 201-204, 223-240; VolShadow.glsl:19; NewPlanet.glsl 888-889; VPlanetAtmo.cpp:653; D3D9Config.cpp 116/122/246/368; VideoTab.cpp 936/1033; D3D9Client.rc 277-286; resource.h 196; the docs lines. Only the ctor line is off (NIT 1).
- **Thread safety of ElevGrids.** The grids are copied on the render thread under `loader->WaitForMutex()`, as HorizonAt (Surfmgr2.cpp:2444-2456) and HorizonSubmit do. hLoadMutex is a static std::recursive_mutex (Tilemgr2.h:251-252, 267), so calling it from AfterOpaque is safe even if a caller already holds it. The pool job only sees copies and shared_ptr<const> maps. `elev` is published with elevReady release (Surfmgr2.cpp:649). Note that `elev` exists only on ElevOwner tiles (2-3 levels above the drawn ones, Surfmgr2.cpp:813-821), so "level 9, or the finest ready ancestor" is the right rule.
- **Maths that is right:** the float-stable altitude h(t); the tangent-plane shadow sweep (hp = h − r²/2R, a straight sun ray, back-conversion); `GetSunColor(·, h − MinAlt)` = TerrainPS's convention (alt above PlanetRad, NewPlanet.glsl:596, 830; VolMarch's `length(p) − size` is the one that is off by minelev); the haze phase sign (camW = −vPosW, NewPlanet.glsl:433, so −fDRS = dot(d, s)); HG normalisation as VolCloud.glsl:114; HDR() includes the auto-exposure (Const.Expo, VPlanetAtmo.cpp:581); FogPS at binding 2 (NUBOS 4) and samplers 13-19 (MAXBINDINGS 32, VkCore.h:302-303).
- **Cost.** The 0.40 ms march estimate is plausible next to VolShadow's measured 0.08-0.09 ms (vfx-changes, 3 simpler passes). MUST 1 adds about 0.05-0.08 ms.

## MUST
1. **The 16-step march cannot see a thin fog layer away from the camera (the Alps Dawn hero case).**
   - **Problem:** in mountains the shell does not clip anything. The Alps shell top is Gmax + Hinv + 200 ≈ 5 km, so a camera on a peak is inside it. The 16 steps are then even in asinh(t/100 m) over [0.5 m, depth].
     - Example: the camera is 2.5 km above a valley and looks at the floor 10 km away, descending at 14.5°. The 150 m fog lid is the last 600 m of the ray.
     - The last step covers 7.2-10 km (ds = asinh(100)/16 = 0.331). Its one sample lands in the fog only when j > 0.81, so P = 0.19, and then a = 1 − exp(−0.008·3200) ≈ 1.
     - So E[α] ≈ 0.19, where the true value is 0.99. Per frame, α is 0 or 1.
     - A fog sea at 30 km (1.8 km path in the fog) gives P = 0.155.
   - **Consequence:** valleys seen from above look mostly clear and blotchy. With TAA the pixels flicker 0/1, which fails "TAA p99 ≤ 4/255". Without TAA the static IGN pattern shows through the 3×3 blur. Test 15.7 passes only because its slab starts at the camera.
   - **Fix:**
     - Add a per-pixel **lid scan** before the lit march: 12 map taps (F, V, m; no lighting, no noise) at asinh spacing over [t0, t1]. Bound the lid as L + 2Δ with n = 1, and take ta and tb as the first and last t where h(t) is below that bound, widened by one scan interval.
     - Split [t0, t1] into [t0, ta], [ta, tb] and [tb, t1], with 2 + 12 + 2 steps, each in asinh from its own start. The pieces tile the ray, so the Hillaire step and the haze stay exact.
     - Put the step placement in FogMath (for example `FogMath::Steps`) and mirror it in GLSL.
     - Add tests: the geometry above gives α within 3 % of the analytic value for j ∈ {0, 0.25, 0.5, 0.75, 0.99}, and so does the 30 km fog-sea ray.
2. **The terrain is not in tDepth unless SSAO or VolClouds is on.**
   - **Problem:** `depthRec = (Config->SSAO || Config->VolClouds) && …` (Surfmgr2.cpp:1973). With both off, RenderDepth (Surfmgr2.cpp:1835) draws no tiles. The fog then treats all terrain as depth 0: it marches through mountains to the size+Fmin sphere and fogs the foreground with the fog behind it. §10 explicitly plans the "TAA, SSAO and clouds all off" case, which is exactly this one.
   - **Fix:** add `Config->HeightFog` at 1973. The SSR design (§2) adds `Config->SSR` and the missing `Config->TAA` at the same line. Better still, define one predicate, for example `static bool SceneFx::TerrainDepthWanted()`, used both at Surfmgr2.cpp:1973 and SceneFx.cpp:37, so the five features cannot drift apart.
3. **Fog every dawn and every night, everywhere: the occurrence and moisture model.**
   - **Problem:**
     - DayNoise is in [0.25, 1.35] (§6), so there is never a fog-free day.
     - On a plain at dawn without volume clouds (hum 0.7), σf = 0.008·0.7·f_season·m. Even at the minimum m that leaves a 40-75 m layer with 2.4-5 km visibility, and the typical value is about 1 km.
     - With volume clouds, a clear sky gives hum 0.35, which still means 1.4 km visibility at m = 1. That covers the Sahara at every dawn: a clear sky does not mean dry air.
     - Sea texels get w = 1 and m up to 3: about 230 m visibility over every sea at every dawn, on a radiation (diurnal) cycle that sea fog does not follow.
     - The lake test (3×3 node range < 1 m) fires on any plain flatter than about 0.2 %, because the elevation steps are 0.5 m (map-water §3). That covers Florida, the Great Plains and playas, so they get m × 2.5, and Coast Dawn's "thinner over land" check fails at KSC.
     - Polar night (0.8) fogs Antarctica and Greenland all winter.
     - The night ramp `^0.7` gives f_tod 0.65 at midnight. The night fog is unlit (Cs = 0, A ≈ 0), so it only absorbs: from 10 km at 45°, the city lights drop to about 50 % (σ ≈ 0.0036, 175 m path) every night.
   - **Fix:**
     - **Occurrence:** `m_day = 1.35·smoothstep(1 − p, 1.2 − p, v)`, with v the existing value noise in [0, 1] and p the fog-day probability (about 0.3 on moist land). Most days then have no valley fog, and the haze layer stays. Test: 55-80 % of days fog-free over a year at a fixed point; continuity kept.
     - **Moisture:** replace the constant hum (0.7, or 0.35 under a clear sky) with `mix(0.25, 1, smoothstep(0, 0.2, Wfar))`, where Wfar is the far map's water fraction. Keep the coverage only for f_clear.
     - **Water:** sea texels add moisture but get no radiation fog (advection sea fog is v2). Lakes need h − F < 1 m (a local minimum), a 5×5 range ≤ 0.5 m and ≥ 4 texels. Test: a 0.15 % planar slope gives 0.
     - **Polar night:** ≤ 0.4, times moisture.
     - **Night ramp:** `^2` instead of `^0.7`, so f_tod is 0.33 at midnight and peaks at sunrise.
4. **The plume contract is unresolved, and the proposed hook is wrong.**
   - **Problem:** plumes §13 (design-volumetric-plumes.md:489) needs "FogAt(origin, dir, dist)" or an agreed alternative. Fog §11 offers "multiply by FogSunT or sample tFH", and neither works:
     - FogSunT is the camera-to-sun transmittance, unrelated to the plume's distance.
     - tFH holds the fog up to the surface behind the plume.
     - The order itself is fine: fog runs in AfterOpaque, the plumes after Haze (Scene.cpp:1962).
   - **Fix (cheap, no shared bindings):**
     - PSFogMarch writes an MRT tFD (half-res R32F) holding the transmittance-weighted fog distance tw/ts, as VolMarch's oDist does (VolCloud.glsl:197, 251).
     - SceneFx exposes `VkTex *FogTex()` (tFH[1], premultiplied) and `VkTex *FogDist()`, valid only in the frame the fog drew.
     - PostPlume's composite, with αf and Cf from tFH[1], tf from tFD, w = smoothstep(0.75·tf, 1.25·tf, t_plume): `rgb' = P.rgb·(1 − αf·w) + Cf·αf·w·P.a`, α unchanged. Under ONE/ONE_MINUS_SRC_ALPHA over the fogged image this is exact for w = 0 and w = 1.
     - Write this into both designs and remove the FogSunT/tFH sentence.

## SHOULD
1. **The sharpen strength changes with fog.** PostLook.cpp:248 uses bLabelsLate as "TAA ran" (0.18 vs 0.12). With fog on and TAA off, the fog now raises the sharpening.
   - **Fix:** `scn->TaaActive()`, which SSR also proposes (design-ssr.md:281, §8). List PostLook.cpp:248 in §16.
   - Merge Scene.cpp:1596 with SSR: `TaaActive() || SsrActive() || (pFx && pFx->FogLikely())`.
2. **The wet-body test uses a user option.**
   - **Problem:** PlanetShader::bWater is true only when the launchpad "surface reflections" option is on (VPlanetAtmo.cpp:115, 161, 47). With it off, Earth gets body 0.25 and no water term.
   - **Fix:** `vp->HasRipples()` (VPlanet.h:302, from the planet cfg).
3. **The fog is painted over the 2D cloud deck from above.**
   - **Problem:** §7's coverage mitigation needs a fresh tCov, but CloudsWanted ends at cloudalt + 15 km (SceneFx.cpp:169) and CloudUpdate stops there. The map then stays stale (±100 km, VolCloudMap.h:16) while the fog runs to 30 km (±150 km). With VolClouds = 0 there is no map at all.
   - **Fix:** when hc > cloudalt and bCloud, sample the global cloud alpha atlas (CloudAtlasTex, Cloudmgr2.cpp:422/510; equirectangular, CloudLng with cloudrot) at the view ray's cloudalt crossing. That needs one Cloudmgr2 accessor and one more sampler. Without an atlas, fade wF to 0 over cloudalt to cloudalt + 500 m.
4. **The haze ends in a ring at the far map's edge.** The 0.85-1.0 edge fade also cuts ρh, which needs no terrain detail. From 3-12 km altitude, the haze (α up to about 0.2 on these slant rays) stops in a visible circle 0.1-1.8° below the horizon.
   - **Fix:** fade only ρf at the edge. Beyond the map, evaluate ρh with Ḡfar.
5. **FogSunT can go stale.**
   - **Problem:** fogSunT is set only in FogUpdate. Frames that early-exit at 274, or that have !fogOn, keep the last value, so the glare (Scene.cpp:3976) and the flare (PostLook.cpp:234) stay dimmed after the camera climbs out (for example above 30 km).
   - **Fix:** set fogSunT = 1 at the top of AfterOpaque, next to cloudFade (271), and have `FogSunT()` return 1 unless the fog drew this frame (fogFrame == GetFrameId()).
6. **The atmosphere data may be missing.** FogLikely and FogWanted must also require `vp->GetScatterConst()`, as CloudsWanted does (SceneFx.cpp:167; it is NULL without surfmgr2, VPlanetAtmo.cpp:866-870), and the five tables non-NULL. They are created lazily in UpdateScatter (VPlanetAtmo.cpp:595-604; skipped for mesh planets). A NULL table makes SetTextureNative unbind the slot (IProcess.cpp:623-627).
7. **SSR reflects fogged colour, then the water is fogged again.**
   - **Problem:** SSR captures after the resolve (design-ssr §2, Scene.cpp:1955), so its colour pyramid already contains the fog along the camera-to-object path. The fog pass then fogs the water pixel again. design-ssr.md:334 (§11) wrongly says the fog is "applied per shader". The result: over-fogged reflections on foggy lakes.
   - **Fix:** state this in fog §11 and in SSR, accept it for v1, and add a "Valley Inside" lake check.
8. **Shared depth gates with plumes.** Plumes cut the DepthPass far plane to plumeFar unless "SSAO, clouds or TAA" run (plume §10, design-volumetric-plumes.md:428). With fog on and those off, the fog would see the terrain beyond plumeFar as sky.
   - **Fix:** use the MUST 2 predicate there too.
   - Merge the 274 exit: `!ssao && !clouds && !taa && !fogOn && !gi && !ssr && plumeFar <= 0`.
9. **Ship `HeightFog = 0` by default until §14 passes with tuned constants.**
   - Every dawn scenario changes, even after MUST 3.
   - The ambient gain 2, the phase weights and σf are uncalibrated.
   - At noon σh = 6e-5·0.7·0.64 = 2.7e-5/m, which adds α ≈ 0.74 to terrain 50 km away on every clear day, on top of the Scatter haze.
   - Flip the default to 1 once Alps Dawn, KSC Dawn and Alps Noon are signed off.
10. **Rebuild thresholds are too coarse at low sun.** At e0 = 3°, the near map's 0.5° threshold moves a 1 km ridge's shadow tip by 1 km·(cot 3° − cot 3.5°) = 2.7 km, and the far map's 1° by 4.8 km. That is a visible slide in the 1 s fade every 2-3 min, at the hero time.
    - **Fix:** a threshold of max(0.1°, 0.1·e0) below 10° of sun elevation. The job takes ≤ 30 ms.

## NIT
1. The InitDepth line is SceneFx.cpp:37 (the ctor is 34-42), not 36.
2. **Risk 13.1 has the sign wrong.**
   - A display-space over-blend through a concave curve (1 − exp) is never lighter than the exact `HDR(T·S + Lf)` (Jensen). Fog over snow comes out too **dark**: with E·S = 3, E·F = 1 and α = 0.3, the blend gives 0.855 against the exact 0.909 (−14/255).
   - With _FILMIC the toe is convex, so it can go either way below it.
3. §7 "terrain beyond 1e6 m" as depth 0: the tiles also drop out of tDepth beyond about 150 km (Surfmgr2.cpp:1300). This is harmless inside the ±150 km far map, but it should be stated.
4. §4: "±0.4 h of solar time across ±150 km at 45°" is ±0.13 h (1.9° of longitude).
5. The f_tod switch to the polar constants is discontinuous: the sunrise peak of 1 jumps to 0.15 as nightLen → 0. Blend toward the constants over the last hour of night or day length, and add 66° to test 15.2.
6. **Retrograde rotation.** Take the sense of rotation from the sign of dλs/dt (the subsolar longitude now vs. the last frame) rather than oapiGetPlanetPeriod. That needs no API assumption.
7. FogSunT ignores the drift noise. The hash is integer and pure, so put it in FogMath and let the CPU follow holes in front of the sun.
8. **Uncovered texels.** Bake should mark them as no fog (m = 0, Horizon::NODATA), not F = 0. FogUpdate should not validate a far map with < 90 % of its texels covered, as at start-up before the owners load.
9. **GfxPool starvation.** FOGMAP runs after every queued HORIZON job (GfxPool.cpp:131-134, MaxQueue 64). On a 1-worker pool in low flight the near map can lag by seconds. This is acceptable, but say so in §6 and §10.
10. **Grid copy cost.** 25 × 268 KB = 6.7 MB of fresh vectors under the loader mutex is about 1 ms with page faults, not 0.3 ms.
    - Cache `shared_ptr<const Horizon::Grid>` per (lvl, ilat, ilng, horGen) in FogState and copy only new tiles.
    - For the far cascade, copy every 2nd node.
11. resource.h 3054 collides with the shadows design's IDC_FX_SCENESHD 3054. The plume design's table gives fog 3058. The fog's dialog slot (380, 416) is the only one of the five that does not overlap (the other four all claim 290, 428).
12. The cloud shadow on the fog: at low sun, q leaves the ±100 km cloud map, and Tc jumps to 1. Fade vCld.y with the uv edge, as Coverage's box test.
13. **Cost.** P(cosθ, k) is constant along the ray (cosθ = dot(d, s)). Compute the 3 octave phases once per pixel, which leaves only 3 exp per step.
14. **Integration notes.**
    - Write the merged AfterOpaque order in §2: DepthPass → Flag → SSAO → VolShadow → SSGI → [Fog | Clouds | Fog] (SSGI §2 puts SSGI before Clouds).
    - design-scene-shadows.md:306 lists fog edits in NewPlanet.glsl, Tree.glsl and the mesh shaders. Fog touches none of them.
    - SceneFx.h/.cpp is shared by fog, plumes (AfterOpaque signature, depthFull), SSGI and SSR. One integrator should own it.

Counts: MUST 4, SHOULD 10, NIT 14.
