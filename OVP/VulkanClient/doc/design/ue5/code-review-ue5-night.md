# Code review: night look (NightLook, UE5 batch)

Repo Orbiter-linux-custom, branch ue5-batch on 35e2949c plus the uncommitted night files as of the owner's DONE (17:20; the sources were last written 16:31-17:02). Paths are under OVP/VulkanClient/ unless shown otherwise. This is a read-only review: nothing was built or run. The only checks run were validate-only glslangValidator calls with output to /dev/null, and a pixel count of the existing shots in ~/Pictures/orbiter-vfx.

**Verified.**
- **Design review MUSTs, all fixed in code.**
  - M1, the orbital term: weighted by smoothstep(0.35, 0.8, CamSpace) (NewPlanet.glsl:942). The near term is ×(1 − CamSpace) as before.
  - M2, the near-saturated share: now counted on full-res pixels (SceneFx.cpp:526-546, Exposure.glsl PSHalfSat/PSBlocksSat).
    - An MSAA or unsampleable target is first copied into tFull, which is freed in the dtor.
    - The sampled target's layout change ends rendering first (VkCore.cpp:1575-1577).
    - By day the legacy three-blit chain and PSBlocks run unchanged.
  - M3, the band:
    - Only lo is lowered (AutoExpo.cpp:61). hi, satLimit and the downward rule keep the legacy formula.
    - n is taken over the camera's own horizon, sin(el + dip) (:98).
  - M4, lamp filtering: per-family footprints fwidth(qw.x)/fwidth(qw.y), at three levels (NewPlanet.glsl:524-561).
  - M5, NaN: max(I, 1e-3) (:925).
  - M6, the window step: clamp(0.03 + 0.03·max(lod, 0), 0.03, 0.15) (PBR.fx:401).
- **Design review SHOULDs and NITs.**
  - S1, nested mips: NiteNest, tested child by child.
  - S2: done as Rev 2 says. Internal views keep the day band and clamp, and the brake stays.
  - S3: ownership is listed.
  - S4: the scenarios are added, but the runs are open (SHOULD 1).
  - NITs 1-5 and 7-10 are in the code: spill ×Hw/hw, exp(−r²/s²) with π s², width ≥ 1.0·footprint, a ≤ 0.85, a = 0 on never-lit texels, GetLandView in uniform flow, tClass re-fetched in the uniform branch, any rise braked, -O2 on the test.
- **Exposure maths.**
  - With n = 0 or the key off, Target is the HEAD code verbatim (AutoExpo.cpp:49-59 vs 35e2949c).
    - The Update clamp reduces to min(maxEV, topEV).
    - SatThreshold returns 254/255.
    - The test checks the EV sequence to the bit.
  - Every night term is continuous in n: the band −3n, the clamp mix, the brake n·S(sat) and the threshold mix. A sun crossing +3° therefore causes no step.
  - No pumping:
    - The brake only scales rises.
    - Between brake1 (3 %) and satLimit (15 %) there is a dead band. sat is monotone in e.
    - A rise stops and nothing pushes back down.
  - NaN: SetNight and NightWeight reject non-finite input. Update already rejects non-finite samples. PSHalfSat has no division.
  - Daylit content: in the daylit orbit shots, 2.0-6.7 % of pixels are ≥ 0.9, and on the high cloud deck 12.3 % are. All are under satLimit 0.15, so the downward rule does not fire on them (see NIT 4 for the brake).
- **TAA and LightBlur.**
  - The measure runs after the TAA resolve and LightBlur (bMeasureLate). Glow around lights only makes the brake act earlier.
  - Lamps and windows are static, so there is nothing for TAA to average. fwidth of the jittered frame differs by sub-pixel amounts only.
- **Windows.**
  - No inversion at negative lod.
  - At D = 1 every lit window is fully on (a + w ≤ 1.0). At D = 0 nothing is added at any lod.
  - The branch is uniform per draw (gCfg, gMtrl). textureQueryLod is core in 460.
  - Mips:
    - Levels 1-3 are the box, with the on-time weighted by luminance (dark children do not move a).
    - Levels ≥ 4 are one hash-chosen child, so every coarse point exists one level finer. The trilinear blend from level 3 to 4 is continuous, so there is no popping.
    - RePad is a no-op from level 3 for every map (g = 8/16 = 0), so stopping it at level 4 changes nothing.
- **Key off and daytime.**
  - With the key off:
    - No macro and no C++ path. tHalf usage and blits are legacy.
    - PSBlocks/PSTotal and TerrainPS keep their SPIR-V; PBR.fx's preprocessed text is identical (the owner checked all three).
  - With the key on by day:
    - fNgt = 0 makes cNgt = cNgt2 = 0 exactly.
    - D = 0 adds 0. Emission2 stays 0 as Make sets it, so the legacy line was max(cDiff, 0) as well.
    - n = 0 gives the legacy exposure.
- **PBR, glTF and legacy meshes.**
  - PbrMesh.fx has no `_NIGHTLOOK` code.
  - FAST_PS is untouched: facades with maps never render fast.
  - Other meshes enter the new branch only with SpecialFX.w = 0.75, which only BldgMgr sets.
  - A missing facade_nite.dds leaves `nite` false and the legacy windows on.
- **Compiles** (glslangValidator -V, vulkan1.3, validate only):
  - TerrainPS compiles with the full Earth set (`_WATER _RIPPLES _CLOUDSHD _CLOUDATLAS _WATERFX _SSR _VOLSHD _NIGHTLIGHTS _NIGHTLOOK _LOCALLIGHTS _SHDMAP _FILMIC _CSM`), with `_PERFORMANCE`, and with `_DEVTOOLS _CSM`.
  - TerrainVS compiles with the full set.
  - All four Exposure entries compile.
  - D3D9Client.fx/PBR.fx could not be checked outside the client (its loader transforms the D3D9 syntax).
