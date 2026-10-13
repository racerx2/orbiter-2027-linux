# Code review: TAA and volume cloud shadows (visual batch)

Repo Orbiter-linux-custom, branch visual-fx: 6189b7a9 hooks plus the uncommitted TAA and VolShadow files. Paths are under OVP/VulkanClient/ unless shown otherwise. Read-only review: nothing was built or run.

**Verified.**
- **TAA maths.** Checked by hand:
  - Rigid() and the row_major `v * M` layout match D3D row vectors.
  - Jitter sign end to end. `_31 += 2jx/W, _32 -= 2jy/H` moves the image by +j. CameraRays and SSAO subtract `_31/_11, _32/_22`. PSTaa adds +j to prevUV and centres the Blackman-Harris fit at (i,j) − j.
  - Halton H(n%8+1), the sphere roots, the Catmull-Rom weights, the YCoCg/Tm round trip and the NaN/Inf guards.
  - Reset, first-frame, frame-gap and skip paths.
- **Passes and state.**
  - The StretchRect resolve into RGBA16F works, and the blit and DrawCopy back into MSAA and single-sample targets works.
  - Push/Pop and SetState restore the main target.
  - FlagPass passes at equal depth: the default is LEQUAL and the vertex shader is the same.
  - The MRT output 1 is cleared after the march.
  - Every new VkTex, VkSurf and IPI is freed.
  - TAA=0 and VolCloudShadows=0 take the legacy C++ path and macros. Uniform names and bindings match. Derivatives in branches are harmless: min and mag filters are equal and there are no mips.
- **VolShadow.**
  - The sun ray from P goes through Slab(): exit from the base sphere, the 6 km cap and the planet stop.
  - 12 midpoint steps with VolMarch's clump fade.
  - The depth normal faces the camera, and the alpha follows S8.
  - Blend op 1 over the target, at half-res sizes.
  - A failure logs once and leaves the clouds on, and the pass runs before Clouds (M3).

## MUST
None.

## SHOULD

1. **PostTaa.glsl:109-114 (PostTaa.cpp:155-160): every depth-0 pixel below the cloud shell reprojects at the 7 km shell.**
   - **Problem:** Stars, planets and the Moon get a false parallax.
     - Example: a DG at 1 km and 200 m/s at 47 fps, looking 30° up to the side. The shell is about 12 km away along the ray, so stars move about 0.5 px per frame (4.3 m / 12 km × 1480 px/rad).
     - α stays at 0.1 and the history is read half a pixel to a pixel off a 1-px star. That history is dark and lies inside the box, which the star itself widens.
     - Point sources therefore fall to a fraction of their brightness and smear on night flights below 7 km (Earth CloudAlt = 7e3).
   - **Fix:** for depth-0 pixels under the shell, form two prevUVs: the shell, through mPt, and infinity, through mSky. Fetch both histories and keep the one nearer c0y in YCoCg(Tm) before the clip.
   - The extra 5 taps run only on those pixels, and the 2D layer still reprojects correctly (S2).

2. **SceneFx.cpp:303-321 (FlagPass), PostTaa.cpp:112-123: only oapiCameraTarget() is flagged.**
   - **Problem:** Vessels docked or attached to the target move rigidly with it, but they reproject as ground.
   - In a LEO chase of a ship docked at a station, the camera moves about 155 m per frame against the ground. Every station pixel's prevUV is then off screen or wrong:
     - α = 1 every frame: no AA, and the residual jitter shimmers, which is worst without MSAA;
     - or a clipped history from the wrong place.
   - **Fix:** in FlagPass, draw every vessel of the target's superstructure into tFlag.
     - Walk it from vVessel::GetInterface() recursively: DockCount()/DockingStatus(i), and AttachmentCount(false/true), GetAttachmentHandle and GetAttachmentStatus.
     - mVs (the target's Rigid) is exact for all of them.

3. **SceneFx.cpp:405: VolMarch steps ×0.75 with TAA.**
   - **Problem:** Rev 2 S2 says "VolMarch steps stay at full count", and design review SHOULD 2 says to keep them until verified.
   - The cut also applies when TAA is not running (see NIT 2).
   - **Fix:** remove the line. Or, if the Alps High check is meant as the verification, record the measured noise and time in the design and gate the line on TAA actually running.

## NIT

1. **Scene.cpp:1596: `bLabelsLate = bJitter`.**
   - **Problem:** Reset frames have no jitter: the first frame, an F1 switch, or a focus or target change.
     - So labels, planetarium art and surface labels are drawn early.
     - PSTaa's reset path then copies them into the history, and they ghost faintly on the next frames until the clip removes them.
   - **Fix:** have BeginFrame report whether Resolve will run (!skip), and set bLabelsLate from that, not from the jitter.

2. **TreeMgr.cpp:548 and SceneFx.cpp:272-276, 405: TAA side effects are keyed on Config->TAA, not on TAA running.**
   - **Problem:** With PostTaa failed (`ok` false) or skipped (mesh-debugger camera, wireframe):
     - the tree screen-door crawls every frame with no TAA to average it;
     - VolMarch runs at 0.75 steps;
     - DepthPass and FlagPass run every frame for no reader.
   - **Fix:** use scn->IsJittered() for the tree phase. Add PostTaa::Active() (ok && !skip, reset frames included, since they need tDepth) for the passes and the step count.

3. **PostTaa.glsl:48-54 and 65-71: two blocks track published sample code term for term.**
   - History() matches MJP's SampleTextureCatmullRom: the same Horner forms, `floor(p - 0.5) + 0.5`, w2/w12 and t−1/t+2.
   - Clip() matches Playdead's clip_aabb: r / max|r/e|.
   - Both sources are MIT. Their content is fixed by the maths, but the project rule bans external code.
   - **Fix:** credit both in the header line next to Karis and Salvi, or restate them from the spline and box definitions.

4. **Scene.cpp:1706: the celestial sphere's planetarium overlays are still drawn before pass A with the jittered matrix.**
   - These are the grids and grid labels (CelSphere.cpp:345-405), drawn inside m_celSphere->Render. S3 moved only the plist labels, RenderSceneArt and the surface labels.
   - **Problem:** With planetarium on, the lines and text soften like the stars. There are no trails, since they reproject by mSky.
   - **Fix:** document this, or split the overlay part of CelSphere::Render into the bLabelsLate block.

5. **SceneFxShadow.cpp:38-55: the shadow IPI is built on the first frame with volume clouds.**
   - **Problem:** That is mid-flight, when descending to the layer. Three glslang compiles of VolCloud.glsl and Scatter.glsl cause a one-off stall there.
   - **Fix:** build it in the SceneFx ctor when VolClouds && VolCloudShadows. That is still only with the switch on.

6. **VolCloud.glsl:135-139 (6189b7a9): the PixelRay() extraction is not under a macro.**
   - **Problem:** VolMarch's SPIR-V changes with every switch off. The arithmetic is the same, so the pixels match, but rule 0 says "no SPIR-V change when off".
   - **Fix:** accept it and note it in the design rules, or keep the inline expression in the legacy path.
