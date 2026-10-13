# Design: seamless far cloud layer (cloud atlas), rev 2

## Rev 2: review fixes (design-review-cloud-seams.md); they override the sections below
- M1: tCloudAtlas goes on binding 24, shared with tGlare (only HorizonPS reads tGlare; no technique reads both). It is declared only under `#if defined(_CLOUDATLAS)`.
- M2, NIT 4: no textureQueryLod. The LOD comes from the uv derivatives with the anisotropy rule: `Pmax/min(Pmax/Pmin, aniso)`, in atlas texels (texUV·zw·(2048, 1024)). It sits under `#ifdef STAGE_PS`, with a stub returning 0 otherwise.
- M3: the job opens its own `ZTreeMgr::CreateFromFile(root, LAYER_CLOUD)` and deletes it after the 8 reads. The shared reader is never touched.
- M4: the job captures values only (root string, the loose and archive gates, a shared_ptr<AtlasBox>). The render thread takes the pixels from the box and uploads them.
  - The state lives in `struct CloudAtlasState *atlas` in TileManager2Base, with free functions in Cloudmgr2.cpp (CloudAtlasFree, CloudAtlasTex), as the horizon jobs do.
  - The job uses VkLoadPixels / VkLoadPixelsFromMemory + VkConvertPixels, not Tile code.
- S1: `_CLOUDATLAS` is added to the GiantCloud, Clouds and Earth flags only when CloudAtlas is on, so the SPIR-V is unchanged when it is off. On, the new code sits in `if (Prm.vCldAtlasW.x > 0.0)` (uniform), so no fetch happens before the atlas is ready.
- S2: bind with IPF_ANISOTROPIC | IPF_CLAMP_V (u repeats, v clamps).
- S3: AtlasRange(tile, rot) for surface tiles, using the shadow block's own `mgr->prm.rprm->cloudrot`.
- S4: the ramp is `smoothstep(-1.0, -0.25, lodA)`. An A/B crop of the far field in Alps High and an orbit limb view decides it; if it looks soft, move to (−0.75, 0.0).
- S5: the job uses the same gates as PreLoad: loose files only if DoLoadIndividualFiles(0), the archive only if ZTreeManager(0).
  - File level 5 = internal level 1: 2 rows × 4 cols, ilat 0 north, ilng 0 at −180°.
- NIT 1: "file level" means the folder number; internal = file − 4.
- NIT 3: static_assert(offsetof(ShaderParams, vCldAtlas) == 416 && sizeof(ShaderParams) == 448).
- NIT 5: whole-disc views (file level 4 hemisphere tiles) gain level-5 detail. This is noted, and it is the intended effect.
- NIT 7: IDC_GG_CLOUDATLAS 3044, plus a .rc line in the Ground graphics group, row 1 at x 420.
- NIT 8: GfxPool kind CLOUDATLAS goes last (3), NKIND 4.
- NIT 9: not done. A surface tile whose dual-texture match fails stays unshadowed, as today.

Repo: /home/racerx/Projects/Orbiter_linux_Earth/orbiter-2027-linux, branch cloud-seams from earth-streaming (10a4656). Then merge into Orbiter-linux-custom.

## 1. Problem (measured)
- Seen from orbit, the 2D cloud layer shows straight lines and a change of texture along cloud tile edges. Example: Tests/Gfx Clouds Orbit Alps, edges at 45°N and at cloud lng −135° (ground 9.8°E at MJD 51981.43, cloudrot 215.2°).
- The fault is present with all five ground-graphics switches off and with EnableCloudShadows off. It goes away with EnableClouds off. So it is in the legacy layer (CloudPS), not in the new code.
- Ruled out with debug shaders and file checks:
  - Tile UVs are continuous across the edges (UV debug shot).
  - Every tile shows its own file (correlation 0.66–0.95, best at mip 0).
  - Edge rows in the files are continuous (edge step 8.5 vs 7.0 inside).
  - Mips are proper box averages (corr ≥ 0.996).
  - vCloudOff is right, the mesh does not bake the range, and microrange is continuous.
- Cause: each tile has its own mip chain, and sampling uses IPF_CLAMP. At mip k, the last texel on each side of an edge averages a band 2^k texels wide, and bilinear filtering clamps instead of blending into the neighbour.
  - Measured in the files: at mip 0 the edge rows differ by about 3/255; at mip 2 the edge texels differ by about 11/255.
  - Rendered: a 6/255 step in the row mean at the edge (y 571→572), a 1-pixel line with a band on each side.
  - From orbit an L7 tile is drawn at mip 2–3, where one texel is 20–40 km wide.
