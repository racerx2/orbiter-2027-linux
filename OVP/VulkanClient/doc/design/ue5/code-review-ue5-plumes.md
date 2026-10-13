# Code review: volumetric plumes, smoke and steam, re-entry plasma (UE5 batch)

Repo Orbiter-linux-custom, branch ue5-batch: 35e2949c plus the plumes owner's uncommitted files, final state after `<!-- DONE -->` (PostPlume.cpp 17:13:54, build b7). Reviewed whole: PostPlume.h/.cpp, PlumeMath.h/.cpp, shaders/Plume.glsl, Tests/PlumeMath.Test.cpp, Config/GC/Plume/*.cfg; the diff of Particle.h/.cpp, VVessel.h/.cpp, VObject.h, PostLook.cpp; and the hooks they rely on (Scene.cpp 1965/1977, SceneFx AfterOpaque/DepthPass/Depth, SceneFxFog FogAlongRay/FogTex, IProcess Execute, ShaderClass::Setup). Paths are under OVP/VulkanClient/ unless shown otherwise. Read-only: nothing was built or run.

**Verified.**
- **Maths (PlumeMath.cpp), checked by hand:**
  - AreaRatio, PressureRatio and its inverse, Prandtl-Meyer and ν(∞) (Anderson); Pack's L_s = 1.306·sqrt(M_j²−1)·D_j in exit radii; r_j/r_e = sqrt(A(M_j)/A(M_e)); PlumeRadius is 1 at x = 0 and continuous; Simons cos^(2/(γ−1)) with (1/(1+d))².
  - ExitArea from Orbiter's pfac; ExitPressure clamp; JetState clamps p_a ≥ 0.01·p_e and turns the jet off below w_atm 1e-3, so vacuum is finite (review S3).
  - Wyman 2013 lobe coefficients and the sRGB matrix; PlasmaColour nodes and clamps; PlasmaIntensity uses q·V = ½ρV³.
  - ChordIntegral/ChordFull (4/3·h³/R²), HG normalisation, BandIntegral and its cap, DeepOpacity order independence.
  - FlowViewProj is D3DXMatrixLookAtRH × OrthoOffCenterRH; FrustumPlanes for row vectors; Cluster keeps the SSMEs as 3 and packs 33 (M4).
  - The 16 test cases match design 17 with N2, N3 and S3.
- **Shader field (Plume.glsl).** The capsule/sphere intersections, the 8-interval merge and ≤ 48 steps, Hillaire's step `src·(1−e^(−σdt))/σ`, the haze and fog blend `C·Tr·Tf + (1−T)(In·Tf + fogC(1−Tf))`, the ellipsoid fallback (mat3 columns e0..e2, `q*M` and the normal `M*(qe+g·t0)` are right), the flow-map u mirror of the back map, 1−z to metres, the Newtonian (n·f)², the reduce and blur layout, the puff index layout (64 puffs × 6 texels per row) and the parabolic chord. No division by zero or negative sqrt: every divisor is clamped (r_e ≥ 0.02, steps ≥ 0.1·r_e-ish, λ_h > 0, D_b ≥ 0.03, lf ≥ 0.03) and every emission term is ≥ 0, so SoftCurve never sees a negative input.
- **Switch off (VolPlumes = 0).** PostPlume is never created, `inst` is null: LegacyWeight and LegacyReentry return 1 (VVessel.cpp:989-994 calls RenderExhaust with the untouched `es`, 1785 multiplies by 1.0f), HazeShape leaves PostLook.cpp:116-117's old values, Particle.cpp:414 never returns early (plumeFrame 0), the FLOWMAP branches (VVessel.cpp:689, 808) never run, Plume.glsl is never compiled and no existing shader changed. SceneFx's `more` and DepthWanted only add VolPlumes. HullBox (1880-1881) is computed but unused.
- **Legacy and add-ons.** Exhausts with no thruster, NULL lpos/ldir, lsize ≤ 0, zero level, zero F0 or Isp, or huge Isp fall to the texture class or stay legacy; an unclaimed or culled exhaust keeps weight 1; groups beyond 32 stay legacy; Nozzle and ClassCfg pointers live in std::map (stable). Re-entry streams are never puffs (M3). Claims expire by themselves (frame + 1) and only the main pass honours them, so custom cameras, env maps and the flow/depth passes draw legacy.
- **Re-entry billboard.** RenderReentry is only called from vVessel::Render(VkDev*) in the opaque loop, before Collect, and is scaled by the previous Collect's reentryW (0 for every member of a drawn plasma primitive, MAXPZ-limited), 1 in other passes and after 2 stale frames. DepthPass, FlagPass and FlowMaps call Render(dev, false), which does not draw it. Never twice (M2); at most one frame of overlap or gap at the onset/end.
- **Fog contract.** FogAlongRay(cam global, unit dir, dist) is called per plume midpoint and per puff light sample before AfterOpaque (it accepts last frame's fog); FogTex() rgb/a gives the fog colour; with no fog the flag is off and Tf = 1. Matches Rev 2 S4.
- **Depth.** tDepth is camera distance; 0 maps to far before the 2×2 min (N10); the march, puffs and composite all use the same convention; PostPlume reads SceneFx::Depth() only (M5). Collect's new plane fix (w > 0 instead of the pass's near/far) is right: column 3 of a row-vector projection is the unit view axis.
- **Passes and state.** Render saves VkDev::State, re-pushes the main target and restores it, as PostLook::Haze. Puffs blend ONE/ONE_MINUS_SRC_ALPHA with blendSeparate off, so A' = As + Ad(1−As) (D3D9Util.cpp:1847). The march MRT output 1 is cleared after use. FlowMaps pushes RENDERPASS_FLOWMAP (0x000B, labels grown) and pops the pass. IPI only binds the samplers its active shader reads, so stale pTextures from the previous pass never alias the current outputs.
- **Lifetime.** Every VkTex, VkSurf, IPI, ShaderClass, the Exhaust.dds handle and the three maps are freed; the noise job holds a shared_ptr box and the pool id is unregistered. The TEMP per-frame DBG log is gone and PLUME_TIMING is 0.
- **Cost.** The owner's live measurement: 0.06-0.10 ms GPU for two DG plumes, Collect 0.006 ms.

