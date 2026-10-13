# Design review: cloud seams (cloud atlas)
I checked design-cloud-seams.md against earth-streaming 10a4656: Cloudmgr2, Tilemgr2(.h/_imp.hpp), Surfmgr2, VPlanet.h, VPlanetAtmo, NewPlanet.glsl, VkShader, VkCore, D3D9Util, D3D9Catalog, GfxPool, ZTreeMgr, D3D9Client.rc and Earth.cfg, plus test compiles with glslangValidator 16.2.0.

## MUST
1. **Binding 32 does not exist.**
VkDev::MAXBINDINGS is 32, so the valid bindings are 0–31 (VkCore.h:303). The set layout has 32 entries (VkCore.cpp:763-773). Push skips any binding ≥ 32 (VkShader.cpp:318), so the descriptor is never written. ShaderClass::SetTexture writes pTextures[binding−4] with no bounds check, and the array has 28 entries (D3D9Util.cpp:1939, D3D9Util.h:404). Binding 32 is an out-of-bounds write.
Bindings 24–30 are also in use, not free (NewPlanet.glsl:141-147). tHorizon at 31 is marked as the last free binding. maxPushDescriptors is 32 on common drivers, so raising MAXBINDINGS is not portable.
Fix: put tCloudAtlas on binding 24, shared with tGlare. Only HorizonPS reads tGlare (NewPlanet.glsl:293; it is set on pDome, HazeMgr.cpp:333). No technique reads both. glslang 16.2 and spirv-val accept two sampler2D on one binding (tested). Do not hide tGlare behind a PS_ guard: HorizonPS is compiled into every technique and would no longer compile.

2. **textureQueryLod breaks every NewPlanet vertex compile.**
VkCompileGLSL compiles the whole file once per stage, with #version 460 and STAGE_VS or STAGE_PS (VkShader.cpp:166-169). CloudPS, TerrainPS and the new helper have no guard, so the VS stage parses them too. textureQueryLod only exists in fragment shaders. glslangValidator on a vertex stage with the helper defined but unused fails: "'textureQueryLod' : no matching overloaded function found". All PlanetShaders and the Dome/Ring shaders would fail to build.
Fix: `#ifdef STAGE_PS` with the real AtlasW, `#else` a stub that returns 0.0, as NewMesh.glsl:198 does.

3. **ZTree reads race with the tile loader.**
ZTreeMgr::ReadData does fseeko and fread on one shared FILE* with no lock (ZTreeMgr.cpp:201-208). The loader thread calls PreLoad, and so ReadData, after it releases the loader mutex (Tilemgr2.cpp:1046-1051). Taking the loader mutex in the pool job therefore does not serialize with it. Either thread can get wrong bytes.
Fix: in the job, open a private reader with ZTreeMgr::CreateFromFile(root, LAYER_CLOUD) and delete it after the 8 reads. Or add a mutex inside ReadData.

4. **Unregister does not stop a running job.**
GfxPool::Unregister removes only queued jobs (GfxPool.cpp:71-80). ~TileManager2 deletes treeMgr (Tilemgr2_imp.hpp:363-366). A job that holds the manager, a tile or the ZTreeMgr* can outlive them.
Fix: capture only values (the data root string, the tileLoadFlags bits) and a shared_ptr<Box>, as HorizonSubmit does (Surfmgr2.cpp:1758-1772). The render thread takes the result from the box and uploads it. Do not call Tile::LoadTextureFile in the job. Use VkLoadPixels or VkLoadPixelsFromMemory with VkConvertPixels.

## SHOULD
1. **Make CloudAtlas=0 identical by construction.**
mix(a,b,0)==a holds only when b and w are finite. w = 0·smoothstep(lod) is NaN if the lod is NaN. The driver's codegen can also change around new code, because SPIR-V allows FMA contraction.
Fix: add `_CLOUDATLAS` to the Clouds, GiantCloud and Earth flags only when Config->CloudAtlas is set (VPlanetAtmo.cpp:127-133, 158-168). With the switch off, the SPIR-V is byte-identical to today's.
With the switch on, wrap the new code in `if (Prm.vCldAtlasW.x > 0.0)`. The condition is uniform, so derivatives stay defined. This also skips the query and the extra fetches while the atlas is not ready: 1 per cloud pixel, 5 with _CLOUDNORMALS.

2. **Use per-axis addressing and drop the wrap fallback.**
IPF_CLAMP_V exists (gcCore.h:82). UpdateTextures maps U and V separately (D3D9Util.cpp:1775-1781). Bind with IPF_ANISOTROPIC | IPF_CLAMP_V, so u repeats and v clamps.

3. **Take the rotation from the same source as the tile shadow.**
The tile shadow uses mgr->prm.rprm->cloudrot (Surfmgr2.cpp:991-993). Coverage uses GetPlanet()->prm.cloudrot (Cloudmgr2.cpp:278).
Fix: change the signature to AtlasRange(tile, rot) and pass the rot of the shadow block. Tile and atlas shadows then line up in the blend band.

4. **Measure the softening band.**
For a file-level-7 tile, the atlas LOD is the tile LOD minus 2. w goes from 0 to 1 between tile mip 0.75 and 1.5. In that range the atlas is magnified 2.4× to 1.4×, so it is softer than the tile it replaces. Atlas and tile carry the same detail only from tile mip 2 up.
Fix: add an A/B crop of this band, for example the far field in Alps High and an orbit view near the limb. If the softening shows, start the ramp later, for example at (−0.75, 0.0).

