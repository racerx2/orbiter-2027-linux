# Design review: visual batch

Checked design-visual-batch.md against Orbiter-linux-custom (branch cloud-seams, with the uncommitted cloud-atlas changes). Paths are under OVP/VulkanClient/ unless shown otherwise. Read-only: nothing was built or run.

## MUST

1. **Vessels have no motion in the TAA reprojection.**
   - §3 reprojects every tDepth pixel as a static point of the proxy planet.
   - Vessels are in tDepth (SceneFx.cpp:275, VVessel.cpp:799), and nothing marks them as vessels.
   - In a chase view of a flying vessel, the vessel stays still on screen while its prevUV follows the ground. The history then comes from the wrong place every frame. The main subject smears and shimmers, and TAA=1 is the default.
   - Fix: in DepthPass, write an object flag as a second R8 target (SceneDepth.glsl and NewMesh.glsl SceneDepth_PS, under _TAA): 1 for oapiCameraTarget's vessel, 0 for everything else.
   - Reproject the flagged pixels with that vessel's rigid motion: M = inverse(mWorld_now)·mWorld_prev·Vprev·Pprev (mWorld: VObject.h:267).
   - Other vessels keep the planet frame and rely on the clamp.

2. **Jitter shows whenever the history is dropped.**
   - α = 1 outputs the raw jittered frame.
   - The 2 km rule drops the history every frame in orbit at time warp ×20 and up (7.7 km/s · warp / fps). For as long as the warp lasts, the whole picture shakes by ±0.5 px.
   - Fix: drop the distance rule. The planet-frame reprojection is exact for static ground at any speed, and an off-screen prevUV already sets α = 1 per pixel.
   - Decide any full reset before Scene.cpp:1430, and leave jitterOn false for that frame.
   - Also reset on:
     - a proxy change (Camera.hObj_proxy);
     - an internal/external switch;
     - the mesh-debugger camera;
     - any frame after one where pass A did not run.

3. **Volume shadows are composited over the volume clouds.**
   - §2A and §11 order the passes SSAO, volume clouds, then volume shadows.
   - PSVolShadow reads tDepth, which holds the ground behind a cloud. The black composite therefore darkens cloud pixels that sit in front of shadowed ground.
   - Fix: run the shadow pass between Ssao() and Clouds() (SceneFx.cpp:259-260).

4. **The SSAO blur and composite can't be reused.**
   - pAO exists only with SSAO=1 (SceneFx.cpp:38, 57-70).
   - PSComposite returns 0 beyond vFade.y = 600 m and is scaled by the AO strength (SSAO.glsl:102, 118-119).
   - Fix: reuse VolBlur (VolCloud.glsl:194-208), which already does a depth-weighted 3×3 inside pVol and works on any channel.
   - Add an own composite entry with VolComposite's 2×2 depth-aware upsample.
   - Compile PSVolShadow only when VolCloudShadows=1, under #ifdef PS_VolShadow. IsOK() fails the whole IPI if any entry fails (IProcess.cpp:666-673), which would switch the volume clouds off.

5. **Filmic misses the sky dome.**
   - HazeMgr.cpp:270-271 compiles HorizonPS and HorizonRingPS from NewPlanet.glsl with NULL flags. Both call HDR() (NewPlanet.glsl:321, 352).
   - With Filmic=1 the sky keeps 1 − exp while ground and clouds get f(), which leaves a step at the horizon.
   - Fix: pass the flags string with _FILMIC there too.
   - The planet flags are built in three separate blocks (VPlanetAtmo.cpp:121-134, 138-154, 156-170), and each block needs _FILMIC.

6. **The 0.57 calibration doesn't hold, and two of the curves have a different shape.**
   - Against 1 − exp(−x) over x = 0.3–2:
     - 0.57 is the best single scale (minimax 0.568), but the error still reaches +0.037 at x = 0.7 and −0.035 at x = 2;
     - so the PostMath test "within 0.03" fails;
     - at x = 3–4, f is 0.05 lower, so highlights drop.
   - Metalness.fx:388-394, LightBlur.glsl:61-64 and 140, and PbrMesh.fx:538-541 use x(1+x⁴)^−¼, which is almost linear up to 0.8:
     - at x = 1, f(0.57x) gives 0.658 against 0.841;
     - at x = 2 it gives 0.830 against 0.985;
     - vessels, and the whole PostProcess=1 image, would go much darker.
   - In PostProcess=1, LightBlur's curve runs over planet values that HDR() has already curved. Replacing both curves tones the planet twice.
   - Fix:
     - set the test tolerance to 0.04, or fit over x = 0.3–1.5 only;
     - keep the x(1+x⁴)^−¼ users on their own curve, or give them a separately fitted scale;
     - leave LightBlur's curve unchanged.

