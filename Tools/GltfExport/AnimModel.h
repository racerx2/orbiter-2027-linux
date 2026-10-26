// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: Orbiter animations as glTF nodes and channels (claude/design-gltf-export.md 5)

#pragma once
#include "GxTypes.h"

struct GxAff {
	double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	double t[3] = {0, 0, 0};
	GxVec3 Point (const GxVec3 &p) const;
	GxVec3 Dir (const GxVec3 &d) const;
	GxAff operator* (const GxAff &b) const; // this after b
	GxAff Inverse () const;
};

double GxFrac (const GxComp &c, double s);
bool GxDegenerate (const GxComp &c);    // state0 == state1 or a zero axis: Orbiter applies no transform
GxAff GxCompTransform (const GxComp &c, double fa, double fb); // Orbiter's transform from fraction fa to fb

// rest parameters from live ones moved by the ancestors up to each animation's state (design 5.2)
void GxRecoverRest (GxAnimSet &set);

GxVec3 GxToGltf (const GxVec3 &v);      // (x, y, -z)
void GxQuatGltf (const GxVec3 &axis, double angle, double q[4]); // Orbiter rotation as a glTF quaternion (x, y, z, w)

struct GxNode {
	std::string name;
	int parent = -1;
	std::vector<int> children;
	GxVec3 t;                // rest translation, glTF frame
	GxVec3 poseT;            // the node's TRS with each animation at its state (design-gltf-simpose-glow 1)
	double poseR[4] = {0, 0, 0, 1};
	GxVec3 poseS{1, 1, 1};
	GxVec3 pivot;            // Orbiter vessel frame
	int comp = -1;           // component of a chain node
	int mesh = -1, group = -1; // a group's mesh node (index into GxSnapshot::meshes and its groups)
	int meshOf = -1;         // a mesh node: index into GxSnapshot::meshes
};

enum class GxPath { Rotation, Translation, Scale };

struct GxChannel {
	int node = 0;
	GxPath path = GxPath::Rotation;
	std::vector<float> times, values; // values: 4 per key (rotation) or 3
};

struct GxGltfAnim {
	std::string name;
	int anim = 0;
	std::vector<GxChannel> channels;
};

struct GxModel {
	std::vector<GxNode> nodes;
	std::vector<int> roots;
	std::vector<GxGltfAnim> anims;
};

// labels[m][g]: the .msh LABEL of group g of snapshot mesh m, may be empty
void GxBuildModel (const GxSnapshot &snap, const std::vector<std::vector<std::string>> &labels,
	GxModel &out, std::vector<std::string> &warnings);