## MUST

1. **PostPlume.cpp:1233-1235 with Plume.glsl:586-587: puffs are invisible in any frame without a plume or plasma primitive.**
   - **Problem:** March() clears tVolPD to 0 and returns before the march when `plumes` and `plasma` are empty, so tVolPD.g (the half-res scene distance) stays 0 everywhere. Composite still runs for the puffs, and PSPlumeComposite weights every tap by `DistW(min(d,1e7), min(dt,1e7))` with dt = 0, which is exp(−2d): it underflows to exactly 0 in fp32 for any scene distance above about 50 m and for every sky pixel (d → 1e7). ws falls to the 1e-6 floor and F and B come out 0.
   - The streams were already claimed in Collect, so the legacy particles are also skipped (Particle.cpp:414): the smoke disappears.
   - **Failure:** SRB contrails after burnout; a contrail seen while the engines are behind the camera or under 1 px (GatherPlumes culls them, plumes empty); the pad ground cloud after engine cut-off; any add-on smoke stream with PUFFS 1 and no lit engine. All draw nothing.
   - **Fix:** do not depend on the march for the puffs' upsample depth. Either (a) in PSPlumeComposite take dt from tDepthP directly (the min of the 2×2 full-res texels of tap q, as SceneDist() does), or (b) when the march is skipped and nPuff > 0, run PSPlumeMarch anyway (it returns at nh == 0 after writing primDist = (0, dsc)), or (c) pass a flag and use plain bilinear weights for F and B when !vol. (a) also removes the composite's dependence on the march rect.

## SHOULD

1. **Plume.glsl:275 (smoke tail self-shadow): the sun path is negative outside the tail cylinder on the sun side, so the tail is lit more than by the bare sun.**
   - **Problem:** `tex = (−b + sqrt(max(b²−cc, 0)))/spl` is the far root of |rv + sn·s| = Rl. For a sample outside the cylinder (cc > 0) on the sun side (b > 0) both roots are negative, so tex < 0 and `tsun = exp(−k·tex)` > 1. On the shadow side outside, it counts the gap outside the cylinder as smoke.
   - Example: solid class at x ≈ L (C3.w·dens ≈ 3), sun across the axis, r = 1.5·Rl: tex = −0.5·Rl, tsun = e^0.83 = 2.3 instead of 1. The capsule bound (2× the tail radius) makes these samples real, so dense SRB and kerolox tails get a bright fuzzy rim on the sunlit side.
   - **Fix:** use the chord inside the cylinder: `D = b² − cc; tex = D > 0 ? (max(−b + sqrt(D), 0) − max(−b − sqrt(D), 0))/spl : 0`.

2. **Plume.glsl:221 and 227: a hard switch at w_atm = 0.5 pops the plume during ascent.**
   - **Problem:** `norm` jumps from 1/(1.8R) to 1/(2r_e) and the texture-class flame uv jumps from (r/R, x/L) to (θ, d/L) in one frame, while ρ itself is a smooth mix.
   - With p_e = 25.5 kPa the switch is at p_a ≈ 4.2 kPa (about 20 km). There the jet bulges (R ≈ 2·r_e), so the core brightens about 1.8× in one frame, and the DG/XR texture pattern changes shape at once.
   - **Fix:** blend with a smooth weight, e.g. `float wj = Ss(0.3, 0.7, watm); norm = mix(1.0/(2.0*re), 1.0/(1.8*R), wj);`, and fetch both atlas uvs and mix them with wj (or fade the two mappings over the same range).

