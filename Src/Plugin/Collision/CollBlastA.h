// not upstream: collision addon, blast: Voronoi cells of one vessel slot, NVIDIA Blast asset, family, actors and stress solver (design-CA-blast 2)
#ifndef COLLBLASTA_H
#define COLLBLASTA_H
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Vecmat.h"

struct NvBlastAsset;
struct NvBlastFamily;
struct NvBlastActor;
class NvBlastExtDamageAccelerator;
namespace Nv { namespace Blast { class ExtStressSolver; } }

constexpr double BLAST_CELL      = 0.12;     // cell edge = this * Size [m]
constexpr int    BLAST_MIN_CELLS = 8;
constexpr int    BLAST_MAX_CELLS = 256;
constexpr double BLAST_RHO       = 2700;     // skin density, aluminium [kg/m^3]
constexpr double BLAST_T_MIN     = 1e-3;     // skin thickness clamp [m]
constexpr double BLAST_T_MAX     = 0.05;
constexpr double BLAST_SIGMA_Y   = 2.76e8;   // al_structure yield (6061-T6) [Pa]: compression elastic limit
constexpr double BLAST_SIGMA_U   = 3.1e8;    // al_structure ultimate [Pa]: compression fatal limit
constexpr double BLAST_SIGMA_C   = 0.5e6;    // al_structure sigma_c of the dent table: hit material scale reference [Pa]
constexpr double BLAST_TENSION   = 0.5;      // tension limits = this * compression
constexpr double BLAST_SHEAR     = 0.6;      // shear limits = this * compression
constexpr int    BLAST_ITER      = 64;       // solver iterations per frame
constexpr double BLAST_LIVE      = 2.0;      // solver runs this long after a live hit [s]
constexpr double BLAST_KICK      = 0.1;      // separation speed = this * vn
constexpr int    BLAST_CONVERGE  = 8;        // impact frame: add + update at most this often until converged
constexpr double BLAST_TAU_MIN   = 0.002;    // contact time floor [s]: F = Jn / max (depth / vn, this)
constexpr double BLAST_MASS_CUT  = 0.9;      // at most this fraction of the empty mass leaves with debris

struct CollBlastInput {                      // triangles of one slot, rest frame of its static class
	std::vector<Vector> v;                   // 3 corners per triangle
	std::vector<uint32_t> w;                 // weld id per corner (DENT_WELD grid)
	std::vector<int> piece;                  // per triangle: -1 static class (cells), else the animated piece index (one chunk)
	double size = 10, mass = 1000;           // Size [m], EmptyMass [kg]
	int maxCells = 64;                       // cfg.blastCells
};
struct CollBlastChunk { int cell = -1, piece = -1; Vector c; double area = 0, mass = 0; };
struct CollBlastBond { uint32_t a = 0, b = 0; double area = 0, len = 0; Vector c, n; uint32_t asset = 0; bool prox = false; }; // chunks a < b; asset: Blast bond index; prox: joins an unwelded island
struct CollBlastSplit { std::vector<uint32_t> chunks; double mass = 0; Vector c, n; }; // one actor separated from the main one: chunks, mass, centroid, unit kick direction (rest frame)

class CollBlastA {
public:
	CollBlastA () {}
	~CollBlastA ();
	CollBlastA (const CollBlastA &) = delete;
	CollBlastA &operator= (const CollBlastA &) = delete;
	static std::vector<Vector> Sites (const CollBlastInput &in); // farthest-point sites over the static triangle centroids, %.9g
	static size_t SiteCount (const CollBlastInput &in);
	bool Build (const CollBlastInput &in, const std::vector<Vector> *given = nullptr, const Vector &cg = Vector ()); // given: saved sites; cg: vessel CG (main actor ties)
	void Material (double sigmaY, double sigmaU);           // stress limits [Pa]
	void Force (const Vector &c, const Vector &F);          // contact force [N] at c, applied at the next Step
	void Spin (const Vector &com, const Vector &w);         // centrifugal load of the vessel spin [rad/s] about com
	void Impact (const Vector &c, double R, double damage); // impact spread damage 0..1 within R of c
	std::vector<CollBlastSplit> Step ();                    // impact, stress update, fracture, split: actors that left the main one
	void Restore (const std::vector<uint32_t> &bonds, const std::vector<uint32_t> &removed, const std::vector<uint32_t> &weak = {}); // load: broken bonds, removed chunks, weakened bonds (W rows), split, no reports
	std::vector<uint32_t> WeakPairs () const;               // W rows: chunk key pair, remaining health x 1e6 (1..999999), sorted by pair
	std::vector<uint32_t> Broken () const;                  // broken bond indices (ours), sorted
	std::vector<std::vector<uint32_t>> Partition () const;  // chunks of every actor, sorted
	std::vector<uint32_t> MainChunks () const;
	int ChunkOfCell (uint32_t cell) const;
	int ChunkOfPiece (int piece) const;
	uint32_t ChunkKey (uint32_t c) const;                   // cell, or site count + piece
	std::vector<uint32_t> BrokenPairs () const;             // K rows: key a * 65536 + key b (a < b), sorted
	std::vector<uint32_t> BondsOfPairs (const std::vector<uint32_t> &pairs) const;
	double Health (uint32_t b) const;                       // remaining bond area [m^2]
	bool Ok () const { return main != nullptr; }
	std::vector<Vector> site;                               // rest frame, %.9g
	std::vector<int> cellOf;                                // per input triangle: cell, -1 animated
	std::vector<CollBlastChunk> chunk;
	std::vector<CollBlastBond> bond;
	std::vector<uint8_t> gone;                              // chunks that left the main actor
	double t = 0, Acell = 0, Atotal = 0, maxArea = 0, meanMass = 1;
	static uint64_t logErrors;                              // Blast error messages seen
private:
	struct Buf { std::vector<unsigned char> b; void *Get (size_t n); };
	void Release ();
	void Split (std::vector<CollBlastSplit> *out);
	std::vector<uint32_t> Chunks (const NvBlastActor *a) const;
	Buf assetMem, familyMem, scratch;
	NvBlastAsset *asset = nullptr;
	NvBlastFamily *family = nullptr;
	NvBlastActor *main = nullptr;
	Nv::Blast::ExtStressSolver *solver = nullptr;
	NvBlastExtDamageAccelerator *accel = nullptr;
	std::vector<uint32_t> nodeOf;                           // chunk -> graph node
	std::vector<std::pair<Vector, Vector>> force;
	Vector com, w; bool spin = false;
	Vector impC; double impR = 0, impD = 0;
	Vector cg;
};
#endif
