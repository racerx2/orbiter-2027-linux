# NVIDIA Blast (subset)
Not upstream for Orbiter: used by the collision addon (Src/Plugin/Collision) for live structural fracture.
Source: https://github.com/NVIDIAGameWorks/Blast, version 5.1.0, commit 636e7eb43b8d41e44c40eb1b4799bfd027f17ba7 (2026-09-28), BSD-3-Clause (LICENSE.md).
Taken unchanged: source/sdk/{lowlevel,globals,common}, source/sdk/extensions/{stress,shaders}, source/shared/{stress_solver,NsFoundation}, include/{lowlevel,globals,shared,extensions/stress,extensions/shaders}.
Built by Src/Plugin/Collision/cmake/blast.cmake as the static library CollBlastLib: C++14, warnings off; GCC/Clang add -include limits/cstdio/cstdint/cstring, and -mavx -mfma for source/shared/stress_solver/stress.cpp and source/sdk/extensions/stress/NvBlastExtStressSolver.cpp only (the solver checks AVX and FMA3 at run time and falls back to scalar); MSVC uses no /arch flag.