- The ground's cloud shadows sample the same tile textures with the same clamp (Surfmgr2.cpp:983-1049, NewPlanet.glsl:484-488 and 699-704), so they seam at distance the same way.

## 2. Fix
One seamless global texture per cloud layer, the atlas. Pixels where a tile texture would be minified use the atlas instead. Near the camera nothing changes.

### 2.1 Atlas (new CloudAtlas.h/.cpp: pure maths, unit tested, plus the build)
- Source: the 8 level-5 cloud tiles (2 rows × 4 columns of 512 px, 90° each). They are assembled into one 2048×1024 B8G8R8A8 image in the cloud frame:
  - row 0 is north;
  - column 0 starts at lng −180°, as the tile grid (Extents) and the tile files.
- Read order per tile:
  - loose `Cloud\05\%06d\%06d.dds` first, through the same path rule as CloudTile::PreLoad;
  - then `ZTreeManager(0)->ReadData(5, r, c)`;
  - any tile missing means no atlas, so legacy behaviour.
- Mips: VkConvertPixels with Mips 0 (box filter). The dimensions are powers of two, so no mip crosses the ±180° seam incorrectly.
- Memory: 8 MB, about 10.7 MB with mips, per planet with a CloudMgr2.
- Build:
  - On GfxPool (new kind CLOUDATLAS, run before CLOUDMAP), once, when TileManager2<CloudTile> is first rendered and the switch is on.
  - The decode works on its own copies, so no tile state is touched. A ZTree read takes the loader mutex for that read only.
  - Upload on the render thread at the next Render (a VkTex with full mips).
  - Failure is logged once; the atlas stays off.
- Freed with the manager. The pool job is cancelled by Unregister.

### 2.2 Per-tile atlas mapping (pure maths)
- `FVECTOR4 AtlasRange(const Tile&)` gives the offset and scale so that `uvA = texUV * zw + xy`.
- Cloud tiles, level ≥ 1 (quadpatch, tu 0..1 west→east, tv 0 north): xy = ((bnd.minlng + π)/2π, (π/2 − bnd.maxlat)/π), zw = ((maxlng − minlng)/2π, (maxlat − minlat)/π).
- Cloud level 0 (hemisphere): tu runs from du to 1−du with du = 0.5/512, tv = colatitude/π.
  - zw.x = 0.5/(1 − 2du), xy.x = 0.5·ilng − du·zw.x;
  - zw.y = 1, xy.y = 0.
- Surface tiles (cloud shadows): as level ≥ 1, with lng in the cloud frame, minlng' = wrap(bnd.minlng + cloudrot). The atlas wraps in u, so a tile across ±180° is continuous. Surface level 0 uses the hemisphere rule above, with the same rotation added to xy.x.
- Tests:
  - an atlas texel centre maps back to the tile texel it came from (levels 1, 3, 5, 9, both hemispheres, poles);
  - adjacent tiles give equal uvA on their shared edge;
  - the surface mapping with cloudrot 0, 215.2° and 359.9° matches the cloud-frame point.

### 2.3 Shaders (NewPlanet.glsl)
- New sampler `tCloudAtlas`, binding 32 (free; bindings 13–23 and 31 are in use).
  - Sampler state: linear + anisotropic, wrap in u and clamp in v, if IPF supports per axis. If it does not, use wrap: the pole row bleed is under half a texel (0.09°).
- Prm gains, appended after vVolCld (C++ ShaderParams and GLSL PerObjectParams, same order):
  - `vec4 vCldAtlas` (uvA offset xy, scale zw);
  - `vec4 vCldAtlasW` (x: 1 when the atlas is on for this draw, else 0).
- Shared helper:
  ```
  float AtlasW(vec2 uvA) { float l = textureQueryLod(tCloudAtlas, uvA).y; return Prm.vCldAtlasW.x * smoothstep(-1.25, -0.5, l); }
  ```
  - The atlas is used where it is magnified less than about 1.4×. That is where a level-7 tile would be at mip ≥ 0.75; tiles at level ≤ 5 always get atlas weight 1 where minified.
- CloudPS:
  - `cTex = mix(texture(tDiff, vUVTex), texture(tCloudAtlas, uvA), w)`;
  - in the _CLOUDNORMALS block, the four ±d taps become mixes of the tile tap and an atlas tap at ±d·(zw/vCloudOff.zw);
  - micro texture, lighting and fVol are unchanged.