7. **PbrMesh glass undoes the old curve.**
   - Transmitted() inverts x(1+x⁴)^−¼ on the scene copy (PbrMesh.fx:273-276).
   - If the display curve changes under _FILMIC and the inverse does not, every transmission background shifts in brightness.
   - Fix: under _FILMIC, invert f() in closed form (the root of a quadratic), then undo the 0.57 scale.

8. **The second wave octave seams at every tile edge.**
   - vUVWtr is tile-local. Every tile up to L14 starts at 0 and spans 2^dlev repeats (Tilemgr2.cpp:219-231, NewPlanet.glsl:478).
   - A 0.37 scale and a 63° turn are not periodic on the unit square, so the second layer jumps at every L14 tile edge (about 1.2 km).
   - Fix: map the uv with an integer lattice, e.g. uv2 = vec2(2u + v, 2v − u). That is a 26.6° turn, √5 finer.
   - Scroll each axis by a whole number of repeats per 3600 s, because Const.Time wraps at 3600.
   - Do the same for the foam noise: ClsNoise with an integer period (NewPlanet.glsl:466-471).

9. **The normal-strength formula is backwards.**
   - cNrm.z *= Const.wNrmStr (NewPlanet.glsl:593), so a larger wNrmStr flattens the waves.
   - Multiplying wNrmStr by a factor that falls to 0.15 makes far normals about 6.7× steeper, the opposite of the intent.
   - Fix: after decoding, scale cNrm.xy by k = mix(1, 0.15, smoothstep(2e3, 30e3, dst)). Keep z *= wNrmStr.

## SHOULD

1. **§12 puts glass in the wrong place.**
   - Transparent PBR groups draw in RenderPbrDeferred after AfterOpaque, not before TAA:
     - exterior at Scene.cpp:1974, after the exhausts and particles;
     - VC at 2036.
   - The outcome is still fine:
     - the glass is unjittered and stays out of the TAA input;
     - the transmission copy (Scene.cpp:2654-2662) holds the TAA result.
   - Drop the note that BLEND groups reproject at the depth behind.
   - The groups that do ghost are legacy see-through groups. They draw in the main vessel pass and are left out of tDepth (Mesh.cpp:3114-3121).
   - Add a DG canopy check.

2. **Clouds have no reprojection depth, yet VolMarch steps are cut.**
   - Neither the volume clouds nor the 2D layer are in tDepth (SceneFx.cpp:266-279), so a cloud in front of the sky reprojects as if it were infinitely far.
   - Flying at 250 m/s under a layer at 3 km, that is about 1.6 px of error per frame, which smears.
   - VolMarch already computes the cloud distance tw/ts (VolCloud.glsl:180-181, 187).
   - Fix: write tw/ts to a second half-res R32F target. It is the same size as tVol[0], which IProcess.cpp:167-190 allows.
   - In PSTaa, use that distance where tVol[1].a > 0.3. For depth-0 pixels below the layer, use the cloud shell distance (SphereHit at Rp + cloudalt).
   - Keep the full step count until this is verified.

3. **Labels and markers drawn before pass A get jittered and blended.**
   - These draw into pBackBuffer at Scene.cpp:1633-1810 with the jittered matrices (Scene.cpp:1755, 1766, 1777; CelSphere.cpp:637):
     - planet labels and base markers;
     - surface labels;
     - planetarium and exterior sketchpad art.
   - Only the vessel markers come after A (Scene.cpp:1925).
   - Text goes soft. Surface labels have depth 0, so they leave trails while the camera moves.
   - Fix: with TAA on, draw that label block unjittered right after A. Labels then sit over vessels. Otherwise, document the softening.
   - Correct §2A, which says the markers come after A.

