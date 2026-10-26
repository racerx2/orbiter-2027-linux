# GltfExport

Saves a vessel with its meshes, textures and animations as one `.glb` file, for Blender and other 3D tools.

## Use
1. Launchpad, Modules tab, Developer: tick **GltfExport**.
2. In flight, with the camera on the vessel: Ctrl+F4, Custom functions, **Export vessel to glTF**.
3. The file is `Exports/<vessel name>.glb` in the Orbiter folder. A notification says when it is done; warnings go to Orbiter.log.

In Blender: File, Import, glTF 2.0. Each Orbiter animation is one action and one NLA track, one second per state unit (24 frames), named by its first labelled part. After the import Blender plays the first animation, so its parts show its first frame; to switch: Dope Sheet, sidebar (N), glTF tab, glTF Animations (turn on "Animation UI" in the glTF add-on's preferences), or pick the action in the NLA editor. Everything else stands as it was in the simulator when you exported (doors, gear, switches at their states). The exterior and the cockpit meshes are under the nodes "Exterior" and "Cockpit"; each animated part hangs from an empty at its hinge.

Glow is drawn as Orbiter draws it: a material's emissive colour lights its texture (Blender: emission from the same image), and an `_emis` map is combined with it into one emission image. Parts with "no lighting" (flag 4) carry their glow in the base colour.

Without a window (headless) or for batch use: `ORBITER_GLTF_EXPORT=1 ./Orbiter --plugin=GltfExport ...` exports the focus vessel on the third frame; `ORBITER_GLTF_EXPORT=thread` does it through the worker thread as the menu does (needs the window).

## Back into Orbiter
In Blender: File, Export, glTF 2.0, glTF Binary, with Include > Data > Custom Properties ticked (the Orbiter data rides on them), and keep each action's name starting with its number. Put the .glb in the vessel's mesh folder (`Meshes/dg-xr1` for the XR1); Orbiter uses it for every mesh it was exported from. Untouched parts, materials, textures and animations stay as the vessel has them; changed ones come from the file. Orbiter.log says what came from the file. Export from the vessel as it ships (no drop-in file in its folder), so the recorded parts are the program's.

Files from version 0.1 of this add-on can't go back: export them again.

## Not exported
Groups Orbiter does not draw (flag 2; they stay from the .msh when the file goes back), night textures, exhaust, beacons, particles, 2D panels, graphics client material configs, `_spec` and other maps besides `_norm`, `_emis`, `_rghn`, `_metal`. An animation that moves vertex lists only has no action (its data is in the scene's `extras.orbiter`).

## Build
Standalone against an Orbiter build or install:

    cmake -S Tools/GltfExport -B out/build/gltfexport -DORBITER_DIR=<Orbiter folder> -DFETCHCONTENT_SOURCE_DIR_CATCH2=<catch2 source>
    cmake --build out/build/gltfexport
    cmake --install out/build/gltfexport

Tests: `ctest --test-dir out/build/gltfexport`. `Tests/gltfx_ref` and `Tests/blender_check.py` check a written file against the client's animation code in Blender.
