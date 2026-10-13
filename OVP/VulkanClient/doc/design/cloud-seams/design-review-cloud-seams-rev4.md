# Design review: cloud seams, rev 4 (atlas as the cloud alpha)

**Verified:** I checked the data, the uv maths, R8 support and the answers to questions 1 and 5.
- **Data:** I measured both data sets on disk.
  - The streamed Earth set (Orbiter_linux_Earth loose tiles) has file levels 4–7 complete: 2/8/32/128 tiles.
  - The Custom Cloud.tree is complete through L9 (L10 is empty).
  - In both sets every Earth tile is DXT5 with rgb exactly 255 (luminance std 0). Taking only alpha from the atlas therefore loses nothing for Earth, and keeping the tile's rgb cannot show a seam (question 5).
- **uv maths:** Quad, Hemi and the cloudrot wrap are unchanged from rev 3 and still match the meshes.
- **R8 support:** R8 goes through DecodePixel, EncodePixel and VkLevelSize. Linear filtering of R8_UNORM is mandatory in Vulkan, so IPF_ANISOTROPIC|IPF_CLAMP_V works.
- **Question 1:** the claim holds at mip 0 for owners at exactly L. The differences are listed in NIT 1.

## MUST
1. **Getting alpha into R8.**
   - **Problem:** `VkConvertPixels(tile, R8_UNORM, SWZ_NONE)` encodes red, because EncodePixel takes v[0] = c[0]. Earth's rgb is 255 everywhere, so the atlas would be all 1: a solid overcast. With SWZ_ALPHA the encoded byte is correct, but VkCreateTexture then sets the {0,0,0,R} view swizzle (VkTexFile.cpp:27), so `.r` reads 0.
   - **Fix:** keep the per-tile BGRA conversion, copy byte 3 into the R8 buffer, and keep the atlas at swz = SWZ_NONE. Add a unit test on a synthetic tile where r ≠ a.
2. **Memory of the mip chain.**
   - **Problem:** `VkConvertPixels(R8 → R8, levels 0)` at 8192×4096 decodes to RGBA floats at 16 B per pixel. That is 512 MiB for level 0, plus 128 MiB for level 1. The swap keeps the 512 MiB capacity for the whole loop. Adding the input (32 MiB) and output (43 MiB) gives about 0.7 GiB per body, and Unseam's float copy adds another 128 MiB before that.
   - **Concurrency:** the pool runs up to 3 workers, so the Earth, Venus and Titan builds can overlap, for about 2 GiB.
   - **Fix:** add a 1-channel byte box downsample in CloudAtlas.cpp, `(a+b+c+d+2)>>2`, with a unit test. That costs about 11 MB extra. Run Unseam per joint on a float band rather than on the whole image.
3. **Cloud-normal tap spacing (NewPlanet.glsl:956).**
   - **Problem:** with z = 1 the four taps come from the atlas, but `da = d·vCldAtlas.zw/vCloudOff.zw` is still 2 *owner* texels. In atlas texels that is 2 for L7 owners, 4 for L6, 8 for L5 and 16 for hemispheres. The normal strength (x1²−x2²) scales with the spacing, so the shading still steps at every L6/L7 boundary, which is the line rev 4 removes from the alpha. CloudMicro and NormalmappedClouds both default to 1.
   - **Fix:** when `vCldAtlasW.z == 1`, use `da = 2.0 / vec2(textureSize(tCloudAtlas, 0))`. Otherwise keep the current formula.

## SHOULD
1. **Level choice and fallback (question 3).**
   - **Problem:** the worst case today is 127 wasted reads and decodes (about 0.6 s on one worker), then 32 more for L6 and 8 for L5. One missing L7 tile also costs the whole planet its L7 atlas, which brings the seams back everywhere.
   - **Fix, part 1:** check presence before decoding. For loose files use `fs::exists` on the resolved path. For the tree use `Idx()` plus `NodeSizeInflated() > 0` on the job's own ZTreeMgr. Then decode only the chosen level, and keep the decode-failure fallback.
   - **Fix, part 2:** better still, fill a missing tile from its ancestor, upsampled. That is exactly what the tile manager shows there (the owner is that ancestor), so z stays right.
2. **Streamed clouds.**
   - **Problem:** TileStream bakes L6..cloudMax on a background work item whenever any of those tiles is missing (TileStream.cpp:2012-2020). On a first run, the build at the first cloud render finds L7 incomplete, and the atlas stays at L5 or L6 for the whole session, with the L6/L7 seams.
   - **Fix:** while L < 7, re-check presence about every 30 s, or when the level directory changes, and rebuild once when the level is complete.
3. **The 8192 limit (question 2).**
   - **Problem:** Vulkan only guarantees 4096 for 2D images. The device limit is `caps.MaxTextureWidth` (D3D9Frame.cpp:274), and RingMgr.cpp:77 already clamps to it.
   - **Fix:** pass that limit to the job and cap L by it, so a 4096 device gets L6.
4. **Other bodies.**
   - **Measured:**
     - Venus alpha is 255 everywhere.
     - Titan alpha is 214 everywhere.
     - Jupiter's bands are in rgb: luminance std 28, and the rgb step at a tile edge is 2.44 against 1.19 inside.
   - **Problem:** an alpha-only atlas changes nothing on these bodies, yet costs 43, 43 and 11 MB and about 2 s each. Their seams are rgb seams, and they stay.
   - **Fix:** skip the atlas when the chosen level's alpha is constant (track min and max during the copy). State in the design that rgb seams (Jupiter, Venus) are out of scope.

## NIT
1. **The wording of claim 1.** The claim is true at mip 0 for owners at L. The CPU and GPU BC3 alpha decodes can differ by ±1/255, and the Unseam ramps are deliberate changes. Owners below L get finer data, so they look sharper, not the same. When minified, the tile's mips are BC3-re-encoded by VkConvertPixels (TileMipmaps 1, the default; the files have no stored mips), while the atlas mips are exact. Say this in the design, so the A/B check expects these differences.
2. **Pool order.** CLOUDATLAS is kind 0. An L7 build of 1–2 s on a 1-worker pool (machines with ≤ 7 cores) delays the cloud map and horizon jobs at start. Consider running it after CLOUDMAP.
3. **Runtime L in both z tests.** Store L in CloudAtlasState. CloudTile::Render and Surfmgr2 both compare against the constant `CloudAtlas::FILELVL` today.
4. **Surfmgr2 shadows.** With z = 1, the atlas alone could cast the shadow where the dual-texture match fails. Today "CloudShadows mapping failed" leaves no shadow there. This is optional.
5. **The L7/L8 owner boundary near the camera (Custom data).** On the atlas side that boundary is an L7 joint carrying the Unseam ramp, while the L8 side shows raw data. Look at that step in Alps High.
6. **The mean-alpha log** reads byte 3. With R8 it must read byte 0, at a stride of 1.
