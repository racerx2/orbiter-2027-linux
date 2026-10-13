# Common brief for the "UE5 batch" design agents (read fully)

Project: native Linux/Vulkan port of the Orbiter space flight simulator. The VulkanClient (OVP/VulkanClient) is a port of the D3D9Client to Vulkan, shaders in GLSL compiled at run time by glslang (OVP/VulkanClient/shaders, copied to the build tree's Modules/VulkanClient).
Repo on the user's machine: /home/racerx/Projects/Orbiter-linux-custom, branch Orbiter-linux-custom (HEAD dd4e087c). Reach it ONLY with the Desktop Commander tools (ToolSearch "select:mcp__remote-devices__Desktop_Commander__read_file,mcp__remote-devices__Desktop_Commander__start_process"). Each call must return within 60 s; wrap shell pipes in bash -c '...' or use python3 -c. READ-ONLY: do not edit, build, run Orbiter or touch git (git log/show/diff are fine). Screenshot runs are using the display.

Recent work you must build on (read these files in THIS container with the Read tool):
- /home/claude/gg/design-visual-batch.md (Rev 2 + Rev 3 as built): TAA (PostTaa: jitter, depth every frame, reprojection), PostLook (haze, shafts, flare, sharpen, vignette), water (_WATERFX), volume cloud shadows (SceneFxShadow), filmic, the Effects config group, the hooks in Scene::RenderMainScene.
- /home/claude/gg/map-postprocess.md (older line numbers; re-check in Custom): render order, buffers, SceneFx, IPI ImageProcessing API, camera/projection, exposure, tone curves, exhausts/particles, config/dialog/docs pattern, tests.
- /home/claude/gg/map-water.md: the terrain/water shader.
- /home/claude/gg/vfx-changes.md: what the four agents built (file by file).
Key facts: SceneFx::AfterOpaque runs DepthPass (tDepth R32F camera distance, 0 none; every main frame while TAA runs), SSAO, VolShadow, Clouds; then PostTaa::Resolve, late labels, PostLook::Haze; exhausts/particles/VC/glass later; Measure; LightBlur; PostLook::Final; RenderGlares; GDI; 2D overlay. Only vessels/cockpit are in GBUF_DEPTH (normals+distance) and only when glares/flare/shafts are on. NewPlanet.glsl has no free sampler binding (24 is shared by tGlare/tCloudAtlas; 26-28 are _DEVTOOLS overlays). Custom also has PBR/glTF meshes (PbrRender, PbrMesh.fx), XR vessels and legacy Mesh/Metalness paths; all must keep working.

Rules for every design:
- One VulkanClient.cfg switch per feature (dialog: the Effects group, which can grow), read once at start. Off = the legacy image: shader code only under a macro added only when on, C++ passes skipped, no buffers.
- Our own code from published papers/talks (cite them in one line); NO GPL code, NO Khronos/vendor sample code, nothing paid. One-line comments only.
- Legacy rendering and add-ons keep working; the 2D panel/HUD/MFD/GDI are never touched; no text over the sim screen.
- Budget: name a cost target at 1080p on an NVIDIA desktop GPU and how to measure it (Vulkan timestamp queries were used by an agent before).
- TAA-aware: noise per frame where TAA can average it; sub-pixel stable when TAA is off.
- Line numbers must be from the Custom tree as it is now.

Deliverable: /home/claude/gg/design-<name>.md (Write tool, this container), in the style of design-visual-batch.md: Repo line, 0 Rules, then numbered sections: what it does and why, where it hooks (Custom file:line), passes and buffers (formats, sizes), shaders (entries, macros, bindings), data needed and where it comes from, C++ changes per file, config/dialog/docs, cost, interplay with the existing effects (TAA, SSAO, volume clouds, shafts, flare, water, filmic, exposure, LightBlur), PBR/glTF/XR notes, risks and open questions, checks (scenarios and pass/fail criteria), pure-maths units with tests. Tight bullets, no padding. Also list the files the implementation would touch, so the integrator can split ownership with the other four features (shadows, SSR, volumetric plumes/plasma, SSGI, height fog). Final reply: 8-12 lines summary + the touched-files list.
