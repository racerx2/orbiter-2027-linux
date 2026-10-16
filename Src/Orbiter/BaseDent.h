// not upstream: building dents and holes drawn by the core with the collision add-on's DentMath (design coll-bounded 5.7); plain types only, no Vecmat
#ifndef __BASEDENT_H
#define __BASEDENT_H
#include <cstdint>
#include <vector>

struct BaseDentVtx { float x, y, z, nx, ny, nz, tu, tv; };  // NTVERTEX layout
struct BaseDentGroup { std::vector<BaseDentVtx> vtx; std::vector<uint16_t> idx; uint32_t room = 0; }; // one object group: indices into its own vertices; room: vertices it may gain
struct BaseDentRec { uint32_t mode, seed, bits, flags; double c[3], n[3], t[3], R, h, T, P, hd, hz; }; // a DentRecord of the add-on, base frame

// rest groups and the object's records in order: refined, folded, normals turned, the holes' triangles dropped
void BaseDentApply (const std::vector<BaseDentGroup> &rest, const std::vector<BaseDentRec> &rec, std::vector<BaseDentGroup> &out);
#endif