4. **tDepth can be a frame or more old.**
   - AfterOpaque returns before DepthPass when neither SSAO nor clouds are wanted (SceneFx.cpp:256). tDepth then keeps its old content.
   - With TAA off, the shafts' sky test and the haze depth test read stale silhouettes. This happens, for example, from 20 km up to CameraInSpace() 0.9.
   - Fix: add SceneFx::DepthValid() as a frame stamp, and use tDepth only when it was written this frame.

5. **The flare's cloud query has no data without VolClouds.**
   - CloudTile::Density() is filled only when VolClouds=1 (Cloudmgr2.cpp:201). With VolClouds=0 the flare shines through clouds, so one switch depends on another.
   - Threading is fine:
     - the loader takes its mutex only for queue changes (Tilemgr2.cpp:1039-1059);
     - Render already holds it (Cloudmgr2.cpp:377-387).
   - Fix:
     - keep the density when LensFlare=1 too;
     - do the lookup inside TileManager2<CloudTile>::Render;
     - convert lng with VolCloud::CloudLng(lng, cloudrot), as SceneFx.cpp:213 does.

6. **Flare visibility misses buildings, trees and 3D clouds.**
   - TerrainSunVisibility marches the terrain only (VPlanet.cpp:733).
   - pLocalResults covers vessels and the cockpit only (Scene.cpp:1193-1256).
   - Fix, in PSFinal:
     - multiply by the share of a few tDepth taps around the sun point that are 0, when tDepth is valid;
     - multiply by 1 − tVol[1].a at the sun point, when the volume clouds drew this frame.

7. **The flare and shafts need GBUF_DEPTH.**
   - GBUF_DEPTH, pLocalCompute and pLocalResults exist only with SunGlare or LocalLightsGlare (Scene.cpp:226-231, 283-286).
   - Without them, the design's fallback of 1 lets the flare show through vessels and the VC.
   - The shafts would also count the cockpit as sky, because tDepth only holds the exterior.
   - Fix: also create them when LensFlare or LightShafts is on. RenderGlares stays gated on bGlares (Scene.cpp:3891).
   - Sample tVis point/clamp at u = 0.5/32 and apply smoothstep(0.5, 1), as GlareVS does (Glare.glsl:131).

8. **The volume-shadow composite also darkens haze and back faces.**
   - Black-alpha blending scales the final colour, inscatter included (NewPlanet.glsl:820-821).
   - At 20–60 km most of a ground pixel is haze, so far shadows turn grey-black.
   - Fix: a = shadow · 0.75 · luminance(GetLandView(P).atn) · lit, with lit = saturate(dot(n, sun)) from the depth normal, as SSAO.glsl:118-119 does.

9. **Gate the 2D shadow fade per tile and per pass.**
   - Cloud and surface tiles share one ShaderParams (Cloudmgr2.cpp:247, 269; Surfmgr2.cpp:939), and CloudTile writes vVolCld.z into it.
   - So SurfTile::Render must write vVolCld on every tile: in the main pass and for the proxy planet only, as Cloudmgr2.cpp:268-269 does.
   - Use a weight that says the volume shadow really drew last frame. CloudFade() is set even when the shadow pass is off or failed.
   - Apply the fade before the fX line (NewPlanet.glsl:799).

10. **The sun glint from orbit almost vanishes.**
    - GGX_NDF peaks at 1/(π·rgh²) (NewPlanet.glsl:449-454):
      - raising rgh from 0.2 to 0.35 cuts the peak to ×0.33;
      - Schlick F0 0.02 against today's 0.15·wSpec at normal view is ×0.13;
      - together the peak glint falls to about ×0.043.
    - Fix:
      - use Schlick on V·H (fDCH, NewPlanet.glsl:602) for the glint, and on V·N for the sky;
      - cap the far roughness near 0.2;
      - add an orbit glint check that compares peak luminance with WaterFx on and off.

11. **Water line references and details.**
    - In Custom the water block is NewPlanet.glsl:587-628 and the specular is at 813-814. The design cites the Earth tree's 774-775.
    - fShadow already includes fH (line 545). The Flow.bHorizon branch must use fShadow in place of fH, not both.
    - cRfl is scaled by fSrf = 1 − CamSpace (lines 579 and 618), which is 0 in space. The new space-sky term must leave fSrf out.