3. **Plume.glsl:756 (PuffPS): the lit-smoke curve is applied even with _LIGHTGLOW.**
   - **Problem:** design 6.5 says "unless _LIGHTGLOW". The march (548) and the meshes (Metalness.fx:404, PbrMesh.fx:295) skip their curve there, because LightBlur tone-maps later.
   - **Failure:** with PostProcess = 1 the smoke is compressed twice (here and by LightBlur), so it is darker and flatter than the hulls and the plumes next to it, and it does not bloom.
   - **Fix:** `#if !defined(_LIGHTGLOW)` around `col = LitCurve(col);`.

4. **PostPlume.cpp:866-890 (GatherPuffs): the 8192 cap and the fill budget count puffs that are off screen.**
   - **Problem:** puffs are sorted by distance and kept nearest-first with no frustum test, and every kept puff adds its projected area to the 32-screen budget.
   - **Failure:** at a launch, the pad ground cloud behind the camera (384 puffs per vessel at up to ~90 m radius, close by) can fill the budget, so the visible contrail ahead is thinned to a hashed subset. Its density and noise then change as the camera turns. With long contrails, off-screen near puffs also take the 8192 places of visible far ones, which then vanish because their stream is claimed.
   - **Fix:** test each puff against `planes` (sphere: n·c + w ≥ −r, the w > 0 plane included) before it enters `order`; the inside test stays as it is. The puffs that are left out still need their stream claimed, which is right because they would not be visible either.

## NIT

1. **Plume.glsl:360 and 376: λ is computed but never used.** BandAt adds `min(1/|cos|, 8)` without the λ factor of design 7.4 and BandIntegral, so st.lam, Z5.y (λ_min ≥ half a flow texel, review S2) and the "inflate below 2 px" step have no effect on the band. If the band is meant to be normalised to unit thickness [V], remove st.lam and say so in the design; otherwise multiply by st.lam.
2. **Plume.glsl:537: the "pixel footprint" term of the end-on-hull tolerance is about 1/50 of a pixel.** `0.02·dsc·vTex.x·2` is 0.02·d·2/W, while one pixel spans d·2·vCamY.w·vTex.y. Use `dsc * 2.0 * vCamY.w * vTex.y` (one full-res pixel) or the half-res pixel.
3. **PostPlume.cpp:88: Hash() is Wellons' lowbias32 term for term.** It has the same shifts and the 0x7feb352d and 0x846ca68b constants. The source is public domain, so there is no licence problem, but the project rule asks for own code or a credit: add "lowbias32 (Wellons, hash-prospector)" to the comment.
4. **PostPlume.cpp:39-45 and 1016-1037: the PLUME_TIMING block is still in the file.** It is off, but with 1 the static tsPool is never destroyed (a validation error at device destroy), and every frame gets two extra EndRendering splits. Remove it, as the vshd owner did, or destroy the pool in the destructor.
5. **PostPlume.cpp:974-979 and 319-335/1049-1066: Revoke() does not reset reentryW, and bakeQueue outlives a frame without Render.**
   - In the first case the billboard stays off while no plasma draws. In the second, Bakes() later dereferences `SURFACE(s.h)` for a handle that an add-on may have released (use after free).
   - Both paths are unreachable today: with VolPlumes on, DepthPass runs every frame and Render always follows a non-empty Collect.
   - Fix: clear reentryW in Revoke. In Collect, drop the queued slots: set `s.h = nullptr` for each queued slot and clear bakeQueue.
6. **Plume.glsl:607: the mean colour reads mip 20.** A texture without mips gives its centre texel. Fall back to a small box average of mip 0, or accept it.
7. **Cost, extreme inputs.**
   - PlumeMath.cpp:270: rc is unbounded. A stream of big clumped puffs splats up to 48² cells each, which is O(n·N²): about 14 M sqrt for 6000 puffs. Clamp rc, or splat a fixed 5×5 footprint.
   - PlumeMath.cpp:324: Cluster builds and sorts O(n²) pairs every frame. A vessel with hundreds of lit exhausts allocates and sorts ~10⁵ entries. Build pairs only when uf.n > budget.
8. **PostPlume.cpp: nozzles and groundAcc are never pruned.** A deleted vessel whose OBJHANDLE and thruster pointer are reused with the same F0, Isp0 and wsize keeps the old class's cfg result. Drop entries whose handle is no longer a vessel, at the same place UpdateGround prunes ground.
9. **PostPlume.cpp:1070-1122: both flow maps are drawn every frame.** Design 7.3 said the second every other frame. This is a small cost, about 4 hull draws.
10. **Plume.glsl:548: the march applies SoftCurve after the haze, curve(C·Tr + In).** The meshes do curve(C)·Tr + In (Metalness.fx:404-412), so there is a slight mismatch in thick haze only.
11. **Integrator (SceneFx.cpp AfterOpaque `more`): with VolPlumes on, DepthPass runs every frame even when Collect found nothing.** Collect runs first and knows. This is moot while SSR and SSGI default to 1. If those are turned off, skip the pass when PostPlume is the only reader and nothing is in view.

Counts: MUST 1, SHOULD 4, NIT 11.
