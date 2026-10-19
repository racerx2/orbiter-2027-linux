// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// not upstream: a .glb from Blender in a vessel's mesh folder stands in for its .msh meshes (claude/design-gltf-roundtrip.md 2, 3)

#ifndef __GLTFDROPIN_H
#define __GLTFDROPIN_H

#include <memory>

class Mesh;
struct RtFile;

struct GltfDropSlot {
	std::shared_ptr<const RtFile> file;
	int entry = -1;     // the file's recorded mesh
	int addedBase = 0;  // index of the first new group in the built mesh
};

bool GltfDropInMesh (const char *meshname, Mesh &mesh);         // true: mesh is a copy of the one built from a drop-in file
bool GltfDropInSlot (const char *meshname, GltfDropSlot &slot); // the drop-in file for a mesh name (its mesh built if not yet)
void GltfDropInFlush ();                                         // session end

#endif // !__GLTFDROPIN_H