12. **Mask mips are not a land fraction.**
    - Masks are punch-through DXT1 (Src/Orbiter/TileStreamImg.cpp:298-303), and archive masks are box-filtered back into BC1.
    - Every mip therefore keeps 0/255 alpha, so textureLod(...).a is only a 1-texel bilinear ramp. Foam follows a 200 m staircase, up to 100 m off the real coast.
    - With TileMipmaps=0 there are no mips at all (Surfmgr2.cpp:124, Tilemgr2.cpp:93-95).
    - A tile may use its parent's mask subrange (Surfmgr2.cpp:237).
    - Fix:
      - average 8 taps on a ring at a ~50 m mip, only where fMask > 0 and dist < 8 km;
      - take the texel size from vTexOff.zw and the mask width.

13. **Jitter ray rebuild: sign and slots.**
    - With D3D LH row vectors, ndc.x = x·_11/z + _31. The rebuilt ray must use (uv.x·2 − 1 − P._31) and (1 − 2uv.y − P._32), which subtracts the jitter rather than adding it.
    - VolPS has free .w slots: vCamZ.w, vMapN.w and vMapU.w (VolCloud.glsl:13-26).
    - SSAOPS has no free slot (SSAO.glsl:9-16). It needs a new member under _TAA, and pAO needs a ppf, since SceneFx.cpp:63 passes none.

14. **The Halton index starts at 0.**
    - H(frame%8) starts at H(0) = 0. The 8-phase mean is then (−0.0625, −0.111), so the "under 0.07" test fails.
    - Fix: use H(frame%8 + 1). Its mean is (−0.055, 0.000).

15. **The tree frame term has nowhere to go.**
    - vCam holds the camera (xyz) and the time (w) (Tree.glsl:12), so there is no room in it.
    - Fix: put frame%8 in vInsc.w, which is free (TreeMgr.cpp:537 and 545 set it to 0).
    - Use the frame counter, not the time. Time stops while paused, and the dither would freeze.

16. **Heat haze.**
    - Turn it off in vacuum, using the vessel's GetAtmDensity() or CameraInSpace() < 0.9.
    - Reject offset samples nearer than the nozzle by reading tDepth at uv + offset. Otherwise the foreground is pulled into the plume.
    - Draw only the bounding rect of the segments with SetTemplate (IProcess.cpp:214).
    - The API is there:
      - vVessel::GetInterface (VVessel.h:85);
      - MWorld() (VObject.h:106), updated at Scene.cpp:3558;
      - reuse the exhaust loop's filter (Scene.cpp:1945-1954) and GetExhaustLevel (VVessel.cpp:978).

17. **Test plan gaps.**
    - PostProcess=1 runs: TAA on the HDR offscreen target, pass B after LightBlur, and the late Measure.
    - True fullscreen without MSAA, and SceneAntialias=1.
    - Chase camera behind a DG at 200 m/s (MUST 1).
    - Time warp ×100 in orbit (MUST 2).
    - VC with MFD text, for sharpen and flare.
    - Planetarium and surface labels on (SHOULD 3).
    - Custom camera MFD and the env map, to confirm there is no jitter there.
    - Ocean at 1–3 km over L14 tile edges (MUST 8).
    - Orbit sun glint (SHOULD 10).
    - Filmic: the horizon (MUST 5) and the PBR glass bench (MUST 7).
    - TAA, SSAO and VolClouds off with the shafts on (SHOULD 4).
    - SunGlare=0 with the flare on (SHOULD 7).
    - Cost of the forced DepthPass from LEO. It draws every tile, base, building and tree in the visible disc (VPlanet.cpp:1333-1340).

## NIT

1. **The design uses Earth line numbers.** In Custom:
   - UpdateCameraFromOrbiter: 1430;
   - SetPhase(PHASE_ALL): 1919;
   - AfterOpaque: 1920;
   - exhausts: 1952;
   - particles: 1972;
   - RenderPbrDeferred: 1974 and 2036;
   - VC: 2018–2037;
   - Measure: 2042 and 2127;
   - LightBlur: 2061–2131;
   - RenderGlares: 2140;
   - overlay push: 2176.