5. **Mirror PreLoad's source rules.**
PreLoad reads loose files only if DoLoadIndividualFiles(0) is true. It reads the archive only if ZTreeManager(0) exists (Cloudmgr2.cpp:76-87, tileLoadFlags 0x1 and 0x2). The atlas build must use the same two gates.
State in 2.1 how the file level maps to the tile grid. File level 5 is internal lvl 1, because the path uses lvl+4 (Cloudmgr2.cpp:77). That gives 2 rows × 4 columns. ilat 0 is north, and ilng 0 starts at −180° (Tilemgr2.cpp:423).

## NIT
1. **Level names.** The design mixes file and internal levels. "Level ≥ 1, quadpatch" is internal. "Level-5" and "L7" are file levels. Earth.cfg has MaxCloudResolution 9, so the deepest internal cloud level is 5, and the test list "levels 1, 3, 5, 9" needs a unit. Say "file level" throughout, with internal = file − 4.

2. **Band numbers in §3.** smoothstep(−1.25, −0.5) gives w = 0 only above 2^1.25 = 2.38× magnification, not 1.78×. The weight is 1 below 1.41×.

3. **Layout guard.** vVolCld ends at offset 416. vCldAtlas sits at 416, vCldAtlasW at 432, and the size becomes 448. All of these are 16-aligned under pack(4) with alignas(16) FVECTOR4, and they match the scalar GLSL layout. FlowVS moves from 416 to 448 in PlanetVSBlock (NewPlanet.glsl:122). Reflection picks that up. HazeMgr copies sizeof(ShaderParams), and SetValue clamps the copy to the reflected size.
Add static_assert(offsetof(ShaderParams, vCldAtlas) == 416 && sizeof(ShaderParams) == 448). vVolCld.w is unused (Cloudmgr2.cpp:159) and could hold the flag instead of a second vec4.

4. **LOD from texUV.** On deep surface tiles, uvA changes by only a few float ulps per pixel (zw ≈ 2^-11 at internal level 10). textureQueryLod then works from quantized derivatives. With an anisotropic sampler, its result also depends on the driver.
Option: compute l = log2(max(length(dFdx(texUV)·zw·vec2(2048,1024)), length(dFdy(texUV)·zw·vec2(2048,1024)))), also under STAGE_PS.

5. **Hemisphere draws change.** File-level-4 tiles hold 1024 px per 360°. They get w = 1 even when magnified up to 2.8×, so the whole-disc view gains level-5 detail. Note this in §3 and in the shots.

6. **Keep the atlas off the template.** Members added to TileManager2<T> also land in the SurfTile manager (Tilemgr2.h:456-537). Follow the horizon pattern instead: a forward-declared pointer in TileManager2Base (Tilemgr2.h:427) and a free function that releases it. CloudMgr2() is const at the shadow site (Surfmgr2.cpp:989), so AtlasTex() must be const.

7. **Dialog resource.** The checkbox also needs a line in D3D9Client.rc next to the other GG boxes (D3D9Client.rc:269-271). The next free ID is 3044 (resource.h:186).

8. **Pool order.** Inserting CLOUDATLAS at 0 shifts the enum. Nothing depends on the numeric values. Since this is a one-shot job, it can also go last, so it never delays a CLOUDMAP job.

9. **Shadow when the dual-texture match fails.** If the second cloud tile is missing, has_shadows stays false and the surface tile gets no shadow (Surfmgr2.cpp:1018-1043). Once the atlas is ready, pixels with w = 1 could still be shadowed. This is optional.

10. **Holds as written.**
- Cause: tDiff and tCloud are sampled with IPF_CLAMP | IPF_ANISOTROPIC (Cloudmgr2.cpp:150, Surfmgr2.cpp:1049-1050). Pool tiles have 6 box-filtered levels at TileMipmaps 1 (D3D9Catalog.h:301, Tilemgr2.cpp CreateTexture). With TileMipmaps 0 or 2 they get 1 level and alias instead. The atlas helps there too.
- Quadpatch: tu = j/grd runs west to east, and tv = 0 is at maxlat (Tilemgr2.cpp:526-527). Extents and WorldMatrix put lng 0 of the tile grid at −180° (Tilemgr2.cpp:423, 1238). The design's xy/zw are right.
- Hemisphere: tu runs from du to 1−du, tv = colatitude/π, and ilng adds π (Tilemgr2.cpp:692-711). zw.x = 0.5/(1−2du) and xy.x = 0.5·ilng − du·zw.x are right.
- Surface frame: cloud lng = wrap(rot + lng) (Surfmgr2.cpp:993). The prerot matrix gives the same sign (Tilemgr2.cpp:1199). A repeating u keeps tiles across ±180° continuous.
- Level-5 read: ReadData(5, r, c) goes to Idx and then to child (r&1)·2 + (c&1) of rootPos4[c/2]. This matches PreLoad (Cloudmgr2.cpp:77, 82).
- Unbound samplers get the 1×1 black default with a repeat sampler (VkShader.cpp:318-324). w = 0 keeps it out of the image. New ShaderParams fields start at zero (the FVECTOR4 constructor).
- VolTiles reads only tile densities. Eclipse and the micro texture are untouched. Env and custom-camera passes go through the same CloudTile::Render.
