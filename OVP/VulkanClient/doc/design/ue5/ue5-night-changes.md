# UE5 batch: night look (NightLook) changes

Owner: night. Design: design-night-look.md with "## Rev 2 (implementation)" (one bullet per review MUST/SHOULD/NIT). Branch ue5-batch, uncommitted.

## Root cause (night shots cNn-85 AE=1, cNx-85 AE=0, NYC, sun -33 deg)
- Windows "uniformly white": the facade emission map was right, but (a) auto-exposure climbed to about +2.3 EV with no brake, because the saturation share was counted on 8x8 box averages at >= 254/255 and never fired; (b) the map's box mips turn the dense lit windows into a uniform wall glow at 2-6 km; (c) all windows of a cell switched together (Emission2 per cell).
- Ground "flat grey-white blanket": at 700 m it is mostly the orbital term cMsk·CamSpace·4·fNgt (0.17 flat, about 50/255 through HDR at e = 1, 168/255 at e = 4.9) plus the near term, which lights the day albedo with a saturated, structureless light map (median 248/255 over NYC).

## Files and what each change does
- OVP/VulkanClient/AutoExpo.h/.cpp (own)
  - Params night, nightDropEV 3, nightMaxEV 1, brake0/brake1 0.005/0.03, satThrNight 0.9. Controller SetNight(n, internal), Night(), SatThreshold().
  - n <= 0 (day, or key off): the legacy Target code path unchanged (unit-tested equal to the bit).
  - Night: only the band's low edge is lowered (lo·2^(-3n)); hi, satLimit 0.15 and the downward rule stay legacy (M3). Any rise is braked by the near-saturated share: x(1 - n·smoothstep(0.005, 0.03, sat)) (NIT 9). Upper clamp mixes to +1 EV at full night.
  - Cockpit (internal view): the band drop and clamp are off, the brake and threshold stay (S2).
  - NightWeight(sinElev, cosDip) / NightWeightAt: sun over the camera's own horizon (elevation + dip), 0 at +3 deg, 1 at -9 deg (M3, a camera in orbit in sunlight has n = 0).
  - Namespace NightLook: CellDusk, WindowOn (M6 clamp), CityLevel, SegShare, Warp, LampMeans, LampField (CPU copy of the shader's lamps, three filter levels, M4), ClsHash/ClsNoise CPU copies, NiteHalf (box, on-time weighted by light) and NiteNest (each texel one of its 4 children, S1).
- Tests/AutoExpo.Test.cpp (own): 11 new cases: legacy equality at n = 0, night weight, night city, dark countryside, bright content legacy, brake, threshold, cockpit, CellDusk/WindowOn (incl. negative lod), lamp energy at every filter level (fixed 1e6/4e5 samples) and periodicity, nested mips (each level >= 4 texel equals a child, expected value kept at night and dusk). Tests/CMakeLists.txt: -O2 on AutoExpo.Test (NIT 10).
- OVP/VulkanClient/shaders/Exposure.glsl (own entries): PSHalfSat (W/2 from the full-res frame, alpha the share of its 4 pixels with max >= thr) and PSBlocksSat (sums alpha), only under `PS_PSHalfSat`/`PS_PSBlocksSat`; PSBlocks/PSTotal SPIR-V identical (M2).
- OVP/VulkanClient/SceneFx.cpp (integrator file, small edit_blocks): InitExposure (tHalf get RTUsage and the two entries compile only with NightLook; p.night), BeginFrame (SetNight from the camera proxy, internal flag), Measure (at night: PSHalfSat on the full-res target, or on a single-sample copy when the target is MSAA or not sampleable; then the legacy blits; PSBlocksSat; by day the legacy chain unchanged), free of the copy. SceneFx.h: `tFull/sFull` (one line).
- OVP/VulkanClient/shaders/NewPlanet.glsl: `_NIGHTLOOK` helper NightLamps/NightWarp/NightPal before TerrainPS; in the `_NIGHTLIGHTS` block: contrast level I^2.2, NaN-safe hue `cMsk/max(I,1e-3)` (M5), land class dimming, lamps per street family with per-direction footprints fwidth(qw) (points, line band, area mean; M4), lamp glare through GetLandView's atn computed in uniform flow (NIT 7), orbital term weighted by smoothstep(0.35, 0.8, CamSpace) (M1). Legacy lines under `#else`.
- OVP/VulkanClient/shaders/PBR.fx: `_NIGHTLOOK` window branch at the emission line: tagged building cells (SpecialFX.w 0.75) add the night map's colour where D passes the window's on-time, step width clamp(0.03 + 0.03·max(lod,0), 0.03, 0.15) (M6); else the legacy max line.
- OVP/VulkanClient/BldgMgr.cpp/.h: with NightLook the facade's emission map is replaced by facade_nite.dds (missing file: legacy windows); Night() writes the cell's dusk progress D = CellDusk(el, hash) into SpecialFX (0, D, 1, 0.75), Emission2 stays 0. Member `nite`.
- OVP/VulkanClient/VPlanetAtmo.cpp, D3D9Effect.cpp: one macro line each (`_NIGHTLOOK` only with the key, and only with `_NIGHTLIGHTS` for the planet).
- Utils/bldgtex/bldgtex.cpp, CMakeLists.txt: the night map facade_nite.dds (2048 BC3, rgb window colour from blackbody white points, a the on-time 0.05-0.85, never-lit 0; levels 1-3 box, >= 4 nested points). facade.dds, facade_emis.dds, facade_norm.dds, roof*.dds md5 unchanged.

## Tests
- Build: /tmp/earth/vfx/build.sh, BUILD EXIT=0 (b1.log 16:58 and b2.log 17:02, after the full-res measure change). No warnings in my files.
- Unit: Tests/AutoExpo.Test: "All tests passed (21650 assertions in 19 test cases)" (8 legacy + 11 night cases).
- Offline SPIR-V and text identity, key off (scripts /tmp/earth/ngt/spv.sh, expo_spv.sh, /tmp/earth/night/pbr_chk.sh):
  - TerrainPS with the Earth flags `_WATER _RIPPLES _CLOUDSHD _CLOUDATLAS _WATERFX _VOLSHD _NIGHTLIGHTS`: SPIR-V identical to the file with my blocks cut. With `_NIGHTLOOK` (and with `_NIGHTLIGHTS _NIGHTLOOK _FILMIC`): compiles.
  - Exposure.glsl: PSBlocks and PSTotal SPIR-V identical to the pre-change file; PSHalfSat and PSBlocksSat compile.
  - PBR.fx without `_NIGHTLOOK`: preprocessed text identical to the file with my block cut (blank lines aside); with it the block is present.
- Legacy textures: facade.dds, facade_emis.dds, facade_norm.dds, roof.dds, roof_norm.dds md5 equal to /tmp/earth/ngt/tex_md5_before.txt after bldgtex re-ran.
- Display runs (queued 16:58 in /tmp/earth/night/runs.sh): ngtA1 "Tests/Gfx Shot New York Night" NightLook=1 AutoExposure=1, ngtA0 the same with NightLook=0, ngtD1 "Tests/Gfx Shot New York Dusk" NightLook=1; shots at 10, 20 and 28 s.
  - NOT RUN by 17:22. From 16:57 to 17:14 the queue was blocked: cfgrun.sh busy() waited for a Custom-tree Orbiter (PID 935629, DISPLAY=:0, started from the user's desktop; I did not touch it). Reported to main at 17:03. At about 17:18 the machine rebooted. /tmp was wiped: /tmp/earth (build.sh, run.sh, cfgrun.sh, the display script, shots, logs, my check scripts) is gone, and the queued runs with it. Reported to main at 17:19. The source edits are on disk, and VulkanClient.so relinked at 17:13:59. When the build and run scripts are back, I will rebuild, re-run the three checks and add the results, shots and Pictures copies under "## Review fixes".

## Open items
- The display checks above (shots, "[ERROR]" scan, numbers, night-before.png / night-after.png).
- Not run in this phase (3-run limit): 30 km continuity (Gfx Night NYC 30km), orbit terminator EV (Gfx Shot Orbit Terminator Night), VC night (Gfx Night KSC VC), TAA pan (Gfx Night NYC Pan); scenarios are in the build tree, the maths is unit-tested.
- Constants NL_KAMB 0.06, NL_KPOOL 2.0, NL_KSPILL 0.24, NL_KHEAD 4.0 (with lightfac 0.5 in fNgt) are not re-tuned against shots in this phase.
- Cost not measured (estimate: TerrainPS night <= 0.2 ms city full screen, 0 by day; Measure at night +1 full-res fetch pass, about 0.05 ms, plus a full-res copy only if the target is MSAA or unsampleable).

## For the integrator (shared files I edited by edit_block)
- SceneFx.cpp: InitExposure (usage, two entries, p.night), BeginFrame (SetNight), Measure (night branch), free of tFull/sFull. SceneFx.h: one line (tFull, sFull).
- NewPlanet.glsl: the `_NIGHTLOOK` helper before TerrainPS (outside the `_NIGHTLIGHTS` block) and the `_NIGHTLIGHTS` block. PBR.fx emission line. BldgMgr.cpp ctor and Night(), BldgMgr.h one member. VPlanetAtmo.cpp and D3D9Effect.cpp one line each. Tests/CMakeLists.txt -O2 on AutoExpo.Test. Utils/bldgtex.
- Docs line (design §10): VulkanClient.html Effects list and README key list.


## Review fixes
Review: code-review-ue5-night.md (MUST 0, SHOULD 3, NIT 4).
- SHOULD 1, display checks: NOT DONE. The 17:18 reboot wiped /tmp/earth, and the shared build.sh, run.sh, cfgrun.sh and the Xvfb display had not been restored by the last check. Left to do, in order: shared build, then ngtA1 (NYC Night, NightLook=1 AutoExposure=1), the day pair "Gfx Shot New York" NightLook 1/0, ngtA0 and ngtD1, then ngtM/ngtT/ngtV. After that the numbers, the "[ERROR]" scan, and the copies night-before.png / night-after.png into /home/racerx/Pictures/orbiter-vfx/.
- SHOULD 2, ALU by day (fixed, NewPlanet.glsl `_NIGHTLOOK` block):
  - The footprint is now fwidth of the unwarped q, and dFdx/dFdy of vUVSrf are taken in uniform flow. NightWarp, the tClass fetch (textureGrad) and NightLamps run only under `fNgt > 0`.
  - The second GetLandView is gone: the lamp glare cNGl is added as cNGl·sct.atn right after cNgt2, behind `#if defined(_NIGHTLIGHTS) && defined(_NIGHTLOOK)`.
  - By day only fwidth/dFdx and a few multiplies by 0 remain.
- SHOULD 3, cockpit brake (fixed, AutoExpo.cpp Target): with band = 0 (internal view) the brake window is 3x wider, 1.5-9 %. The VC's MFDs, HUD and lamps (estimated 1-2 %) no longer hold the rise, and a city filling the canopy still stops it. Test: the cockpit case now uses sat 0.1. The EV check on the real VC (ngtV) is left with SHOULD 1.
- NIT 1 (fixed): the across widths sxp, sxh and hw are capped at 0.25 block in NightLamps and in LampField.
- NIT 2 (fixed): NightWeight sets s = 1 when the sun is up and elevation + dip passes 90 deg. New test "a far camera on the sunward side is not in the night".
- NIT 3 (fixed): BldgMgr::Night pushes D in 0.003 steps, not 0.01.
- NIT 4 (recorded): added to the design's Rev 2 as a risk, to be read off ngtT.
- Re-checks done:
  - Offline glslangValidator (/tmp/earth/night/spv.sh): TerrainPS SPIR-V with the Earth flags without `_NIGHTLOOK` is identical to the file with my blocks cut. With `_NIGHTLOOK` it compiles, and with the full Earth set (`_WATER _RIPPLES _CLOUDSHD _CLOUDATLAS _WATERFX _SSR _VOLSHD _NIGHTLIGHTS _NIGHTLOOK _LOCALLIGHTS _SHDMAP _FILMIC _CSM`) it compiles.
  - AutoExpo.Test built standalone in /tmp/earth/night/ut (g++ -O2 against the build tree's catch2, no shared target touched): "All tests passed (21654 assertions in 20 test cases)".
  - The shared build (BUILD EXIT) after these fixes is NOT DONE: the build script is missing.
- Files touched by the fixes: OVP/VulkanClient/shaders/NewPlanet.glsl (my two `_NIGHTLOOK` regions plus the glare line after cNgt2), AutoExpo.cpp, Tests/AutoExpo.Test.cpp, BldgMgr.cpp (one line in Night()).
- State at 17:41 CDT: /tmp/earth/vfx holds only build.lock, and /tmp/earth/cs and Xvfb are still missing. Every code fix is in the source. Left to do: the shared build and the display runs listed under SHOULD 1. These two need the integrator's scripts back.