- GiantCloudPS: the same mix for cTex.
- Surface PS:
  - `if (Flow.bCloudShd)`: compute the tile value as today (fChA/fChB/select), then `fShd = mix(tileShd, texture(tCloudAtlas, uvA).a, w)` before the existing `1 − fShd·fAlpha`;
  - uvA comes from texUV and the surface tile's vCldAtlas.
- With vCldAtlasW.x = 0, every changed expression reduces to the old one exactly: mix(a, b, 0) = a in IEEE. So switch-off is pixel-identical.

### 2.4 C++
- `TileManager2<CloudTile>` gets the atlas: `VkTex *atlas`, a state (none, building, ready, failed), a GfxPool owner id, and `AtlasTex()`.
- CloudTile::Render sets vCldAtlas from AtlasRange(*this), sets vCldAtlasW.x = atlas ready ? 1 : 0, and binds tCloudAtlas (the default texture when not ready).
- SurfTile::Render, in the cloud shadow block: the same, through `vPlanet->CloudMgr2()->AtlasTex()` and the rotated range.
- The PlanetShader binding goes through the existing SetTexture by name ("tCloudAtlas").
- Config: `CloudAtlas` in VulkanClient.cfg, default 1. 0 means no build and w 0 everywhere: legacy.
  - Read and written in D3D9Config.
  - A checkbox in the Ground graphics group (VideoTab, resource.h IDC_GG_CLOUDATLAS), following the existing switches.

## 3. Not changed
- Tile LOD, tile loading, the volume clouds and their coverage map (tile densities), the micro texture, the cloud lighting.
- Near-camera look: where the atlas is magnified more than 1.78×, w = 0 and the output is bit-identical.

## 4. Checks
- Alps orbit view: row-mean step at the 45°N edge under 1.5/255 (now 6); no visible line at 9.8°E; image otherwise close to the current one (mean abs < 3/255 away from the edges).
- Europe and Africa orbit, Alps High 9.5 km, Carpathians Under: visually unchanged; pixel diff against the current build shows no change near the camera.
- CloudAtlas = 0: pixel-identical to the current build in all of the shots above.
- Cloud shadows from 150–400 km: no shadow seam at tile edges.
- Unit tests (2.2). ctest. No validation-layer errors (vvl run on the orbit scenario).
- Docs: VulkanClient.html Ground graphics (one line), NEW.tex (one line).

## Rev 4: what the orbit seams really are (measured 2026-10-13); overrides 2.1-2.3 where they differ
- Measured in the Earth tree (streamed clouds, data to file level 7): with rev 3 + the code-review fix (edge band only where the tile is minified), atlas on and off are identical (row step 571→572 stays 3.5). At 490 km the tiles are magnified, so no band applies.
- The seams there are LOD boundaries: a rendered L6 tile (blurrier owner) next to an L7 tile, plus the half-texel clamp at each joint. Each tile has its own texture, so the texture resolution jumps at the geometry edge.
- Custom (Cloud.tree, data to L9): at the same view every tile is file level 9 with its own texture. The only visible seam is a data step inside the pack (a short diagonal), not a clamp or LOD line.
- Fix: the atlas becomes the cloud alpha itself for every tile whose texture owner is at or below the atlas level. One texture means per-pixel mip choice, so there is no LOD or clamp seam at all.
  - Atlas level L = 7 if all 8×16 tiles load, else 6, else 5 (first complete level). R8 alpha only: 512·2^(L−3) × 512·2^(L−4); L7 is 8192×4096, 32 MB, 43 MB with mips.
  - Unseam works on n channels (1 here) and ramps every joint of the chosen level, so pack data steps become 8-texel ramps.
  - vCldAtlasW.z = 1 when the owner's file level ≤ L (CloudTile tl walk; Surfmgr2 pCloudTile->Level()). Then the atlas weight is 1, near or far, including the normal taps.
  - Finer owners (Custom L8/L9 near the camera): as rev 3 with the review fix. AtlasW where the atlas is not magnified, the edge band only where the tile is minified. Legacy look near the camera.
  - Shaders: the atlas is sampled `.r`. Only cTex.a is mixed; rgb stays the tile's. The atlas size for AtlasW comes from textureSize(tCloudAtlas, 0). GiantCloudPS is the same (alpha only).
  - Cost: one more fetch per cloud pixel (it was already there when x > 0). The build reads up to 128 tiles once on the pool.
- Checks: Earth tree Alps orbit, row step at 571→572 under 1 with the atlas on, and no visible L6/L7 line. Custom Alps orbit and Alps High: no change beyond run noise (L9 owners). ctest; CloudAtlas tests for the level sizes and a 1-channel Unseam.