- **The CPU copy matches the shader.** NightLook::LampField matches NightLamps term for term: hash arguments, palette split, band, points and per-family means. NightLamps has no derivatives inside the per-pixel `fNgt > 0` branch.
- **Rules.**
  - Comments are one line, with `// not upstream` in the upstream files.
  - ClsHash is credited (Hoskins, MIT). The integer finaliser is already used in five project files.
  - Blackbody values are table data. No copied GPL or vendor code was seen.

## MUST
None.

## SHOULD

1. **The display checks are not run (ue5-night-changes.md:35-39; the 17:18 reboot wiped /tmp/earth).**
   - **Problem:** Nothing yet shows that:
     - the shaders compile in the client with all six features on (no "[ERROR]" line);
     - day is unchanged (ngtY, which is also the only runtime check of the M5 NaN fix);
     - key off is the old image (ngtN0);
     - the NYC numbers meet §15 (building region < 3 % ≥ 250, ground mean < 60, p95/p50 ≥ 2.5).
   - Neither of the MUST-fix checks has run: ngtM (M1, 30 km continuity) and ngtT (M3, terminator EV within 0.1).
   - **Fix:** before the commit, run:
     - ngtA1/ngtA0/ngtD1;
     - one day pair, "Gfx Shot New York" with NightLook 1/0;
     - ngtM, ngtT and ngtV (SHOULD 3).
   - Record the numbers in "## Review fixes". If the run limit forces a choice, the day pair and ngtA1 come first.

2. **NewPlanet.glsl:928-936: the night block costs ALU by day too.**
   - **Problem:**
     - NightWarp (two ClsNoise, 8 hashes), fwidth and the extra tClass fetch run for every Earth pixel when Flow.bClass is set (camera under 20 km with a Class tile).
     - The extra GetLandView (6 fetches, Scatter.glsl:744-746) runs for every Earth pixel at any altitude, day and night.
     - Only NightLamps is gated by fNgt. §11 promises "0 by day", and nothing was measured (changes :41).
   - **Fix:**
     - Take the footprint from the unwarped lattice coordinate: `fq = fwidth(q)` with q linear in texUV. The warp's slope is ≤ ~0.1, so the footprint stays within ~10 %.
     - Move `qw = NightWarp(q)` inside the `fNgt > 0.0` branch, so only the uniform fwidth stays outside.
     - For the head term, either rely on the driver's CSE and prove it with a frame-time A/B by day, or keep the head glare in its own vec3 and add it ×sct.atn where cNgt2 is added (:1034, under `_NIGHTLOOK`).

3. **AutoExpo.cpp:70 with SceneFx.cpp:509: the brake runs in internal views, and the count is now per full-res pixel.**
   - **Problem:** Every VC pixel at ≥ 0.9 (n = 1) counts: MFD text and lines, the VC HUD, lit panel lamps, which are at 1.0 already at e = 1.
     - On the switch to a VC, ev resets to 0.
     - If those pixels reach brake1 (3 % of the frame), the VC never rises. That is the 2 EV darker night cockpit that S2 set out to remove.
     - Between 0.5 % and 3 % it rises more slowly to the legacy level.
   - My estimate for a DG VC at 1080p: two MFDs are about 6 % of the frame and their bright content 10-20 % of that, so 1-2 % with the HUD. That means slower, not stuck, but it is unchecked.
   - **Fix:**
     - Run ngtV (Gfx Night KSC VC, MFDs and HUD on), NightLook 1/0, and record EV after 20 s.
     - If the VC sits more than 0.3 EV under legacy, widen the internal brake window (for example brake0/brake1 ×3 while band = 0). The city through the canopy still stops a rise.

## NIT

1. **NewPlanet.glsl:538 (AutoExpo.cpp LampField): only the nearest line of a family is evaluated, but the widths grow past half a block.**
   - **Problem:** Once fu > 0.25, the spill (2·hw > 0.5) and the pool band are cut at the block midline.
     - Where the two lines differ in lit state, that pixel column steps by up to ~8 % of the family's spill mean at fu 0.3 (×(1 − tA) = 0.26).
     - There is also a small energy dip before tA reaches 1. It is within the test's 3 %.
   - **Fix:** cap hw, sxp and sxh at 0.25 and let tA carry the rest, or move the area level to 0.15-0.25.

2. **AutoExpo.cpp:98: sin(el + dip) folds back once el + dip > 177°.**
   - **Problem:** A camera far out on the sunward side gets n > 0 and the night band. That needs dip > 87°, so r > ~19 R, with the sun within ~3° of its zenith.
   - **Fix:** use s = 1 when cos(el + dip) < 0 and se > 0, that is, cap the angle sum at 90°.

3. **BldgMgr.cpp:413: D is pushed in 0.01 steps.**
   - **Problem:** With w = 0.03 at lod 0, the slope is 0.75/w = 25. Each push moves a window up to 25 % of its brightness.
     - At the real sun rate that is a push about every 24 s.
     - Close-up windows brighten in 4-5 visible steps rather than switching or fading.
   - **Fix:** use 0.003. The cost is one SetMaterialEx per cell per push.

4. **AutoExpo.cpp:70: the brake also holds daylit content at n = 1.**
   - **Problem:** The daylit orbit shots have 2-7 % of pixels ≥ 0.9, and the cloud deck 12 %. That is past brake1, so from a night-side orbit camera the in-band drift back to e = 1 after a bright view is held.
     - This is the design (review NIT 9). The downward rule does not fire (under 15 %).
   - **Fix:** record it in the design's risks and read it off ngtT.

Counts: MUST 0, SHOULD 3, NIT 4.
