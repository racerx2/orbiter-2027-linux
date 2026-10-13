# UE5 batch: integration decisions (integrator, binding on all owners; overrides the five designs where they differ)

## Owners and features
- shadows: SceneShadows (design-scene-shadows.md + design-review-scene-shadows.md)
- ssr: SSR (design-ssr.md + review)
- plumes: VolPlumes (design-volumetric-plumes.md + review)
- ssgi: SSGI (design-ssgi.md + review)
- fog: HeightFog (design-height-fog.md + review)
- night: NightLook (night city look: building windows, ground night lights, exposure at night; brief in your prompt)
- integrator: everything listed under "Integrator prep" below, already done in the commit you start from.

## INT-1 Pass ids
- RENDERPASS_SCENESHADOW = 0x000A, RENDERPASS_FLOWMAP = 0x000B (Scene.h). The pass label arrays in D3D9Client.cpp are grown to 12 by the integrator.

## INT-2 Uniform blocks: no new UBO bindings
- No feature adds a UBO at binding 3 (or anywhere). Parameters go into the EXISTING per-shader block, appended at its end under the feature macro, in this fixed order: gCsm (`#if defined(_CSM)`), then gSsr (`#if defined(_SSR)`).
  - NewPlanet.glsl: append to PlanetPSBlock (binding 2). D3D9Client.fx family: append to FxParams. PbrMesh.fx: append to PbrShared. Mirror the order in C++.
- Each owner checks with a reflection dump (glslangValidator) that the block layout with both macros on matches the C++ struct.

## INT-3 Samplers
- D3D9Client.fx: CsmS = 28, SSR samplers from 29. PbrMesh.fx: CsmS = 29, SSR 30-31. Tree.glsl: shadows' sampler after its last one.
- NewPlanet.glsl: _CSM reuses binding 25 (tShadowMap) for the atlas. _SSR uses 26 and 27, which are free only without _DEVTOOLS: VPlanetAtmo adds _SSR to the Earth flags only when !Config->EnableMeshDbg.

## INT-4 Depth
- One predicate, used by the depth recording in Surfmgr2.cpp and by SceneFx: SceneFx::DepthWanted() = SSAO || VolClouds || TAA || SSGI || SSR || HeightFog || VolPlumes (Config, read once). The depth pass is always full range (the plumes' short-range cut is dropped).
- The SSGI depth-pass MRT (albedo, normal + class) is owned by ssgi. ssr may read the normal target via a SceneFx accessor that ssgi adds. Extra targets are bound only in DepthPass and unbound before it returns.

## INT-5 Order
- SceneFx::AfterOpaque: DepthPass, FlagPass, Ssao, VolShadow, Gi (ssgi), then FogAndClouds (fog), which decides fog-under or fog-over-clouds by camera altitude and calls Clouds.
- Scene::RenderMainScene after AfterOpaque: PostTaa::Resolve, SceneSsr::Capture (ssr), jitter off, late labels, PostLook::Haze, PostPlume::Render (plumes).
- No double fog: ssr either strips fog from hit radiance or captures before the fog. ssr decides and documents it. The fog pass exposes a half-res fog distance/alpha target plus a CPU FogAlongRay() for plumes.

## INT-6 Labels
- bLabelsLate = TaaActive() || Config->SSR || Config->SSGI || Config->HeightFog. PostLook's sharpen uses TaaActive() (the integrator fixed it).

## INT-7 Config and dialog (integrator done)
- Keys and defaults:
  - SceneShadows 1, SSR 1, VolPlumes 1, SSGI 1, SSGIStrength 1.0, HeightFog 0, NightLook 1.
  - IDs IDC_FX_SCENESHD 3054, IDC_FX_SSR 3055, IDC_FX_PLUMES 3056, IDC_FX_SSGI 3057, IDC_FX_FOG 3058, IDC_FX_NIGHT 3059.
- Effects group: a third row at y=440.

## INT-8 File ownership (edit only your files; shared files with edit_block on small exact regions only, re-read right before each edit, never rewrite a shared file whole)
- shadows (own): SceneShadows.h/.cpp, CsmMath.h/.cpp, shaders/SceneShadow.glsl, Tests/CsmMath.Test.cpp, BldgMgr.h/.cpp, VBase.h/.cpp.
  - Shared: NewPlanet.glsl TerrainPS (_CSM), D3D9Client.fx (+ Vessel.fx, PBR.fx, Metalness.fx, Mesh.fx under _CSM), PbrMesh.fx (_CSM), Tree.glsl (_CSM), TreeMgr.h/.cpp, Surfmgr2.cpp (caster walk), Tilemgr2.h, VPlanet.h/.cpp, VVessel.cpp (stencil and caster parts), PbrRender.h/.cpp, D3D9Effect.h/.cpp (_CSM).
- ssr (own): SceneSsr.h/.cpp, SsrMath.h/.cpp, shaders/Ssr.glsl, SsrPrm.glsl, HiZ.glsl, Tests/SsrMath.Test.cpp.
  - Shared: NewPlanet.glsl (_SSR), D3D9Client.fx family (_SSR), PbrMesh.fx (_SSR), VVessel.cpp and VBase.cpp (binding parts), PbrRender, VPlanet.h, VPlanetAtmo.cpp (_SSR flag), D3D9Effect (_SSR).
- ssgi (own): SceneFxGi.cpp, SsgiMath.h/.cpp, shaders/SSGI.glsl, Tests/SsgiMath.Test.cpp, shaders/SceneDepth.glsl, NewMesh.glsl SceneDepth_PS, Mesh.h/.cpp (RenderShadowMap opt 2 parts).
  - Shared: Tree.glsl distance pass, TreeMgr.cpp:110 flags, Surfmgr2.cpp RenderDepth, SceneFx.cpp DepthPass/FlagPass/InitDepth (MRT parts only).
- fog (own): SceneFxFog.cpp, FogMath.h/.cpp, shaders/HeightFog.glsl, Tests/FogMath.Test.cpp.
  - Shared: Surfmgr2.cpp ElevGrids, Tilemgr2.h declaration.
- plumes (own): PostPlume.h/.cpp, PlumeMath.h/.cpp, shaders/Plume.glsl, Tests/PlumeMath.Test.cpp, Config/GC/Plume/*.cfg, Particle.h/.cpp, PostLook.h/.cpp (haze reads your segments).
  - Shared: VVessel.h/.cpp (exhaust, re-entry, flow map), VObject.h.
- night (own): AutoExpo.h/.cpp and Tests/AutoExpo.Test.cpp; the building shader and BldgMgr night/window parts (coordinate: shadows also edits BldgMgr, so use edit_block regions); NewPlanet.glsl _NIGHTLIGHTS block only.
- integrator: Scene.h/.cpp, SceneFx.h (declarations), SceneFx.cpp AfterOpaque order and DepthWanted, D3D9Config, resource.h, D3D9Client.rc, VideoTab.cpp, D3D9Client.cpp labels, GfxPool.h (FOGMAP), CMake lists, docs.
  - Need a change in an integrator file? Write it in your report or make the smallest edit_block change and list it.