2. **The pass placement is valid as designed.**
   - AfterOpaque is called only from the main pass, after the last opaque exterior draw (Scene.cpp:1920).
   - Restructure the early return at SceneFx.cpp:256 so TAA still runs.
   - After the IP passes, re-push the target, as SceneFx.cpp:261-262 does.
   - B fits between 2131 and 2138. Measure has already run there in both PostProcess modes (2042, 2127).

3. **The §12 checks are already answered by the code. Drop the "add them" step.**
   - PBR takes Scene's gVP on every RenderPbr call (PbrRender.cpp:47-48, 386-392; Mesh.cpp:2778-2797).
   - Only Mesh.cpp:2711 reads the projection, for the glass rects, where jitter does no harm.
   - NORMAL_DEPTH already draws PBR groups (VVessel.cpp:795-797; Mesh.cpp:3096-3108) and skips transmission groups (3098).
   - No opaque PBR draw comes after AfterOpaque except the VC, which is by design (Scene.cpp:2033).

4. **PbrRender's macros come from D3D9Effect::BaseMacros** (PbrRender.cpp:96, 330; D3D9Effect.cpp:297-329).
   - Add _FILMIC there only.
   - _TAA is not used by D3D9Client.fx or PBR, and would only add shader variants.

5. **Where each macro goes.**
   - _FILMIC also goes to:
     - TreeMgr.cpp:110;
     - SceneFx.cpp:78, as a string rather than NULL;
     - LightBlur's flags (Scene.cpp:302-306), whose 32-char buffer is large enough.
   - _WATERFX and _VOLSHD go in the Earth block only (VPlanetAtmo.cpp:157-170).
   - The PlanetShader ctor's find("_WATER") also matches _WATERFX (VPlanetAtmo.cpp:49). This is harmless while _WATERFX is only ever added together with _WATER.

6. **Clear the jitter unconditionally at the end of AfterOpaque.**
   - That includes the early-return path.
   - Set it only when TAA is healthy (depthOK, IsOK), and not in wireframe mode (Scene.cpp:1500).

7. **The TAA copy-back can be a StretchRect.** StretchRect already resolves and draws into an MSAA target (VkCore.cpp:1616-1670). Reuse history-next as the haze scene copy, which saves one resolve.

8. **Use vec4 arrays for uniforms.** BuildTable folds struct arrays to element 0 (VkShader.cpp:141), so declare vec4 vSeg[64] rather than an array of structs. 1 KB is well within limits.

9. **History filtering.**
   - Bilinear history sampling blurs over time. Use a 5-tap Catmull-Rom.
   - In PostProcess=1, weight samples by 1/(1+luma) so bright speculars don't flicker.

10. **Dialog.**
    - IDs 3045–3053 are free; 3044 is IDC_GG_CLOUDATLAS (resource.h:187).
    - Row 1 is already full to x = 500 (D3D9Client.rc:267-272).
    - A group with two checkbox rows needs about 36 units plus a gap, so the dialog must grow by at least 40, not 30.
    - Move OK (rc:265) and Credits (rc:247) down by the same amount.
    - VideoTab hooks: 922–929 and 1010–1020.

11. **The vignette r formula is garbled.** Use r = length((uv − 0.5)·vec2(W/H, 1)) / length(vec2(0.5·W/H, 0.5)).

12. **Gate pass B on LightBlur.** Run it only when !pOffscreenTarget || pLightBlur->IsOK(). Otherwise nothing has reached the backbuffer yet (Scene.cpp:2061-2131).

13. **Compute TerrainSunVisibility once per frame.** Do it before pass B, and let RenderGlares use that value instead of its own static cache (Scene.cpp:3915).

14. **Build files.**
    - Add PostFx.cpp and PostMath.cpp next to SceneFx.cpp in CMakeLists.txt:75.
    - Add PostMath.Test the same way as AutoExpo.Test (Tests/CMakeLists.txt:201-204).

15. **Foam time wrap.** The foam noise uses Const.Time, which wraps at 3600 s, so make it periodic over that span.
