# vshd: ground shadows from the volumetric clouds (VolCloudShadows)

Agent vshd. Design: design-visual-batch section 8, rev 2 M3, M4, S8 (S9 is the water agent's, it reads volShdFade).

## Files
- `OVP/VulkanClient/shaders/VolShadow.glsl` (new). It #includes VolCloud.glsl and reuses its VolPS block, Slab, Coverage, CloudQ, Clump, Density, PixelRay and DepthW. Own sampler `tShd` at binding 18. No own uniform block: wV goes in vMarch.z, as VolMarch does.
  - `PSVolShadow` (half res, into R8):
    - Builds P from tDepth at the full-res texel the half-res texel stands for (`FullUV`), plus PixelRay.
    - tDepth 0 or beyond vFade.y (60 km) gives 0.
    - Takes Slab(P, toSun) and caps the path at 6 km.
    - Marches 12 midpoint steps of Density(p, Coverage(p)·Clump, 0), with VolMarch's clump fade over 25-35 km from the camera. Detail noise is skipped and there is no jitter, so there is no per-frame noise.
    - Result: (1 − exp(−od·0.04·0.6)) · (1 − smoothstep(35, 60 km, dist)) · wV.
  - `PSVolShdBlur`: 3×3 depth-weighted, as VolBlur (DepthW, centre weight 2).
  - `PSVolShdComposite`: VolComposite's depth-aware 2×2 up-sample at full res.
    - Output is black with alpha = shadow · 0.75 · luminance(GetLandView atn) · saturate(n·toSun) (S8).
    - The normal comes from tDepth, taking the nearer neighbour on each axis (as SSAO's ViewNormal, in geocentric axes).
    - tDepth 0 (sky) gives alpha 0.
- `OVP/VulkanClient/SceneFxShadow.cpp`:
  - `struct VolShadowState`: the IPI and two half-res R8 targets.
  - `FreeVolShadow`.
  - `VolShadow(target, vp, wV)`:
    - Builds its own ImageProcessing lazily, on the first call. That only happens with VolCloudShadows = 1, so with the key off nothing is compiled or created.
    - ppf is `_PERFORMANCE` when !bAtmoQuality, so the scatter table sizes match.
    - On a failure it logs once and stays off: volShdFade stays 0 and the volume clouds are untouched.
    - It returns unless DepthValid() (S4).
    - It copies Clouds()' VolPS setup (CameraRays, so the TAA jitter comes for free; map frame, cloud frame, slab, vFade, vNoise, vTex) and binds tDepthV, tCov, tShape, tLndAtn.
    - It runs shadow → blur → composite (Execute(1, true)) into the target.
    - volShdFade = wV only when the composite really drew, 0 otherwise.

## Tests (Xvfb :58, 1920×1080 render, NVIDIA)
- Builds b1–b3: BUILD EXIT=0, no warnings.
- Offline glslangValidator compile of all three entries, with and without _PERFORMANCE: OK.
- Client logs have no shader [ERROR] and no "Invalid variable name". The only [ERROR] lines are the pre-existing "UnDeleted Surfaces" at exit.
- Debug shader runs (raw shadow painted red, from a build-tree-only copy):
  - vshDbgA (Alps High): shadows lie on the anti-sun side of the clouds. The sun is at azimuth about 157°, elevation 38°, to the left of the view, and the mountains' lit faces agree.
  - Same run: no shadow in the sky, and the shadow fades out by about 60 km.
  - vshDbgC (Carpathians Under): overcast, the near ground is shadowed except the gaps.
  - vshDbgS (South Under, ocean): the shadow gap matches the layer gap.
  - vshDbgG (Germany Under): almost no clouds there, so almost no shadow.
- On/off pairs (TAA=0, other new effects off):
  - vshA1/vshA0 (Alps High).
  - vshC1/vshC0 (Carpathians Under): mean luminance ratio 0.93. The gaps are brighter because the 2D shadow fades by volShdFade; the rest is darker from the volume shadow.
  - vshH1/vshH0 (Germany High): almost cloudless there. Only one small cloud shadow, plus faint changes where the 2D shadow faded.
  - Switch off: VolShadow is never called, so there is no IPI and no shader, and the output is the legacy one.
- Cost, measured with GPU timestamps around the three passes (temporary code, now removed):
  - 0.094 ms (Alps High), 0.083 ms (Carpathians Under), 0.079 ms (Germany Under), 0.083 ms (South Under);
  - mean frame interval 21.1–21.6 ms in all runs.
- Stability: vshA2, the final build, shot at 20 s and 21 s with a static camera.
  - The frames differ only at the cloud edges (the clouds' own per-frame noise).
  - The shadowed terrain does not change, so there is no noise crawl.
- TAA on: vshT1 (Alps High, TAA=1, final build). No errors, and the shadows look the same as with TAA off.
  - CameraRays already folds the jitter into camZ, so this pass follows it.
- The build-tree debug shader (VolShadowDbg.glsl) has been deleted. Scratch files are in /tmp/earth/vshd.

## Notes / for other owners
- The VolPS constants (vNoise 1/12000, 1/1500, 0.35, 1; vFade 35e3, 60e3, 80e3, 0.04; the slab at cloudalt − 1000 + 3000) are copied from SceneFx::Clouds. If Clouds changes them, change SceneFxShadow.cpp too, or factor out a shared helper in SceneFx (owner of SceneFx.cpp).
- The 2D cloud shadow is a straight-down projection; the volume shadow follows the sun. In the 35–60 km cross-fade, the two shadows of one cloud sit in different places (by cloud height / tan(sun elevation)). The design accepts this.
- The S8 strength is applied as written: n·sun scales linearly, so flat ground at a 38° sun gets about 0.62 of the 0.75.
