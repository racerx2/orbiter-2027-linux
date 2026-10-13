# vfx: better water and the 2D cloud shadow fade (agent "water")

Repo Orbiter-linux-custom, branch visual-fx (HEAD 6189b7a9). Uncommitted. Only my two files were edited.

## Files
- **OVP/VulkanClient/shaders/NewPlanet.glsl** (TerrainPS, all new code under `_WATERFX` or `_VOLSHD`):
  - `vUVWt0`: the unscrolled ocean uv (whole repeats per L14 tile). `fFoam` declared at the top.
  - Second wave tap on the lattice uv2 = (2u + v, 2v − u) (26.6°, √5 finer), scrolled by (24, 18) repeats per 3600 s, so the Time wrap is seamless.
  - Whiteout blend on unit normals, then scaled back to the stored z (0.5 in D3D9Ocean.dds), so `z *= wNrmStr` acts as today. Then `xy *= mix(1, 0.15, smoothstep(2e3, 30e3, dst))`.
  - Fresnel: Schlick F0 0.02 × wSpec × fMask; the glint on V·H, the sky on V·N.
  - Roughness: `min(mix(0.08, 0.35, smoothstep(1e3, 80e3, dst)), 0.2)`, plus a specular AA term from the pixel derivatives of the water normal (Kaplanyan 2016, α² += min(2σ², 0.18)). This is not in the design: without it the 0.08 lobe sparkle-aliased at 1–4 km.
  - Glint gain × 2π (not in the design, see Results). The lit ground has no 1/π and a ×2, so a physical F0 alone made the glint about 7× too weak against the water.
  - Sky: in air GetAmbient(reflect) × F × fSrf as today. In space AmbientApprox(vPlN).rgb × a × 0.6 × F, without fSrf.
  - Colour: `mix(cTex, (0.012, 0.045, 0.075), 0.25 fMask) + (0, 0.08, 0.07) · max(fDPS, 0) · fMask · 0.3`, then × (1 − F_sky). This replaces the fixed tint.
  - Foam (only where fMask > 0 and dst < 8 km):
    - fL is the mean of 8 `textureLod(tMask)` taps on a 100 m ring at the Prm.vWaterFx mip.
    - The noise is two ClsNoise octaves on vUVWt0: 64 and 256 cells per repeat, with periods 64 and 256 and shifts (64, 128) and (512, 256) per 3600 s, so it is seamless over tiles and over the Time wrap.
    - foam = fMask · smoothstep(0.03, 0.2, fL) · (1 − smoothstep(0.35, 0.7, fL)) · smoothstep(0.4, 0.7, n) · (1 − smoothstep(3e3, 8e3, dst)).
    - Foam scales the glint and the sky reflection by (1 − foam).
    - It is lit as white ground after the lighting line: `pow(0.8, TrGamma) · TrExpo · (cSF · fX · fShadow · fShd · 2 + cA + ambient)`. Lit as water (fDPS²), it came out darker than the sky reflection.
  - Specular: `cSun · fSpe · smoothstep(fDPS) · fShadow · fShd` (fShadow already has fH).
  - `_VOLSHD`: `fShd = mix(fShd, 1, Prm.vVolCld.z · (1 − smoothstep(35e3, 60e3, dst)))` just before the fX line.
- **OVP/VulkanClient/Surfmgr2.cpp** (SurfTile::Render, after fTgtScale; written on every tile):
  - `vWaterFx = (mip, ru, rv, 0)`, where:
    - texel size is tile extent (bnd, lng × cos lat) / (512 × texrange size);
    - mip = clamp(log2(50 m / max(tu, tv)), 0, 9). Note: the design's log2(texel/50) has the wrong sign;
    - ru, rv = 100 m ring radius in mask uv, capped at 0.05.
  - `vVolCld = (35e3, 60e3, z, 0)`, where z = scene->GetVolShadowFade() in the main pass for the camera's proxy planet, else 0.

## Build
- /tmp/earth/water/b2.log: BUILD EXIT=0. The build-tree NewPlanet.glsl matches the source.

## Off = old
- glslangValidator compile of the old and new NewPlanet.glsl (TerrainPS and TerrainVS) with the Earth flags without _WATERFX/_VOLSHD gives byte-identical SPIR-V (also checked after the last edit). The C++ only fills the two Prm fields, which the off shaders do not read (vVolCld is read only by CloudPS, and cloud tiles write their own).
- The shaders compile with both macros, also without _RIPPLES. No shader errors in the client logs.

## Tests (TAA, LensFlare, LightShafts, Vignette, Sharpen, HeatHaze = 0 in all runs; shots /tmp/earth/shots/<tag>-24.png)
- New scenarios (build tree only): Tests/Gfx Water Coast 300, Coast 3km (KSC), Sea 1500, Orbit, Orbit Atlantic, Orbit Glint.
- **Coast 300 m** (wat9on vs wat1off):
  - glint path brighter and wider; navy water instead of the light tint;
  - sparkle density like legacy after the AA term (v1 without it and with raw-z whiteout: a speckle field);
  - a thin line of white foam flecks on the beach line.
- **Coast 3 km** (wat8on / watCon vs wat2off): a mottled white surf band, about 100 m wide, along the Cape beach and lagoon shores. Ponds smaller than the ring get none. A debug run (watd1/watd2: R fL, G foam) confirmed fL ≈ 0.5 at the waterline and a foam band out to about 100 m.
- **Sea 1500 m, many L14 edges** (watAon vs watAoff):
  - crossed waves and a strong glint path;
  - no seams at ×3 contrast;
  - glint p99.9 luminance 253 on vs 205 off.
- **Orbit glint** (Gfx Water Orbit Glint, camera on the computed specular point; wat7on vs wat7off): centre ocean patch p99.9 luminance 104 vs 108, max 113 vs 115.
  - The water is about 7 % darker overall, from the removed blue tint.
  - Without the 2π gain (wat3 Mediterranean, v1), the glint area was 25 % darker (86.6 vs 116.1).
- **Alps High, VolCloudShadows 1 vs 0** (watB1 vs watB0, with the shadow agent's SceneFxShadow.cpp of 10:52):
  - within ~35 km the 2D layer's shadows are gone and the volume shadows show;
  - smooth fade to 60 km; no change beyond.
- A diagonal colour seam in the Atlantic orbit view is in both on and off (Surf imagery), not mine.

## Left / notes for others
- Foam also appears on lagoon and lake shores, since there is no wave-exposure data. The mask tile-edge clamp is accepted as designed.
- The water is darker than the legacy look by design (no tint). wBrightness is no longer used under _WATERFX.
- Nothing is needed from other owners.
