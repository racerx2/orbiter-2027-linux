# Code review: seamless far clouds (cloud atlas), branch cloud-seams

**Verified:** I checked the following and found them correct:
- **Job lifetime:** the job captures values only, the shared_ptr Box outlives the manager, Unregister is called, and the VkTex delete is deferred with ForgetTexture.
- **Mesh uv:** Quad and Hemi match CreateMesh_quadpatch (tu = j/grd, tv = 0 at maxlat, range not baked) and CreateMesh_hemisphere (tu = a·x + du, tv = colatitude/π).
- **Shadow mapping:** the surface rot follows the legacy edglng convention. The `tl` walk matches the GetParentSubTexRange chain. SearchTile(bOwntex) returns the owner's level.
- **Uniform layout:** scalar offsets are 416/432 and the size is 448.
- **CloudAtlas=0:** no macro, no Step and no SetTexture.
- **Shaders:** dFdx and the atlas taps sit only under uniform `if`s. VkConvertPixels with (0,0,1) keeps the size, and levels 0 gives a box-filtered mip chain with alpha in byte 3. The Unseam wrap indices are right.
- **Tests:** CloudAtlas.Test passes (1080 assertions).

## MUST
1. **NewPlanet.glsl:170-174 (used at 520, 884, 951-954, 1140): AtlasEdge ignores how the tile is sampled.**
   - **Problem:** within 3 mip-0 texels of every tile texture edge, AtlasEdge forces atlas weight 1 at any distance.
   - **Where it shows:** wherever a tile is magnified, a 0.5–7 km strip along each cloud tile edge shows the 19.5 km/texel atlas. That covers the layer seen from below or nearby with VolClouds off, and the ground cloud shadows seen from an aircraft. Before, there was a step of about 3/255 there. Now there is a soft straight band in the sky and in the shadows, which breaks design §3 (near the camera the output must be bit-identical).
   - **It also misses the seam it is meant to fix:** at tile mip k the clamp seam is 2^k texels wide, but the band stays at 3 texels.
   - **Fix:**
     - Compute the tile LOD `lt` once per pixel, before the taps: the same anisotropic rule as AtlasW, applied to uvT·512.
     - Pass `lt` to AtlasEdge and return `(1.0 - smoothstep(0.5*b, 3.0*b, min(e.x, e.y))) * smoothstep(0.25, 1.0, lt)` with `b = exp2(max(lt, 0.0))`.
     - Magnified tiles then stay bit-identical.

## SHOULD
1. **CloudAtlas.cpp:54, 64: Unseam takes `d` from a single pixel pair per row (or column).**
   - **Problem:** the design measured the cross-joint step at 8.5, against 7.0 inside a tile, so `d` is mostly natural texture variation. Ramping each row by its own ±d over 8 px turns that variation into streaks across every joint of the atlas.
   - **Fix:** low-pass `d` along the joint (for example, the mean over ±4 rows or columns, wrapping in x) before ramping. In the test, check the joint step against the smoothed step instead of requiring ≤ 1.
2. **OVP/VulkanClient/doc/VulkanClient.html:616 and README.md:218: `CloudAtlas` is not documented.** Design §4 asks for a Ground graphics line.
   - **Fix:** add "Seamless far clouds (`CloudAtlas` 1)" to both lists.

## NIT
1. **NewPlanet.glsl:155: the AtlasW comment is stale.** It says "away from tile edges" and "1.4x". AtlasW does not involve edges, and the ramp runs from 0 at 2x magnification to 1 at 1.19x. Also, VPlanet.h:109 and NewPlanet.glsl:120 do not mention `z`.
   - **Fix:** describe the ramp as "0 at 2x magnified, 1 at 1.19x", and add "z 1: texture from file level ≤ 5" to both struct comments.
2. **VPlanet.h:111: only `sizeof` is asserted.** Swapping two float4s keeps the size, so the assert would not catch it. Design NIT 3 asked for the offset check as well.
   - **Fix:** add `static_assert(offsetof(ShaderParams, vCldAtlas) == 416)`.
3. **Cloudmgr2.cpp:124-129: the mean-alpha loop runs on the render thread.** It reads 2M pixels just for a log line.
   - **Fix:** compute it in the job and store it in the Box.
4. **Cloudmgr2.cpp:94, 133: missing sources are not checked before submitting.** The job is submitted even when `!loose && !tree`, and a body without level-5 cloud data logs LogErr.
   - **Fix:** when neither source exists, set state 3 without submitting, and log with LogWrn.
5. **CloudAtlas.cpp:46: Unseam silently assumes 2·band ≤ tile.** Otherwise the ramps of neighbouring joints overlap and read values that were already shifted.
   - **Fix:** assert this, or clamp `band` to tile/2.
6. **Tests/CloudAtlas.Test.cpp:133-152: two cases are missing.** There is no corner check (a vertical joint inside the horizontal band, at y = tile−1 and y = tile), and no Hemi case with rot ≥ 2π or rot < 0.
   - **Fix:** add both cases.
7. **NewPlanet.glsl:159, 172: the sizes 2048, 1024 and 512 are hard-coded.**
   - **Fix:** pass the atlas size in `vCldAtlasW.w`, or add a short comment `= CloudAtlas::W, H, TILE`.
